---@meta

-- =============================================================
-- point
-- =============================================================

---@class point
---@field x integer
---@field y integer
---@operator add(point): point
---@operator sub(point): point
---@operator mul(number): point
---@operator div(number): point
---@operator unm(): point
---@field offset   fun(self: point, dx: integer, dy: integer): point
---@field length   fun(self: point): number
---@field distance fun(self: point, o: point): number
---@field clamp    fun(self: point, lo: point, hi: point): point

---@overload fun(x: integer, y: integer): point
point = {}

-- =============================================================
-- rect
-- =============================================================

---@class rect
---@field x integer
---@field y integer
---@field x2 integer
---@field y2 integer
---@field w integer Computed as x2 - x; assigning to this field raises an error.
---@field h integer Computed as y2 - y; assigning to this field raises an error.
---@operator add(rect): rect
---@operator sub(rect): rect
---@operator mul(number): rect
---@operator div(number): rect
---@field offset     fun(self: rect, dx: integer, dy: integer): rect
---@field inset      fun(self: rect, x: integer, y?: integer): rect
---@field grow       fun(self: rect, n: integer): rect
---@field pos        fun(self: rect): point
---@field size       fun(self: rect): point
---@field center     fun(self: rect): point
---@field with_pos   fun(self: rect, x: integer, y: integer): rect
---@field with_size  fun(self: rect, w: integer, h: integer): rect
---@field aspect     fun(self: rect): number
---@field is_empty   fun(self: rect): boolean
---@field contains   fun(self: rect, p: point): boolean
---@field intersects   fun(self: rect, o: rect): boolean
---@field intersection fun(self: rect, o: rect): rect?
---@field union      fun(self: rect, o: rect): rect
---@field clamp      fun(self: rect, b: rect): rect
---@field center_in  fun(self: rect, b: rect): rect

---@overload fun(x: integer, y: integer, x2: integer, y2: integer): rect
rect = {}

---@param x integer
---@param y integer
---@param w integer
---@param h integer
---@return rect
function rect.xywh(x, y, w, h) end

-- =============================================================
-- math extensions
-- =============================================================

---@param v number
---@param lo number
---@param hi number
---@return number
function math.clamp(v, lo, hi) end

---@param v number
---@return integer
function math.round(v) end

---@param a number
---@param b number
---@param t number
---@return number
function math.lerp(a, b, t) end

---@param x number
---@return -1|0|1
function math.sign(x) end

-- =============================================================
-- string extensions
-- =============================================================

---@param s string
---@param prefix string
---@return boolean
function string.starts_with(s, prefix) end

---@param s string
---@param suffix string
---@return boolean
function string.ends_with(s, suffix) end

---@param s string
---@param patterns string[]
---@return boolean
function string.matches_any(s, patterns) end

-- =============================================================
-- table extensions
-- =============================================================

---@generic K, V
---@param a table<K, V>
---@param b table<K, V>
---@return table<K, V>
function table.merge(a, b) end

---@generic T, U
---@param t T[]
---@param fn fun(v: T): U
---@return U[]
function table.map(t, fn) end

---@generic T
---@param t T[]
---@param fn fun(v: T): boolean
---@return T[]
function table.filter(t, fn) end

---@param value any
---@param opts? { max_depth?: integer }
---@return string
function table.inspect(value, opts) end

---@param ... any
function _G.pprint(...) end

-- =============================================================
-- HW namespace (userdata class container)
-- =============================================================

HW = {}

-- =============================================================
-- HW.Color
-- =============================================================

---@class HW.Color
---@field r integer  0-255 byte value
---@field g integer  0-255 byte value
---@field b integer  0-255 byte value
---@field a integer  0-255 byte value
---@field hex        fun(self: HW.Color): string
---@field with_alpha fun(self: HW.Color, a: integer): HW.Color
---@field with_alphaf fun(self: HW.Color, a: number): HW.Color
HW.Color = {}

-- color constructor table (global)
color = {}

---@param hex string  6-digit hex string, with or without leading #
---@param alpha? integer  0-255 byte value
---@return HW.Color
function color.hex(hex, alpha) end

---@param r integer  0-255 byte value
---@param g integer  0-255 byte value
---@param b integer  0-255 byte value
---@return HW.Color
function color.rgb(r, g, b) end

