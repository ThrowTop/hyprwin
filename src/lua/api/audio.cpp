#include "log/log.hpp"
#include "lua/api/internal.hpp"
#include "lua/util/stack.hpp"
#include "util/strings.hpp"
#include "win/native.hpp"
#include "win/policy_config.hpp"

#include <algorithm>
#include <audioclient.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <vector>
#include <wrl/client.h>

#pragma comment(lib, "ole32.lib")

namespace lua::audio {
namespace {
    using Microsoft::WRL::ComPtr;

    constexpr char kAudioDeviceMetatable[] = "HW.AudioDevice";
    constexpr char kAudioSessionMetatable[] = "HW.AudioSession";

    struct AudioDevice {
        std::wstring id;
        std::string name;
        bool is_default = false;
        EDataFlow flow = eRender;
        ComPtr<IMMDevice> device;
        ComPtr<IAudioEndpointVolume> vol;
    };

    struct AudioSession {
        DWORD pid = 0;
        std::string process;
        std::string name;
        std::string id;
        std::string instance_id;
        std::string state;
        std::string device_id;
        ComPtr<IAudioSessionControl> control;
        ComPtr<IAudioSessionControl2> control2;
        ComPtr<ISimpleAudioVolume> vol;
    };

    bool s_policy_warned = false;

    struct ComGuard {
        bool initialized = false;
        ComGuard() {
            const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            initialized = SUCCEEDED(hr);
            if (hr == RPC_E_CHANGED_MODE)
                LOG_WARN("hw.audio: COM already initialized with different apartment model on this thread");
            else if (!initialized)
                LOG_WARN("hw.audio: CoInitializeEx failed hr={:#010x}", static_cast<unsigned>(hr));
        }
        ~ComGuard() {
            if (initialized)
                CoUninitialize();
        }
        ComGuard(const ComGuard&) = delete;
        ComGuard& operator=(const ComGuard&) = delete;
    };

    bool EnsureComInit() {
        thread_local ComGuard guard;
        return guard.initialized;
    }

    bool SetDefaultDevice(PCWSTR id) {
        ComPtr<IPolicyConfig> pc;
        const HRESULT hr = CoCreateInstance(CLSID_PolicyConfigClient, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pc));
        if (FAILED(hr)) {
            if (!s_policy_warned) {
                LOG_WARN("hw.audio: IPolicyConfig unavailable hr={:#010x}", static_cast<unsigned>(hr));
                s_policy_warned = true;
            }
            return false;
        }
        const HRESULT hr1 = pc->SetDefaultEndpoint(id, eConsole);
        const HRESULT hr2 = pc->SetDefaultEndpoint(id, eMultimedia);
        const HRESULT hr3 = pc->SetDefaultEndpoint(id, eCommunications);
        return SUCCEEDED(hr1) || SUCCEEDED(hr2) || SUCCEEDED(hr3);
    }

    ComPtr<IMMDeviceEnumerator> CreateEnumerator() {
        if (!EnsureComInit())
            return {};
        ComPtr<IMMDeviceEnumerator> enumerator;
        (void)CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
        return enumerator;
    }

    std::wstring DeviceId(IMMDevice* dev) {
        LPWSTR wid = nullptr;
        if (!dev || FAILED(dev->GetId(&wid)) || !wid)
            return {};
        std::wstring id = wid;
        CoTaskMemFree(wid);
        return id;
    }

