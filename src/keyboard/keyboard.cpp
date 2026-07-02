#include "keyboard/keyboard.hpp"

#include "keyboard/key_event.hpp"
#include "log/log.hpp"
#include "lua/binds/key_event.hpp"
#include "perf/perf.hpp"

#include <filesystem>
#include <utility>

namespace hw {
namespace {
    constexpr UINT kModifiers[] = {VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU};

    bool IsInjected(const KBDLLHOOKSTRUCT& event) noexcept {
        return (event.flags & (LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED)) != 0;
    }
} // namespace

Keyboard::Keyboard(UINT super_vk, LuaServices lua_services, SuperCallbacks super_callbacks)
    : m_superVk(super_vk)
    , m_luaServices(std::move(lua_services))
    , m_superCallbacks(std::move(super_callbacks)) {
    m_finalizeAppExit = std::move(m_luaServices.request_app_exit);
    if (!m_luaServices.set_super_key) {
        m_luaServices.set_super_key = [this](UINT vk) { SetSuperKey(vk); };
    }
    if (!m_luaServices.request_config_reload) {
        m_luaServices.request_config_reload = [this] { return RequestReloadConfig(); };
    }
    m_luaServices.request_app_exit = [this] { return RequestExit(); };
    try {
        m_configPath = std::filesystem::absolute(m_configPath).lexically_normal().wstring();
    } catch (const std::filesystem::filesystem_error& error) {
        LOG_WARN("failed to normalize config path: {}", error.what());
    }
    m_luaServices.config_path = m_configPath;

    m_instance.store(this, std::memory_order_release);

    m_dispatchThread = std::jthread([this](std::stop_token stop_token) {
        SET_THREAD_NAME("KB-D");
        DispatchThreadMain(stop_token);
    });

    m_hookThread = std::jthread([this](std::stop_token stop_token) {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        SET_THREAD_NAME("KB-H");
        HookThreadMain(stop_token);
    });
}

Keyboard::~Keyboard() {
    Keyboard* expected = this;
    m_instance.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);

    m_dispatchThread.request_stop();
    m_hookThread.request_stop();

    const DWORD tid = m_hook.thread_id.load(std::memory_order_acquire);
    if (tid != 0) {
        PostThreadMessageW(tid, WM_NULL, 0, 0);
    }

    m_dispatchCv.notify_all();
}

void Keyboard::SetSuperKey(UINT super_vk) noexcept {
    if (super_vk != VK_LWIN && super_vk != VK_RWIN) {
        super_vk = VK_LWIN;
    }
    m_superVk.store(super_vk, std::memory_order_release);
}

bool Keyboard::RequestReloadConfig() noexcept {
    if (m_dispatch.exit_requested.load(std::memory_order_acquire) || m_dispatch.exit_handled.load(std::memory_order_acquire)) {
        return false;
    }
    const bool queued = !m_dispatch.reload_requested.exchange(true, std::memory_order_acq_rel);
    m_dispatchCv.notify_one();
    return queued;
}

bool Keyboard::RequestExit() noexcept {
    if (m_dispatch.exit_handled.load(std::memory_order_acquire)) {
        return false;
    }
    const bool queued = !m_dispatch.exit_requested.exchange(true, std::memory_order_acq_rel);
    m_dispatchCv.notify_one();
    return queued;
}

bool Keyboard::PushKeyFromHook(std::uint32_t encoded) noexcept {
    if (m_keyQueue.push(encoded)) {
        m_dispatchCv.notify_one();
        return true;
    }

    m_queueOverflowed.store(true, std::memory_order_release);
    m_dispatchCv.notify_one();
    return false;
}

