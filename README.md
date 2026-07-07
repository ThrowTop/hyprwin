# HyprWin

**Modal Windows automation with zero idle CPU.**

HyprWin is a fast native Windows automation utility with a modal Windows-key
workflow. It provides mouse-driven window moving and resizing, DirectX border
previews, Lua keybinds, and an extensive Lua API for controlling Windows.

> Not related to hyprwin.cloud.

## Features

- Lua-configured modal keybinds and automation
- `SUPER + left mouse` window dragging
- `SUPER + right mouse` window resizing
- DirectX 11 border previews
- Built-in and custom HLSL border shaders
- APIs for windows, monitors, input, audio, clipboard, notifications, and more
- Tray-based configuration reload and shutdown
- No polling or periodic work while idle

## Requirements

- Windows 11 x64
- CMake 4.3 or newer
- Ninja
- Visual Studio 2022 or newer with the Desktop development with C++ workload

Run builds from an x64 Visual Studio developer environment.

## Build

Clone the repository and its LuaJIT submodule:

```powershell
git clone --recurse-submodules https://github.com/ThrowTop/hyprwin.git
cd hyprwin
```

Configure and build:

```powershell
cmake --workflow --preset debug
```

Other configurations:

```powershell
cmake --workflow --preset release
cmake --workflow --preset perf
```

Build a versioned release archive:

```powershell
cmake --build --preset release --target hyprwin_package
```

Outputs are written to `build/output/`:

| Configuration | Executable |
| --- | --- |
| Debug | `hyprwin-debug.exe` |
| Release | `hyprwin.exe` |
| Perf | `hyprwin-perf.exe` |

## Configuration

HyprWin loads `hyprwin.lua` beside the executable. If it is missing, the
default configuration is generated on first launch.

```lua
hw.settings = {
    border = 3,
    rotating = true,
    rotation_speed = 120,
}

hw.bind("Q", function()
    local window = hw.window.at_cursor()
    if window then
        window:close()
    end
end)
```

The full Lua API, shader API, examples, and troubleshooting guides are on the
[HyprWin documentation website](https://throwtop.dev/hyprwin-docs/).

## Previous version

The [original HyprWin implementation](https://github.com/ThrowTop/hyprwin-v1)
is preserved separately.

## License

HyprWin is available under the [MIT License](LICENSE).