    std::wstring DefaultEndpointId(IMMDeviceEnumerator* enumerator, EDataFlow flow) {
        ComPtr<IMMDevice> dev;
        if (FAILED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &dev)))
            return {};
        return DeviceId(dev.Get());
    }

    ComPtr<IMMDevice> GetDefaultDevice(EDataFlow flow) {
        ComPtr<IMMDeviceEnumerator> e = CreateEnumerator();
        if (!e)
            return {};
        ComPtr<IMMDevice> dev;
        if (FAILED(e->GetDefaultAudioEndpoint(flow, eConsole, &dev)))
            return {};
        return dev;
    }

    ComPtr<IAudioEndpointVolume> GetDefaultVolume(EDataFlow flow) {
        ComPtr<IMMDevice> dev = GetDefaultDevice(flow);
        if (!dev)
            return {};
        ComPtr<IAudioEndpointVolume> vol;
        dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(vol.ReleaseAndGetAddressOf()));
        return vol;
    }

    std::string TakeCoTaskString(LPWSTR value) {
        if (!value)
            return {};
        std::string out = ::util::WideToUtf8(value);
        CoTaskMemFree(value);
        return out;
    }

    const char* SessionStateName(AudioSessionState state) {
        switch (state) {
            case AudioSessionStateInactive:
                return "inactive";
            case AudioSessionStateActive:
                return "active";
            case AudioSessionStateExpired:
                return "expired";
            default:
                return "unknown";
        }
    }

    AudioDevice* TestAudioDevice(lua_State* state, int index) {
        void* ud = nullptr;
        if (lua_getmetatable(state, index)) {
            luaL_getmetatable(state, kAudioDeviceMetatable);
            const bool same = lua_rawequal(state, -1, -2) != 0;
            lua_pop(state, 2);
            if (same)
                ud = lua_touserdata(state, index);
        }
        return static_cast<AudioDevice*>(ud);
    }

    void CycleDevices(EDataFlow flow) {
        ComPtr<IMMDeviceEnumerator> e = CreateEnumerator();
        if (!e)
            return;

        ComPtr<IMMDeviceCollection> coll;
        if (FAILED(e->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &coll)))
            return;

        UINT count = 0;
        coll->GetCount(&count);
        if (count <= 1)
            return;

        std::vector<std::wstring> ids(count);
        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> dev;
            if (SUCCEEDED(coll->Item(i, &dev)))
                ids[i] = DeviceId(dev.Get());
        }

        const std::wstring curId = DefaultEndpointId(e.Get(), flow);

        UINT next = 0;
        for (UINT i = 0; i < count; ++i) {
            if (ids[i] == curId) {
                next = (i + 1) % count;
                break;
            }
        }
        if (!ids[next].empty())
            SetDefaultDevice(ids[next].c_str());
    }

    bool ReadMuteAction(lua_State* state, int valueIndex, BOOL current, BOOL& next, const char* who) {
        if (lua_isboolean(state, valueIndex)) {
            next = lua_toboolean(state, valueIndex) ? TRUE : FALSE;
            return true;
        }

        const std::string action = util::toString(state, valueIndex);
        if (action == "toggle") {
            next = current ? FALSE : TRUE;
            return true;
        }

        luaL_error(state, "%s: expected boolean, nil, or 'toggle'", who);
        return false;
    }

    int EndpointVolumeImpl(lua_State* state, IAudioEndpointVolume* vol, int valueIndex, const char* who) {
        if (lua_gettop(state) < valueIndex) {
            float v = 0.0f;
            if (!vol || FAILED(vol->GetMasterVolumeLevelScalar(&v))) {
                lua_pushnil(state);
                return 1;
            }
            lua_pushnumber(state, static_cast<lua_Number>(v));
            return 1;
        }
        if (!vol) {
            LOG_WARN("hw.audio: volume set on '{}' with no volume interface", who);
            return 0;
        }
        const float v = std::clamp(static_cast<float>(luaL_checknumber(state, valueIndex)), 0.0f, 1.0f);
        if (FAILED(vol->SetMasterVolumeLevelScalar(v, nullptr)))
            LOG_WARN("hw.audio: volume set failed on '{}'", who);
        return 0;
    }

    int EndpointVolumeDbImpl(lua_State* state, IAudioEndpointVolume* vol, int valueIndex, const char* who) {
        if (lua_gettop(state) < valueIndex) {
            float db = 0.0f;
            if (!vol || FAILED(vol->GetMasterVolumeLevel(&db))) {
                lua_pushnil(state);
                return 1;
            }
            lua_pushnumber(state, static_cast<lua_Number>(db));
            return 1;
        }
        if (!vol) {
            LOG_WARN("hw.audio: volume_db set on '{}' with no volume interface", who);
            return 0;
        }
        float lo = 0.0f, hi = 0.0f, step = 0.0f;
        vol->GetVolumeRange(&lo, &hi, &step);
        const float db = std::clamp(static_cast<float>(luaL_checknumber(state, valueIndex)), lo, hi);
        if (FAILED(vol->SetMasterVolumeLevel(db, nullptr)))
            LOG_WARN("hw.audio: volume_db set failed on '{}'", who);
        return 0;
    }

    int EndpointMuteImpl(lua_State* state, IAudioEndpointVolume* vol, int valueIndex, const char* who) {
        BOOL m = FALSE;
        if (!vol || FAILED(vol->GetMute(&m))) {
            if (lua_gettop(state) < valueIndex || lua_isnil(state, valueIndex)) {
                lua_pushnil(state);
                return 1;
            }
            LOG_WARN("hw.audio: mute set on '{}' with no volume interface", who);
            return 0;
        }

        if (lua_gettop(state) < valueIndex || lua_isnil(state, valueIndex)) {
            lua_pushboolean(state, m ? 1 : 0);
            return 1;
        }

        BOOL next = FALSE;
        if (ReadMuteAction(state, valueIndex, m, next, "hw.audio.mute")) {
            if (FAILED(vol->SetMute(next, nullptr)))
                LOG_WARN("hw.audio: mute set failed on '{}'", who);
        }
        return 0;
    }

    int SessionVolumeImpl(lua_State* state, ISimpleAudioVolume* vol, int valueIndex, const char* who) {
        if (lua_gettop(state) < valueIndex) {
            float v = 0.0f;
            if (!vol || FAILED(vol->GetMasterVolume(&v))) {
                lua_pushnil(state);
                return 1;
            }
            lua_pushnumber(state, static_cast<lua_Number>(v));
            return 1;
        }
        if (!vol) {
            LOG_WARN("hw.audio: session volume set on '{}' with no volume interface", who);
            return 0;
        }
        const float v = std::clamp(static_cast<float>(luaL_checknumber(state, valueIndex)), 0.0f, 1.0f);
        if (FAILED(vol->SetMasterVolume(v, nullptr)))
            LOG_WARN("hw.audio: session volume set failed on '{}'", who);
        return 0;
    }

    int SessionMuteImpl(lua_State* state, ISimpleAudioVolume* vol, int valueIndex, const char* who) {
        BOOL m = FALSE;
        if (!vol || FAILED(vol->GetMute(&m))) {
            if (lua_gettop(state) < valueIndex || lua_isnil(state, valueIndex)) {
                lua_pushnil(state);
                return 1;
            }
            LOG_WARN("hw.audio: session mute set on '{}' with no volume interface", who);
            return 0;
        }

        if (lua_gettop(state) < valueIndex || lua_isnil(state, valueIndex)) {
            lua_pushboolean(state, m ? 1 : 0);
            return 1;
        }

        BOOL next = FALSE;
        if (ReadMuteAction(state, valueIndex, m, next, "HW.AudioSession:mute")) {
            if (FAILED(vol->SetMute(next, nullptr)))
                LOG_WARN("hw.audio: session mute set failed on '{}'", who);
        }
        return 0;
    }

    int DevVolume(lua_State* state) {
        auto* d = static_cast<AudioDevice*>(luaL_checkudata(state, 1, kAudioDeviceMetatable));
        return EndpointVolumeImpl(state, d->vol.Get(), 2, d->name.c_str());
    }

    int DevVolumeDb(lua_State* state) {
        auto* d = static_cast<AudioDevice*>(luaL_checkudata(state, 1, kAudioDeviceMetatable));
        return EndpointVolumeDbImpl(state, d->vol.Get(), 2, d->name.c_str());
    }

    int DevVolumeRange(lua_State* state) {
        auto* d = static_cast<AudioDevice*>(luaL_checkudata(state, 1, kAudioDeviceMetatable));
        if (!d->vol) {
            lua_pushnil(state);
            return 1;
        }
        float lo = 0.0f, hi = 0.0f, step = 0.0f;
        if (FAILED(d->vol->GetVolumeRange(&lo, &hi, &step))) {
            lua_pushnil(state);
            return 1;
        }
        lua_newtable(state);
        lua_pushnumber(state, static_cast<lua_Number>(lo));
        lua_setfield(state, -2, "min");
        lua_pushnumber(state, static_cast<lua_Number>(hi));
        lua_setfield(state, -2, "max");
        lua_pushnumber(state, static_cast<lua_Number>(step));
        lua_setfield(state, -2, "step");
        return 1;
    }

    int DevMute(lua_State* state) {
        auto* d = static_cast<AudioDevice*>(luaL_checkudata(state, 1, kAudioDeviceMetatable));
        return EndpointMuteImpl(state, d->vol.Get(), 2, d->name.c_str());
    }

    int DevSetDefault(lua_State* state) {
        auto* d = static_cast<AudioDevice*>(luaL_checkudata(state, 1, kAudioDeviceMetatable));
        lua_pushboolean(state, SetDefaultDevice(d->id.c_str()) ? 1 : 0);
        return 1;
    }

    int AudioDeviceIndex(lua_State* state) {
        auto* d = static_cast<AudioDevice*>(luaL_checkudata(state, 1, kAudioDeviceMetatable));
        const std::string key = util::toString(state, 2);

        if (key == "id") {
            const std::string utf8 = ::util::WideToUtf8(d->id);
            lua_pushlstring(state, utf8.data(), utf8.size());
            return 1;
        }
        if (key == "name") {
            lua_pushlstring(state, d->name.data(), d->name.size());
            return 1;
        }
        if (key == "default") {
            lua_pushboolean(state, d->is_default ? 1 : 0);
            return 1;
        }

        if (key == "volume") {
            lua_pushcfunction(state, DevVolume);
            return 1;
        }
        if (key == "volume_db") {
            lua_pushcfunction(state, DevVolumeDb);
            return 1;
        }
        if (key == "volume_range") {
            lua_pushcfunction(state, DevVolumeRange);
            return 1;
        }
        if (key == "mute") {
            lua_pushcfunction(state, DevMute);
            return 1;
        }
        if (key == "set_default") {
            lua_pushcfunction(state, DevSetDefault);
            return 1;
        }

        lua_pushnil(state);
        return 1;
    }

    int AudioDeviceGc(lua_State* state) {
        auto* d = static_cast<AudioDevice*>(luaL_checkudata(state, 1, kAudioDeviceMetatable));
        d->~AudioDevice();
        return 0;
    }

    int AudioDeviceToString(lua_State* state) {
        auto* d = static_cast<AudioDevice*>(luaL_checkudata(state, 1, kAudioDeviceMetatable));
        lua_pushfstring(state, "HW.AudioDevice(\"%s\")", d->name.c_str());
        return 1;
    }

    void EnsureAudioDeviceMetatable(lua_State* state) {
        util::ensureMetatable(state, kAudioDeviceMetatable, [](lua_State* s) {
            util::setFn(s, "__index", AudioDeviceIndex);
            util::setFn(s, "__gc", AudioDeviceGc);
            util::setFn(s, "__tostring", AudioDeviceToString);
        });
    }

    void PushAudioDevice(lua_State* state, IMMDevice* dev, bool is_default, EDataFlow flow) {
        EnsureAudioDeviceMetatable(state);

        auto* ud = static_cast<AudioDevice*>(lua_newuserdata(state, sizeof(AudioDevice)));
        new (ud) AudioDevice{};
        luaL_getmetatable(state, kAudioDeviceMetatable);
        lua_setmetatable(state, -2);

        ud->id = DeviceId(dev);
        ud->is_default = is_default;
        ud->flow = flow;
        ud->device = dev;

        ComPtr<IPropertyStore> store;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &store))) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) {
                ud->name = ::util::WideToUtf8(pv.pwszVal);
            }
            PropVariantClear(&pv);
        }

        dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(ud->vol.ReleaseAndGetAddressOf()));
    }

    int SessionVolume(lua_State* state) {
        auto* s = static_cast<AudioSession*>(luaL_checkudata(state, 1, kAudioSessionMetatable));
        return SessionVolumeImpl(state, s->vol.Get(), 2, s->name.c_str());
    }

    int SessionMute(lua_State* state) {
        auto* s = static_cast<AudioSession*>(luaL_checkudata(state, 1, kAudioSessionMetatable));
        return SessionMuteImpl(state, s->vol.Get(), 2, s->name.c_str());
    }

    int AudioSessionIndex(lua_State* state) {
        auto* s = static_cast<AudioSession*>(luaL_checkudata(state, 1, kAudioSessionMetatable));
        const std::string key = util::toString(state, 2);

        if (key == "pid") {
            if (s->pid == 0)
                lua_pushnil(state);
            else
                lua_pushinteger(state, static_cast<lua_Integer>(s->pid));
            return 1;
        }
        if (key == "process") {
            lua_pushlstring(state, s->process.data(), s->process.size());
            return 1;
        }
        if (key == "name") {
            lua_pushlstring(state, s->name.data(), s->name.size());
            return 1;
        }
        if (key == "id") {
            lua_pushlstring(state, s->id.data(), s->id.size());
            return 1;
        }
        if (key == "instance_id") {
            lua_pushlstring(state, s->instance_id.data(), s->instance_id.size());
            return 1;
        }
        if (key == "state") {
            lua_pushlstring(state, s->state.data(), s->state.size());
            return 1;
        }
        if (key == "device_id") {
            lua_pushlstring(state, s->device_id.data(), s->device_id.size());
            return 1;
        }
        if (key == "volume") {
            lua_pushcfunction(state, SessionVolume);
            return 1;
        }
        if (key == "mute") {
            lua_pushcfunction(state, SessionMute);
            return 1;
        }

        lua_pushnil(state);
        return 1;
    }

    int AudioSessionGc(lua_State* state) {
        auto* s = static_cast<AudioSession*>(luaL_checkudata(state, 1, kAudioSessionMetatable));
        s->~AudioSession();
        return 0;
    }

    int AudioSessionToString(lua_State* state) {
        auto* s = static_cast<AudioSession*>(luaL_checkudata(state, 1, kAudioSessionMetatable));
        lua_pushfstring(state, "HW.AudioSession(\"%s\")", s->name.c_str());
        return 1;
    }

    void EnsureAudioSessionMetatable(lua_State* state) {
        util::ensureMetatable(state, kAudioSessionMetatable, [](lua_State* s) {
            util::setFn(s, "__index", AudioSessionIndex);
            util::setFn(s, "__gc", AudioSessionGc);
            util::setFn(s, "__tostring", AudioSessionToString);
        });
    }

    void PushAudioSession(lua_State* state, IAudioSessionControl* control, ISimpleAudioVolume* vol, const std::wstring& device_id) {
        EnsureAudioSessionMetatable(state);

        auto* ud = static_cast<AudioSession*>(lua_newuserdata(state, sizeof(AudioSession)));
        new (ud) AudioSession{};
        luaL_getmetatable(state, kAudioSessionMetatable);
        lua_setmetatable(state, -2);

        ud->control = control;
        ud->vol = vol;
        ud->device_id = ::util::WideToUtf8(device_id);
        (void)control->QueryInterface(IID_PPV_ARGS(ud->control2.ReleaseAndGetAddressOf()));

        AudioSessionState sessionState{};
        if (SUCCEEDED(control->GetState(&sessionState)))
            ud->state = SessionStateName(sessionState);
        else
            ud->state = "unknown";

        LPWSTR value = nullptr;
        if (SUCCEEDED(control->GetDisplayName(&value)))
            ud->name = TakeCoTaskString(value);

        if (ud->control2) {
            (void)ud->control2->GetProcessId(&ud->pid);

            value = nullptr;
            if (SUCCEEDED(ud->control2->GetSessionIdentifier(&value)))
                ud->id = TakeCoTaskString(value);

            value = nullptr;
            if (SUCCEEDED(ud->control2->GetSessionInstanceIdentifier(&value)))
                ud->instance_id = TakeCoTaskString(value);
        }

        if (ud->pid != 0)
            ud->process = ::util::WideToUtf8(win::GetProcessNameByPid(ud->pid));
        if (ud->name.empty())
            ud->name = ud->process;
    }

    int DefaultDevice(lua_State* state, EDataFlow flow) {
        ComPtr<IMMDevice> device = GetDefaultDevice(flow);
        if (!device) {
            lua_pushnil(state);
            return 1;
        }

        PushAudioDevice(state, device.Get(), true, flow);
        return 1;
    }

    int EnumDevices(lua_State* state, EDataFlow flow) {
        lua_newtable(state);
        ComPtr<IMMDeviceEnumerator> e = CreateEnumerator();
        if (!e)
            return 1;

        const std::wstring defId = DefaultEndpointId(e.Get(), flow);

        ComPtr<IMMDeviceCollection> coll;
        if (FAILED(e->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &coll)))
            return 1;

        UINT count = 0;
        coll->GetCount(&count);

        int luaIdx = 1;
        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> dev;
            if (FAILED(coll->Item(i, &dev)))
                continue;

            const std::wstring id = DeviceId(dev.Get());
            PushAudioDevice(state, dev.Get(), !id.empty() && id == defId, flow);
            lua_rawseti(state, -2, luaIdx++);
        }
        return 1;
    }

    int AudioSessions(lua_State* state) {
        const int argc = lua_gettop(state);

        ComPtr<IMMDevice> device;
        if (argc == 0 || lua_isnil(state, 1)) {
            device = GetDefaultDevice(eRender);
        } else {
            AudioDevice* d = TestAudioDevice(state, 1);
            if (!d)
                return luaL_argerror(state, 1, "HW.AudioDevice expected");
            if (d->flow != eRender)
                return luaL_argerror(state, 1, "playback device expected");
            device = d->device;
        }

        lua_newtable(state);

        if (!device) {
            LOG_WARN("hw.audio.sessions: no playback device available");
            return 1;
        }

        const std::wstring deviceId = DeviceId(device.Get());

        ComPtr<IAudioSessionManager2> manager;
        HRESULT hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(manager.ReleaseAndGetAddressOf()));
        if (FAILED(hr)) {
            LOG_WARN("hw.audio.sessions: IAudioSessionManager2 activation failed hr={:#010x}", static_cast<unsigned>(hr));
            return 1;
        }

        ComPtr<IAudioSessionEnumerator> sessions;
        hr = manager->GetSessionEnumerator(&sessions);
        if (FAILED(hr)) {
            LOG_WARN("hw.audio.sessions: GetSessionEnumerator failed hr={:#010x}", static_cast<unsigned>(hr));
            return 1;
        }

        int count = 0;
        hr = sessions->GetCount(&count);
        if (FAILED(hr)) {
            LOG_WARN("hw.audio.sessions: GetCount failed hr={:#010x}", static_cast<unsigned>(hr));
            return 1;
        }

        int luaIdx = 1;
        for (int i = 0; i < count; ++i) {
            ComPtr<IAudioSessionControl> control;
            hr = sessions->GetSession(i, &control);
            if (FAILED(hr) || !control) {
                LOG_WARN("hw.audio.sessions: GetSession({}) failed hr={:#010x}", i, static_cast<unsigned>(hr));
                continue;
            }

            ComPtr<ISimpleAudioVolume> vol;
            hr = control.As(&vol);
            if (FAILED(hr) || !vol) {
                LOG_WARN("hw.audio.sessions: ISimpleAudioVolume query failed for session {} hr={:#010x}", i, static_cast<unsigned>(hr));
                continue;
            }

            PushAudioSession(state, control.Get(), vol.Get(), deviceId);
            lua_rawseti(state, -2, luaIdx++);
        }
        return 1;
    }

    int AudioVolume(lua_State* state) {
        return EndpointVolumeImpl(state, GetDefaultVolume(eRender).Get(), 1, "default");
    }

    int AudioVolumeDb(lua_State* state) {
        return EndpointVolumeDbImpl(state, GetDefaultVolume(eRender).Get(), 1, "default");
    }

    int AudioMute(lua_State* state) {
        return EndpointMuteImpl(state, GetDefaultVolume(eRender).Get(), 1, "default");
    }

    int AudioCycle(lua_State*) {
        CycleDevices(eRender);
        return 0;
    }
    int AudioCycleCapture(lua_State*) {
        CycleDevices(eCapture);
        return 0;
    }
    int AudioPlaybackDefault(lua_State* state) {
        return DefaultDevice(state, eRender);
    }
    int AudioCaptureDefault(lua_State* state) {
        return DefaultDevice(state, eCapture);
    }
    int AudioPlaybackDevices(lua_State* state) {
        return EnumDevices(state, eRender);
    }
    int AudioCaptureDevices(lua_State* state) {
        return EnumDevices(state, eCapture);
    }
} // namespace

void registerApi(lua_State* state) {
    EnsureAudioDeviceMetatable(state);
    EnsureAudioSessionMetatable(state);

    lua_newtable(state);
    util::setFn(state, "volume", AudioVolume);
    util::setFn(state, "volume_db", AudioVolumeDb);
    util::setFn(state, "mute", AudioMute);
    util::setFn(state, "cycle", AudioCycle);
    util::setFn(state, "cycle_capture", AudioCycleCapture);
    util::setFn(state, "playback_default", AudioPlaybackDefault);
    util::setFn(state, "capture_default", AudioCaptureDefault);
    util::setFn(state, "playback_devices", AudioPlaybackDevices);
    util::setFn(state, "capture_devices", AudioCaptureDevices);
    util::setFn(state, "sessions", AudioSessions);
    lua_setfield(state, -2, "audio");
}
} // namespace lua::audio