LRESULT CALLBACK Keyboard::HookProc(int code, WPARAM wparam, LPARAM lparam) noexcept {
    HW_PERF_SCOPE(::perf::CounterId::KeyboardHook);

    Keyboard* self = m_instance.load(std::memory_order_acquire);
    if (code != HC_ACTION || self == nullptr || lparam == 0 || !IsKeyMessage(wparam)) {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    const auto* event = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
    if (IsInjected(*event)) {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    const UINT vk = event->vkCode;
    const UINT super_vk = self->m_superVk.load(std::memory_order_acquire);
    const bool is_down = (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN);

    if (vk != super_vk) {
        if (!self->m_hook.super_down.load(std::memory_order_acquire)) {
            return CallNextHookEx(nullptr, code, wparam, lparam);
        }

        self->PushKeyFromHook(EncodeKey(vk, wparam));
        if (IsModifier(vk)) {
            return CallNextHookEx(nullptr, code, wparam, lparam);
        }
        return 1;
    }

    const bool was_down = self->m_hook.super_down.exchange(is_down, std::memory_order_acq_rel);
    if (is_down != was_down) {
        if (is_down) {
            self->NotifySuperPressed();
        } else {
            self->NotifySuperReleased();
        }
    }

    self->PushKeyFromHook(EncodeKey(vk, wparam));

    if (is_down) {
        return 1;
    }

    // The SUPER down was swallowed. Let the matching up through once so Windows
    // and other hooks dont see a permanently held Win key.
    if (was_down) {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }
    return 1;
}

void Keyboard::HookThreadMain(std::stop_token stop_token) {
    m_hook.thread_id.store(GetCurrentThreadId(), std::memory_order_release);
    if (stop_token.stop_requested()) {
        m_hook.thread_id.store(0, std::memory_order_release);
        return;
    }
    m_hook.hook = SetWindowsHookExW(WH_KEYBOARD_LL, HookProc, nullptr, 0);
    if (m_hook.hook == nullptr) {
        LOG_CRITICAL("failed to install WH_KEYBOARD_LL hook: {}", GetLastError());
        return;
    }

    LOG_TRACE("keyboard hook installed");

    MSG message{};
    while (!stop_token.stop_requested()) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            break;
        }
    }

    UnhookWindowsHookEx(m_hook.hook);
    m_hook.hook = nullptr;
    m_hook.thread_id.store(0, std::memory_order_release);
    m_hook.super_down.store(false, std::memory_order_release);
    LOG_TRACE("keyboard hook uninstalled");
}

void Keyboard::DispatchThreadMain(std::stop_token stop_token) {
    lua::Runtime lua_runtime(m_luaServices);
    lua_runtime.LoadConfig(m_configPath);

    while (!stop_token.stop_requested()) {
        const auto predicate = [&] {
            return stop_token.stop_requested() || !m_keyQueue.empty() || m_queueOverflowed.load(std::memory_order_acquire) ||
                   m_dispatch.reload_requested.load(std::memory_order_acquire) || m_dispatch.exit_requested.load(std::memory_order_acquire);
        };

        {
            std::unique_lock lock(m_dispatchCvMutex);
            if (const auto next = lua_runtime.NextTimerDeadline()) {
                m_dispatchCv.wait_until(lock, *next, predicate);
            } else {
                m_dispatchCv.wait(lock, predicate);
            }
        }

        lua_runtime.FireExpiredTimers();

        if (m_dispatch.exit_requested.exchange(false, std::memory_order_acq_rel)) {
            ProcessExitRequest(lua_runtime);
        }

        if (m_dispatch.reload_requested.exchange(false, std::memory_order_acq_rel)) {
            ProcessReloadRequest(lua_runtime);
        }

        if (m_queueOverflowed.exchange(false, std::memory_order_acq_rel)) {
            LOG_ERROR("keyboard event queue overflowed; clearing modal key state");
            m_dispatch.keys.ClearAll();
            m_hook.super_down.store(false, std::memory_order_release);
            NotifySuperReleased();
            std::uint32_t discard = 0;
            while (m_keyQueue.pop(discard)) {}
        }

        std::uint32_t encoded = 0;
        while (m_keyQueue.pop(encoded)) {
            ProcessKey(encoded, lua_runtime);
        }
    }
}

