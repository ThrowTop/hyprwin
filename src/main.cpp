#include "config/settings.hpp"
#include "keyboard/keyboard.hpp"
#include "log/log.hpp"
#include "mouse/mouse.hpp"
#include "overlay/service.hpp"
#include "perf/perf.hpp"
#include "res/resource_ids.h"
#include "tray/tray.hpp"
#include "util/debug.hpp"
#include "win/native.hpp"
#include "version.hpp"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <windows.h>

namespace {
constexpr int kExitAlreadyRunning = 2;
constexpr wchar_t kAppId[] = L"hyprwin.throwtop.dev";
#ifdef NDEBUG
constexpr char kBuildType[] = "release";
#else
constexpr char kBuildType[] = "debug";
#endif

struct AppState {
    hw::AtomicSettingsPtr settings;
    std::atomic<POINT> latest_mouse_pos{};
};
} // namespace

int hyprwin_main();

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
    logging::init({
      .console = false,
      .file_path = logging::make_run_log_file_path(),
    });
    const int r = hyprwin_main();
    perf::DumpSummary();
#ifndef HYPRWIN_RELEASE
    if (r == EXIT_SUCCESS) {
        hw::debug::ReportDxLiveObjects();
    }
#endif
    logging::shutdown();
    return r;
}

int hyprwin_main() {
    SET_THREAD_NAME("MAIN");
    LOG_INFO("hyprwin starting version={} build={} arch=x64 pid={} perf={}",
      HYPRWIN_VERSION,
      kBuildType,
      GetCurrentProcessId(),
      HYPRWIN_ENABLE_PERF != 0);

#ifdef HYPRWIN_RELEASE
    if (!win::EnsureRunAsAdminAndExitIfNot()) {
        return EXIT_FAILURE;
    }
#endif

    auto single_instance = win::SingleInstance::Create(kAppId);
    if (!single_instance.IsValid()) {
        LOG_CRITICAL("single instance mutex creation failed: {}", single_instance.Error());

        return EXIT_FAILURE;
    }

    if (single_instance.AlreadyRunning()) {
        LOG_WARN("another hyprwin instance is already running");
        MessageBoxW(nullptr, L"Another HyprWin instance is already running.", L"HyprWin", MB_OK | MB_ICONWARNING);

        return kExitAlreadyRunning;
    }

    win::SetProcessIdentity(kAppId);
    if (!win::SetPerMonitorDpiAwareness()) {
        LOG_WARN("failed to set per-monitor DPI awareness");
    }

    win::DisableProcessThrottling();

    AppState state{};
    state.settings.store(hw::DefaultSettings(), std::memory_order_release);
    const auto settings = state.settings.load(std::memory_order_acquire);

    hw::OverlayService overlay(GetModuleHandleW(nullptr), &state.latest_mouse_pos, &state.settings);
    overlay.Start();

    hw::Mouse mouse(&state.latest_mouse_pos, &state.settings, &overlay);

    std::unique_ptr<tray::Tray> sys_tray;
    try {
        sys_tray = std::make_unique<tray::Tray>(L"HyprWin", tray::Icon(IDI_APPICON));
    } catch (const std::runtime_error& error) {
        LOG_CRITICAL("tray startup failed: {}", error.what());
        return EXIT_FAILURE;
    }

    hw::Keyboard keyboard(settings->super_vk,
      hw::Keyboard::LuaServices{
        .settings = &state.settings,
        .reload_overlay_settings = [&] { overlay.MarkSettingsDirty(); },
        .request_app_exit = [&] { return sys_tray->requestExit(); },
        .request_notification =
          [&](lua::Notification notification) {
              DWORD flags = NIIF_INFO;
              if (notification.level == "warn") {
                  flags = NIIF_WARNING;
              } else if (notification.level == "error") {
                  flags = NIIF_ERROR;
              }
              return sys_tray->requestNotification(std::move(notification.title), std::move(notification.body), flags);
          },
      },
      hw::Keyboard::SuperCallbacks{
        .pressed = [&mouse] { mouse.InstallHook(); },
        .released = [&mouse] { mouse.UninstallHook(); },
      });

    sys_tray->setTooltip(L"HyprWin");
    sys_tray->DarkMode(tray::dark::AppModeForceDark);

    auto reloadBtn = sys_tray->addEntry(tray::Button(L"Reload Config", [&] { keyboard.RequestReloadConfig(); }));
    reloadBtn->setDefault(true);
    reloadBtn->setGlyphIcon(tray::Icon(IDI_APPICON));

    sys_tray
      ->addEntry(tray::Button(L"Open Config Folder",
        [] {
            const std::filesystem::path dir = win::GetModuleDirectory();
            if (dir.empty() || !win::RunProcess(dir.native(), {}, {}, false))
                LOG_ERROR("open config folder failed");
        }))
      ->setGlyphIcon(tray::Icon::FromStock(SIID_FOLDEROPEN));

    sys_tray->addEntry(tray::Separator());

    sys_tray->addEntry(tray::Button(L"Exit", [&] { keyboard.RequestExit(); }))->setGlyphIcon(tray::Icon(LoadIconW(nullptr, IDI_HAND), tray::OwnershipPolicy::Borrow));

    LOG_TRACE("tray loop starting");
    sys_tray->run();
    LOG_TRACE("tray loop stopped");

    return EXIT_SUCCESS;
}
