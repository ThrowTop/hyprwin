include_guard(GLOBAL)

function(hyprwin_add_luajit target)
  set(_lj_src_root  "${PROJECT_SOURCE_DIR}/external/luajit")
  set(_lj_src_dir   "${_lj_src_root}/src")
  set(_lj_out_dir   "${PROJECT_SOURCE_DIR}/build/.work/luajit")
  set(_lj_work_root "${PROJECT_SOURCE_DIR}/build/.work/temp/luajit")
  set(_lj_inc_dir   "${_lj_out_dir}/include")
  set(_lj_lib       "${_lj_out_dir}/lua51.lib")
  set(_lj_dll       "${_lj_out_dir}/lua51.dll")
  set(_lj_stamp     "${_lj_out_dir}/built.stamp")
  set(_lj_exe       "${_lj_out_dir}/luajit.exe")
  set(_lj_relver    "${PROJECT_BINARY_DIR}/luajit.relver")
  set(_runtime_dll  "${HYPRWIN_RUNTIME_DIR}/lua51.dll")

  file(GLOB_RECURSE _lj_inputs CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${_lj_src_root}/src/*"
    "${_lj_src_root}/dynasm/*"
  )
  list(APPEND _lj_inputs
    "${_lj_src_root}/.relver"
    "${_lj_src_root}/Makefile"
    "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
  )

  file(MAKE_DIRECTORY "${_lj_inc_dir}")
  file(WRITE "${_lj_relver}" "0\n")

  set(_lj_byproducts "${_lj_lib}" "${_lj_dll}" "${_lj_inc_dir}/luajit.h" "${_lj_exe}")

  add_custom_command(
    OUTPUT "${_lj_stamp}"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_lj_work_root}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${_lj_src_root}" "${_lj_work_root}"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_lj_work_root}/.git"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_lj_relver}" "${_lj_work_root}/.relver"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_lj_out_dir}" "${_lj_inc_dir}"
    COMMAND "${CMAKE_COMMAND}" -E chdir "${_lj_work_root}/src"
            "${CMAKE_COMMAND}" -E env "PATH=${_lj_work_root}/src;$ENV{PATH}"
            cmd /c msvcbuild.bat
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_lj_work_root}/src/lua51.lib" "${_lj_lib}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_lj_work_root}/src/lua51.dll" "${_lj_dll}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_lj_work_root}/src/luajit.exe" "${_lj_exe}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_lj_work_root}/src/luajit.h" "${_lj_inc_dir}/luajit.h"
    COMMAND "${CMAKE_COMMAND}" -E touch "${_lj_stamp}"
    DEPENDS ${_lj_inputs}
    BYPRODUCTS ${_lj_byproducts}
    VERBATIM
    COMMENT "Building bundled LuaJIT DLL"
  )

  add_custom_target(luajit_build DEPENDS "${_lj_stamp}")

  add_library(luajit::lua51 UNKNOWN IMPORTED GLOBAL)
  set_target_properties(luajit::lua51 PROPERTIES
    IMPORTED_LOCATION             "${_lj_lib}"
    INTERFACE_INCLUDE_DIRECTORIES "${_lj_inc_dir};${_lj_src_dir}"
  )
  add_dependencies(luajit::lua51 luajit_build)

  target_link_libraries(${target} PRIVATE luajit::lua51)
  target_compile_definitions(${target} PRIVATE LUA_BUILD_AS_DLL)

  add_custom_command(
    OUTPUT "${_runtime_dll}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${_lj_dll}" "${_runtime_dll}"
    DEPENDS "${_lj_dll}"
    VERBATIM
    COMMENT "Copying lua51.dll to output"
  )
  add_custom_target(${target}_copy_luajit_runtime DEPENDS "${_runtime_dll}")
  add_dependencies(${target} ${target}_copy_luajit_runtime)

  set(HYPRWIN_LUAJIT_EXE "${_lj_exe}" PARENT_SCOPE)
  set(HYPRWIN_LUAJIT_STAMP "${_lj_stamp}" PARENT_SCOPE)
  set(HYPRWIN_LUAJIT_LUA_PATH "${_lj_src_dir}/?.lua" PARENT_SCOPE)
endfunction()
