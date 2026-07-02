include_guard(GLOBAL)

function(hyprwin_add_shaders target)
  find_program(FXC_EXE NAMES fxc
    HINTS "$ENV{WindowsSdkVerBinPath}/x64"
    DOC "HLSL shader compiler (fxc.exe)"
  )
  if(NOT FXC_EXE)
    message(FATAL_ERROR "fxc.exe not found. Install the Windows SDK.")
  endif()

  set(_shader_src "${PROJECT_SOURCE_DIR}/src/shader/default.hlsl")
  set(_shader_api "${PROJECT_SOURCE_DIR}/src/shader/hyprwin_shader_api.hlsl")
  set(_vs_header  "${PROJECT_BINARY_DIR}/border_vs.h")
  set(_ps_header  "${PROJECT_BINARY_DIR}/border_ps.h")
  set(_runtime_api "${HYPRWIN_RUNTIME_DIR}/shaders/hyprwin_shader_api.hlsl")

  string(TOLOWER "${CMAKE_BUILD_TYPE}" _cfg)
  if(_cfg STREQUAL "debug")
    set(_fxc_flags /Od /Zi)
  else()
    set(_fxc_flags /O3 /Ges /WX /Qstrip_debug /Qstrip_reflect /Qstrip_priv)
  endif()

  add_custom_command(
    OUTPUT  "${_vs_header}"
    COMMAND "${FXC_EXE}" /nologo /T vs_5_0 /E vs_main
                       /Fh "${_vs_header}" /Vn kBorderVS
                       ${_fxc_flags}
                       "${_shader_src}"
    DEPENDS "${_shader_src}" "${_shader_api}"
    VERBATIM
    COMMENT "Compiling border vertex shader"
  )

  add_custom_command(
    OUTPUT  "${_ps_header}"
    COMMAND "${FXC_EXE}" /nologo /T ps_5_0 /E ps_main
                       /Fh "${_ps_header}" /Vn kBorderPS
                       ${_fxc_flags}
                       "${_shader_src}"
    DEPENDS "${_shader_src}" "${_shader_api}"
    VERBATIM
    COMMENT "Compiling border pixel shader"
  )

  add_custom_command(
    OUTPUT "${_runtime_api}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${HYPRWIN_RUNTIME_DIR}/shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_shader_api}" "${_runtime_api}"
    DEPENDS "${_shader_api}"
    VERBATIM
    COMMENT "Copying public shader API"
  )

  add_custom_target(hyprwin_shaders DEPENDS "${_vs_header}" "${_ps_header}" "${_runtime_api}")
  add_dependencies(${target} hyprwin_shaders)

  set_source_files_properties(
    "${PROJECT_SOURCE_DIR}/src/overlay/border_shader.cpp"
    PROPERTIES OBJECT_DEPENDS "${_vs_header};${_ps_header}"
  )
endfunction()