---@param r integer  0-255 byte value
---@param g integer  0-255 byte value
---@param b integer  0-255 byte value
---@param a integer  0-255 byte value
---@return HW.Color
function color.rgba(r, g, b, a) end

---@param r number  0.0-1.0 normalized value
---@param g number  0.0-1.0 normalized value
---@param b number  0.0-1.0 normalized value
---@return HW.Color
function color.rgbf(r, g, b) end

---@param r number  0.0-1.0 normalized value
---@param g number  0.0-1.0 normalized value
---@param b number  0.0-1.0 normalized value
---@param a number  0.0-1.0 normalized value
---@return HW.Color
function color.rgbaf(r, g, b, a) end

-- =============================================================
-- HW.Window
-- =============================================================

---@class HW.Window
HW.Window = {}

---@return string
function HW.Window:title() end

---@return string
function HW.Window:class() end

---@return string
function HW.Window:pname() end

---@return integer?
function HW.Window:pid() end

---@return rect?
function HW.Window:visual_rect() end

---@param r rect
---@return boolean
function HW.Window:set_visual_rect(r) end

---@return boolean
function HW.Window:get_maximized() end

---@return boolean
function HW.Window:get_minimized() end

---@return boolean
function HW.Window:get_fullscreen() end

---@return boolean
function HW.Window:get_resizable() end

---@return boolean
function HW.Window:maximize() end

---@return boolean
function HW.Window:minimize() end

---@return boolean
function HW.Window:restore() end

---@return boolean
function HW.Window:close() end

---@return boolean
function HW.Window:kill() end

---@param ms? integer
---@return boolean
function HW.Window:responsive(ms) end

function HW.Window:focus() end

---@class hw_window_dump
---@field hwnd integer
---@field process string
---@field title string
---@field class string
---@field pid? integer
---@field raw_rect? rect
---@field visual_rect? rect
---@field maximized boolean
---@field minimized boolean
---@field fullscreen boolean
---@field resizable boolean

---@return hw_window_dump
function HW.Window:dump() end

-- =============================================================
-- HW.MonitorMode
-- =============================================================

---@class HW.MonitorMode
---@field width integer
---@field height integer
---@field hz integer

-- =============================================================
-- HW.Monitor
-- =============================================================

---@class HW.Monitor
---@field rect rect
---@field work_area rect
---@field width integer
---@field height integer
---@field is_primary boolean
---@field name string
---@field dpi point
---@field scale number
---@field hz integer
---@field bit_depth integer
HW.Monitor = {}

---@param w integer
---@param h integer
---@param hz integer
---@return boolean
function HW.Monitor:set_resolution(w, h, hz) end

---@return HW.MonitorMode[]
function HW.Monitor:list_modes() end

---@param w HW.Window
---@return boolean
function HW.Monitor:move_window(w) end

-- =============================================================
-- HW.VolumeRange
-- =============================================================

---@class HW.VolumeRange
---@field min number
---@field max number
---@field step number

-- =============================================================
-- HW.AudioDevice
-- =============================================================

---@class HW.AudioDevice
---@field id string
---@field name string
---@field default boolean
HW.AudioDevice = {}

---@return number?
---@overload fun(self: HW.AudioDevice, v: number)
function HW.AudioDevice:volume(v) end

---@return number?
---@overload fun(self: HW.AudioDevice, db: number)
function HW.AudioDevice:volume_db(db) end

---@return HW.VolumeRange?
function HW.AudioDevice:volume_range() end

---@return boolean?
---@overload fun(self: HW.AudioDevice, muted: boolean|"toggle")
function HW.AudioDevice:mute(muted) end

---@return boolean
function HW.AudioDevice:set_default() end

-- =============================================================
-- HW.AudioSession
-- =============================================================

---@class HW.AudioSession
---@field id string
---@field instance_id string
---@field device_id string
---@field pid integer?
---@field process string
---@field name string
---@field state "active"|"inactive"|"expired"|"unknown"
HW.AudioSession = {}

---@return number?
---@overload fun(self: HW.AudioSession, v: number)
function HW.AudioSession:volume(v) end

