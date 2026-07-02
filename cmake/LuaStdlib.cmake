include_guard(GLOBAL)

function(hyprwin_add_lua_stdlib target)
  if(NOT HYPRWIN_LUAJIT_EXE OR NOT HYPRWIN_LUAJIT_STAMP OR NOT HYPRWIN_LUAJIT_LUA_PATH)
    message(FATAL_ERROR "hyprwin_add_lua_stdlib requires hyprwin_add_luajit to run first")
  endif()

  set(_stdlib_src  "${PROJECT_SOURCE_DIR}/src/lua/stdlib.lua")
  set(_stdlib_bc_h "${PROJECT_BINARY_DIR}/lua_stdlib_bc.h")

  add_custom_command(
    OUTPUT  "${_stdlib_bc_h}"
    COMMAND "${CMAKE_COMMAND}" -E env "LUA_PATH=${HYPRWIN_LUAJIT_LUA_PATH};;"
            "${HYPRWIN_LUAJIT_EXE}" -b -t h -n stdlib "${_stdlib_src}" "${_stdlib_bc_h}"
    DEPENDS "${_stdlib_src}" "${HYPRWIN_LUAJIT_STAMP}"
    VERBATIM
    COMMENT "Compiling stdlib.lua to bytecode"
  )

  set_source_files_properties(
    "${PROJECT_SOURCE_DIR}/src/lua/runtime.cpp"
    PROPERTIES OBJECT_DEPENDS "${_stdlib_bc_h}"
  )

  target_include_directories(${target} PRIVATE "${PROJECT_BINARY_DIR}")
endfunction()
