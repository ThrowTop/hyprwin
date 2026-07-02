include_guard(GLOBAL)

function(hyprwin_copy_d3dcompiler_runtime target)
  if(NOT WIN32)
    return()
  endif()

  set(_d3dcompiler_candidates)

  if(DEFINED ENV{ProgramFiles\(x86\)})
    list(APPEND _d3dcompiler_candidates
      "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/Redist/D3D/x64/d3dcompiler_47.dll"
    )
  endif()

  if(DEFINED ENV{WindowsSdkDir})
    list(APPEND _d3dcompiler_candidates
      "$ENV{WindowsSdkDir}/Redist/D3D/x64/d3dcompiler_47.dll"
    )
  endif()

  set(HYPRWIN_D3DCOMPILER_47_DLL "" CACHE FILEPATH
      "Redistributable x64 d3dcompiler_47.dll for runtime custom shader compilation")

  if(NOT HYPRWIN_D3DCOMPILER_47_DLL)
    foreach(_candidate IN LISTS _d3dcompiler_candidates)
      if(EXISTS "${_candidate}")
        set(HYPRWIN_D3DCOMPILER_47_DLL "${_candidate}" CACHE FILEPATH
            "Redistributable x64 d3dcompiler_47.dll for runtime custom shader compilation" FORCE)
        break()
      endif()
    endforeach()
  endif()

  if(NOT HYPRWIN_D3DCOMPILER_47_DLL)
    message(WARNING "d3dcompiler_47.dll redist not found; runtime custom shader compilation will require a system/app-local DLL.")
    return()
  endif()

  set(_runtime_dll "${HYPRWIN_RUNTIME_DIR}/d3dcompiler_47.dll")

  add_custom_command(
    OUTPUT "${_runtime_dll}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${HYPRWIN_D3DCOMPILER_47_DLL}"
            "${_runtime_dll}"
    DEPENDS "${HYPRWIN_D3DCOMPILER_47_DLL}"
    VERBATIM
    COMMENT "Copying d3dcompiler_47.dll for runtime custom shaders"
  )
  add_custom_target(${target}_copy_d3dcompiler_runtime DEPENDS "${_runtime_dll}")
  add_dependencies(${target} ${target}_copy_d3dcompiler_runtime)
endfunction()