void Keyboard::ProcessKey(std::uint32_t encoded, lua::Runtime& lua_runtime) {
    const UINT vk = DecodeKey(encoded);
    const bool down = IsKeyDown(encoded);
    const UINT super_vk = m_superVk.load(std::memory_order_acquire);

    if (down) {
        if (m_dispatch.keys.IsSet(vk)) {
            return;
        }

        if (vk == super_vk) {
            SeedModifierStates();
            m_dispatch.keys.Set(vk);
            lua_runtime.DispatchSuper(true);
            return;
        }

        m_dispatch.keys.Set(vk);
        if (!IsModifier(vk) && m_dispatch.keys.IsSet(super_vk)) {
            lua_runtime.DispatchBind(lua::KeyEvent{.vk = vk, .modifiers = CurrentModifierMask()});
        }
        return;
    }

    m_dispatch.keys.Clear(vk);
    if (vk == super_vk) {
        m_dispatch.keys.ClearAll();
        lua_runtime.DispatchSuper(false);
        if (m_dispatch.reload_deferred) {
            m_dispatch.reload_deferred = false;
            ProcessReloadRequest(lua_runtime);
        }
    }
}

void Keyboard::SeedModifierStates() noexcept {
    for (UINT vk : kModifiers) {
        if ((GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0) {
            m_dispatch.keys.Set(vk);
        } else {
            m_dispatch.keys.Clear(vk);
        }
    }
}

std::uint8_t Keyboard::CurrentModifierMask() const noexcept {
    std::uint8_t mask = 0;
    if (m_dispatch.keys.IsSet(VK_LSHIFT)) {
        mask |= lua::ModLShift;
    }
    if (m_dispatch.keys.IsSet(VK_RSHIFT)) {
        mask |= lua::ModRShift;
    }
    if (m_dispatch.keys.IsSet(VK_LCONTROL)) {
        mask |= lua::ModLCtrl;
    }
    if (m_dispatch.keys.IsSet(VK_RCONTROL)) {
        mask |= lua::ModRCtrl;
    }
    if (m_dispatch.keys.IsSet(VK_LMENU)) {
        mask |= lua::ModLAlt;
    }
    if (m_dispatch.keys.IsSet(VK_RMENU)) {
        mask |= lua::ModRAlt;
    }
    return mask;
}

void Keyboard::ProcessReloadRequest(lua::Runtime& lua_runtime) {
    if (m_dispatch.exit_handled.load(std::memory_order_acquire)) {
        return;
    }

    LOG_DEBUG("config reload requested");
    if (m_hook.super_down.load(std::memory_order_acquire)) {
        m_dispatch.reload_deferred = true;
        return;
    }
    lua_runtime.ReloadConfig(m_configPath);
}

void Keyboard::ProcessExitRequest(lua::Runtime& lua_runtime) {
    if (m_dispatch.exit_handled.load(std::memory_order_acquire)) {
        return;
    }
    m_dispatch.exit_handled.store(true, std::memory_order_release);
    m_dispatch.reload_requested.store(false, std::memory_order_release);
    m_dispatch.reload_deferred = false;
    LOG_DEBUG("keyboard app exit requested");
    lua_runtime.PrepareExit();
    if (!m_finalizeAppExit || !m_finalizeAppExit()) {
        LOG_ERROR("failed to queue app exit on tray thread");
        m_dispatch.exit_handled.store(false, std::memory_order_release);
    }
}

void Keyboard::NotifySuperPressed() noexcept {
    if (m_superCallbacks.pressed) {
        m_superCallbacks.pressed();
    }
}

void Keyboard::NotifySuperReleased() noexcept {
    if (m_superCallbacks.released) {
        m_superCallbacks.released();
    }
}

} // namespace hw
