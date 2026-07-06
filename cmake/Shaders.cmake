include_guard(GLOBAL)

function(hyprwin_add_shaders target)
  find_program(FXC_EXE NAMES fxc
    HINTS "$ENV{WindowsSdkVerBinPath}/x64"
    DOC "HLSL shader compiler (fxc.exe)"
  )
  if(NOT FXC_EXE)
    message(FATAL_ERROR "fxc.exe not found. Install the Windows SDK.")
  endif()

  set(_shader_src_dir "${PROJECT_SOURCE_DIR}/src/shaders")
  set(_shader_generated_dir "${PROJECT_BINARY_DIR}/generated/shaders")
  set(_outline_src "${_shader_src_dir}/outline.hlsl")
  set(_shader_api "${_shader_src_dir}/hyprwin_shader_api.hlsl")
  set(_outline_vs_header "${_shader_generated_dir}/outline_vs.h")
  set(_outline_ps_header "${_shader_generated_dir}/outline_ps.h")
  set(_thumbnail_src "${_shader_src_dir}/thumbnail.hlsl")
  set(_thumbnail_vs_header "${_shader_generated_dir}/thumbnail_vs.h")
  set(_thumbnail_ps_header "${_shader_generated_dir}/thumbnail_ps.h")
  set(_runtime_api "${HYPRWIN_RUNTIME_DIR}/shaders/hyprwin_shader_api.hlsl")
  file(MAKE_DIRECTORY "${_shader_generated_dir}")
  target_include_directories(${target} PRIVATE "${_shader_generated_dir}")

  string(TOLOWER "${CMAKE_BUILD_TYPE}" _cfg)
  if(_cfg STREQUAL "debug")
    set(_fxc_flags /Od /Zi)
  else()
    set(_fxc_flags /O3 /Ges /WX /Qstrip_debug /Qstrip_reflect /Qstrip_priv)
  endif()

  add_custom_command(
    OUTPUT  "${_outline_vs_header}"
    COMMAND "${FXC_EXE}" /nologo /T vs_5_0 /E vs_main
                       /Fh "${_outline_vs_header}" /Vn kOutlineVS
                       ${_fxc_flags}
                       "${_outline_src}"
    DEPENDS "${_outline_src}" "${_shader_api}"
    VERBATIM
    COMMENT "Compiling outline vertex shader"
  )

  add_custom_command(
    OUTPUT "${_thumbnail_vs_header}"
    COMMAND "${FXC_EXE}" /nologo /T vs_5_0 /E vs_main
                       /Fh "${_thumbnail_vs_header}" /Vn kThumbnailVS
                       ${_fxc_flags}
                       "${_thumbnail_src}"
    DEPENDS "${_thumbnail_src}"
    VERBATIM
    COMMENT "Compiling thumbnail vertex shader"
  )

  add_custom_command(
    OUTPUT "${_thumbnail_ps_header}"
    COMMAND "${FXC_EXE}" /nologo /T ps_5_0 /E ps_main
                       /Fh "${_thumbnail_ps_header}" /Vn kThumbnailPS
                       ${_fxc_flags}
                       "${_thumbnail_src}"
    DEPENDS "${_thumbnail_src}"
    VERBATIM
    COMMENT "Compiling thumbnail pixel shader"
  )

  add_custom_command(
    OUTPUT  "${_outline_ps_header}"
    COMMAND "${FXC_EXE}" /nologo /T ps_5_0 /E ps_main
                       /Fh "${_outline_ps_header}" /Vn kOutlinePS
                       ${_fxc_flags}
                       "${_outline_src}"
    DEPENDS "${_outline_src}" "${_shader_api}"
    VERBATIM
    COMMENT "Compiling outline pixel shader"
  )

  add_custom_command(
    OUTPUT "${_runtime_api}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${HYPRWIN_RUNTIME_DIR}/shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_shader_api}" "${_runtime_api}"
    DEPENDS "${_shader_api}"
    VERBATIM
    COMMENT "Copying public shader API"
  )

  add_custom_target(hyprwin_shaders DEPENDS "${_outline_vs_header}" "${_outline_ps_header}" "${_thumbnail_vs_header}" "${_thumbnail_ps_header}" "${_runtime_api}")
  add_dependencies(${target} hyprwin_shaders)

  set_source_files_properties(
    "${PROJECT_SOURCE_DIR}/src/overlay/outline/outline_shader.cpp"
    PROPERTIES OBJECT_DEPENDS "${_outline_vs_header};${_outline_ps_header}"
  )
  set_source_files_properties(
    "${PROJECT_SOURCE_DIR}/src/overlay/thumbnail/thumbnail_shader.cpp"
    PROPERTIES OBJECT_DEPENDS "${_thumbnail_vs_header};${_thumbnail_ps_header}"
  )
endfunction()
