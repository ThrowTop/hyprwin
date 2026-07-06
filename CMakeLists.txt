cmake_minimum_required(VERSION 4.3)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" HYPRWIN_VERSION)
string(STRIP "${HYPRWIN_VERSION}" HYPRWIN_VERSION)
if(NOT HYPRWIN_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "version.txt must contain only a version like 2.4.0")
endif()

project(hyprwin VERSION "${HYPRWIN_VERSION}" LANGUAGES CXX RC)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerOptions)
include(D3DCompilerRuntime)
include(LuaJIT)
include(LuaStdlib)
include(Shaders)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "hyprwin is x64-only.")
endif()
if(NOT MSVC)
  message(FATAL_ERROR "hyprwin requires an MSVC-compatible Windows toolchain: cl.exe or clang-cl.exe in a VS x64 environment.")
endif()
if(MSVC AND CMAKE_CXX_COMPILER_ARCHITECTURE_ID AND
   NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
  message(FATAL_ERROR "hyprwin requires MSVC x64, got ${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}.")
endif()

set(_hyprwin_default_log_level 0)
string(TOLOWER "${CMAKE_BUILD_TYPE}" _hyprwin_build_type)
if(_hyprwin_build_type STREQUAL "release")
  set(_hyprwin_default_log_level 2)
endif()

set(HYPRWIN_LOG_ACTIVE_LEVEL "${_hyprwin_default_log_level}" CACHE STRING
    "Compile-time log level: 0=TRACE 1=DEBUG 2=INFO 3=WARN 4=ERROR 5=CRITICAL 6=OFF")
set_property(CACHE HYPRWIN_LOG_ACTIVE_LEVEL PROPERTY STRINGS 0 1 2 3 4 5 6)

option(HYPRWIN_ENABLE_PERF "Enable optimized-build performance counters" OFF)
if(HYPRWIN_ENABLE_PERF AND _hyprwin_build_type STREQUAL "debug")
  message(FATAL_ERROR "HYPRWIN_ENABLE_PERF requires an optimized build; Debug benchmark numbers are not useful.")
endif()

string(TOLOWER "${CMAKE_BUILD_TYPE}" _cfg)
set(_build_tag "")
if(HYPRWIN_ENABLE_PERF)
  set(_build_tag "-perf")
elseif(_cfg STREQUAL "debug")
  set(_build_tag "-debug")
endif()
set(HYPRWIN_OUTPUT_NAME "hyprwin${_build_tag}")
set(HYPRWIN_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/output")

file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/res/hw.ico" HYPRWIN_ICO_PATH)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/hyprwin.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/hyprwin.rc"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/version.hpp"
    @ONLY
)

file(MAKE_DIRECTORY "${HYPRWIN_RUNTIME_DIR}")

file(GLOB_RECURSE HYPRWIN_SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.hpp" "src/*.h")

add_executable(hyprwin
    ${HYPRWIN_SOURCES}
    "${CMAKE_CURRENT_BINARY_DIR}/hyprwin.rc"
)

set_target_properties(hyprwin PROPERTIES
    OUTPUT_NAME              "${HYPRWIN_OUTPUT_NAME}"
    RUNTIME_OUTPUT_DIRECTORY "${HYPRWIN_RUNTIME_DIR}"
    PDB_OUTPUT_DIRECTORY     "${CMAKE_CURRENT_BINARY_DIR}/symbols"
    WIN32_EXECUTABLE         TRUE
)

target_include_directories(hyprwin PRIVATE
    src
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_precompile_headers(hyprwin PRIVATE src/pch.hpp)

target_link_libraries(hyprwin PRIVATE
    user32
    userenv
    avrt
    shell32
    ole32
    oleaut32
    advapi32
    gdi32
    dwmapi
    dxgi
    d3d11
    dcomp
    shcore
    propsys
    comctl32
    uxtheme
    windowsapp
)

target_compile_definitions(hyprwin PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    LOG_ACTIVE_LEVEL=${HYPRWIN_LOG_ACTIVE_LEVEL}
    HYPRWIN_ENABLE_PERF=$<BOOL:${HYPRWIN_ENABLE_PERF}>
    $<$<CONFIG:Release>:HYPRWIN_RELEASE=1>
)

hyprwin_apply_compiler_options(hyprwin)
hyprwin_add_luajit(hyprwin)
hyprwin_add_shaders(hyprwin)
hyprwin_add_lua_stdlib(hyprwin)
hyprwin_copy_d3dcompiler_runtime(hyprwin)

if(_cfg STREQUAL "release" AND NOT HYPRWIN_ENABLE_PERF)
  add_custom_target(hyprwin_package
    COMMAND "${CMAKE_COMMAND}"
      "-DVERSION=${PROJECT_VERSION}"
      "-DEXE=$<TARGET_FILE:hyprwin>"
      "-DRUNTIME_DIR=${HYPRWIN_RUNTIME_DIR}"
      "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
      "-DOUTPUT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/build/packages"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Package.cmake"
    DEPENDS hyprwin
    VERBATIM
    COMMENT "Packaging HyprWin ${PROJECT_VERSION}"
  )
endif()