---@return boolean?
---@overload fun(self: HW.AudioSession, muted: boolean|"toggle")
function HW.AudioSession:mute(muted) end

-- =============================================================
-- HW.Timer
-- =============================================================

---@class HW.Timer
HW.Timer = {}

function HW.Timer:cancel() end
function HW.Timer:call() end

-- =============================================================
-- Option/param types
-- =============================================================

---@class hw_notify_opts
---@field body string
---@field title? string
---@field level? "info"|"warn"|"error"

---@class hw_timer_opts
---@field repeating? boolean

---@alias hw_fs_type '"file"'|'"directory"'|'"symlink"'|'"other"'

---@class hw_fs_entry
---@field name string
---@field path string
---@field type hw_fs_type
---@field ext string
---@field size? integer

-- =============================================================
-- hw.settings types  (all fields optional -- defaults apply)
-- =============================================================

---@class hw_settings_debug
---@field trace_binds? boolean
---@field bench_binds? boolean
---@field trace_grabs? boolean
---@field trace_super? boolean
---@field trace_timeout? boolean
---@field last_config_load_ms? integer

---@class hw_grab_filter
---@field action "include"|"exclude"
---@field process? string
---@field class? string

---@class hw_settings
---@field super? "LWIN"|"RWIN"
---@field border? number
---@field colors? HW.Color[]
---@field shader? string|nil
---@field gradient_angle? number
---@field rotating? boolean
---@field rotation_speed? number
---@field corner_radius? number
---@field outer_alpha? number
---@field glow_falloff? number
---@field resize_corner? "closest"|"topleft"|"topright"|"bottomleft"|"bottomright"
---@field grab_filters? hw_grab_filter[]
---@field debug? hw_settings_debug

---@class hw_debug_shader_compiler_status
---@field available boolean
---@field diagnostics string

---@class hw_build
---@field VERSION string
---@field DEBUG boolean
---@field ARCH string
---@field LUAJIT_VERSION string
---@field LUAJIT_MODE string

-- =============================================================
-- hw global
-- =============================================================

hw = {}

---@type hw_settings
hw.settings = {}

hw.build = {} --[[@as hw_build]]

-- hw.window --------------------------------------------------

hw.window = {}

---@return HW.Window?
function hw.window.at_cursor() end

---@return HW.Window?
function hw.window.focused() end

---@param fn? fun(w: HW.Window): boolean
---@return HW.Window[]
function hw.window.list(fn) end

-- hw.mon -----------------------------------------------------

hw.mon = {}

---@return HW.Monitor
function hw.mon.primary() end

---@return HW.Monitor[]
function hw.mon.list() end

---@param p point
---@return HW.Monitor
function hw.mon.at(p) end

---@param w HW.Window
---@return HW.Monitor
function hw.mon.for_window(w) end

---@param w? HW.Window
---@return rect?
function hw.mon.work_area(w) end

-- hw.mouse ---------------------------------------------------

hw.mouse = {}

---@return point
function hw.mouse.pos() end

---@param p point
function hw.mouse.move(p) end

---@param btn "left"|"right"|"middle"
---@param p? point
function hw.mouse.click(btn, p) end

---@param btn "left"|"right"|"middle"
function hw.mouse.down(btn) end

---@param btn "left"|"right"|"middle"
function hw.mouse.up(btn) end

---@param delta integer
function hw.mouse.scroll(delta) end

-- hw.clipboard -----------------------------------------------

hw.clipboard = {}

---@return string?
function hw.clipboard.get() end

---@param text string
function hw.clipboard.set(text) end

---@return string[]?
function hw.clipboard.get_files() end

-- hw.input ---------------------------------------------------

hw.input = {}

---@param combo string
---@return boolean
function hw.input.send(combo) end

---@param text string
function hw.input.send_text(text) end

---@param key string
---@return boolean
function hw.input.is_down(key) end

---@param key string
---@return boolean
function hw.input.get_toggle(key) end

---@param key string
function hw.input.toggle(key) end

---@return string?
function hw.input.lang() end

---@return string[]
function hw.input.lang_list() end

---@param name string
function hw.input.set_lang(name) end

function hw.input.next_lang() end

-- hw.audio ---------------------------------------------------

hw.audio = {}

---@return number?
---@overload fun(v: number)
function hw.audio.volume(v) end

---@return number?
---@overload fun(db: number)
function hw.audio.volume_db(db) end

---@return boolean?
---@overload fun(b: boolean|"toggle")
function hw.audio.mute(b) end

function hw.audio.cycle() end
function hw.audio.cycle_capture() end

---@return HW.AudioDevice?
function hw.audio.playback_default() end

---@return HW.AudioDevice?
function hw.audio.capture_default() end

---@return HW.AudioDevice[]
function hw.audio.playback_devices() end

---@return HW.AudioDevice[]
function hw.audio.capture_devices() end

---@param device? HW.AudioDevice
---@return HW.AudioSession[]
function hw.audio.sessions(device) end

-- hw.sys -----------------------------------------------------

hw.sys = {}

---@return boolean
---@overload fun(enabled: boolean): boolean
---@overload fun(action: "toggle"): boolean
function hw.sys.debug_console(arg) end

---@return boolean
function hw.sys.lock() end

---@param name string
---@return string?
function hw.sys.env(name) end

---@return string?
function hw.sys.username() end

---@param hwnd integer
---@param msg integer
---@param wparam? integer
---@param lparam? integer
function hw.sys.send_notify_message(hwnd, msg, wparam, lparam) end

---@param key string
---@param name? string
---@return string|integer|nil
function hw.sys.reg_get(key, name) end

---@param key string
---@param name string
---@param value string|integer
---@return boolean
function hw.sys.reg_set(key, name, value) end

---@param key string
---@param name? string
---@return boolean
function hw.sys.reg_exists(key, name) end

---@param key string
---@param name? string
---@return boolean
function hw.sys.reg_delete(key, name) end

-- hw.fs ------------------------------------------------------

---@class hw_fs
hw.fs = {} --[[@as hw_fs]]

---@param path string
---@return boolean
function hw.fs.exists(path) end

---@param path string
---@return boolean
function hw.fs.is_file(path) end

---@param path string
---@return boolean
function hw.fs.is_dir(path) end

---@param path string
---@return hw_fs_entry[]? entries
---@return string? err
function hw.fs.list(path) end

---@param path string
---@return string? data
---@return string? err
function hw.fs.read(path) end

---@param path string
---@param text string
---@return boolean ok
---@return string? err
function hw.fs.write(path, text) end

---@param path string
---@return boolean ok
---@return string? err
function hw.fs.mkdir(path) end

-- hw.debug ---------------------------------------------------

hw.debug = {}

---@param enabled boolean
function hw.debug.jit(enabled) end

---@return hw_debug_shader_compiler_status
function hw.debug.shader_compiler_status() end

-- core hw functions ------------------------------------------

---@param key string
---@param fn fun()
function hw.bind(key, fn) end

---@param fn fun(pressed: boolean)
function hw.on_super(fn) end

---@param level "trace"|"debug"|"info"|"warn"|"error"|"critical"
---@param message string
function hw.log(level, message) end

---@return string
function hw.log_path() end

---@return boolean
function hw.open_log() end

---@param message string
---@param title? string
function hw.msgbox(message, title) end

---@param opts hw_notify_opts
---@return boolean
function hw.notify(opts) end

---@param path string
---@param args? string
---@param admin? boolean
function hw.run(path, args, admin) end

---@class hw_run_opts
---@field path? string
---@field file? string
---@field target? string
---@field args? string
---@field cwd? string
---@field dir? string
---@field admin? boolean

---@param opts hw_run_opts
function hw.run(opts) end

---@class hw_launch_opts: hw_run_opts

---@param path string
---@param args? string
---@param admin? boolean
function hw.launch(path, args, admin) end

---@param opts hw_launch_opts
function hw.launch(opts) end

---@return boolean
function hw.reload() end

---@return boolean
function hw.quit() end

---@param fn fun()
function hw.on_reload(fn) end

---@param fn fun()
function hw.on_exit(fn) end

---@return string[]
function hw.binds() end

---@return string
function hw.config_path() end

---@return boolean
function hw.open_config() end

---@param ms integer
---@param fn fun()
---@param opts? hw_timer_opts
---@return HW.Timer
function hw.timer(ms, fn, opts) end
