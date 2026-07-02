foreach(variable VERSION EXE RUNTIME_DIR SOURCE_DIR OUTPUT_DIR)
  if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
    message(FATAL_ERROR "${variable} is required")
  endif()
endforeach()

set(required_files
  "${EXE}"
  "${RUNTIME_DIR}/lua51.dll"
  "${RUNTIME_DIR}/d3dcompiler_47.dll"
  "${RUNTIME_DIR}/shaders/hyprwin_shader_api.hlsl"
  "${SOURCE_DIR}/.luarc.json"
  "${SOURCE_DIR}/hw_annotations.lua"
)
foreach(path IN LISTS required_files)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Required package file does not exist: ${path}")
  endif()
endforeach()

set(package_name "hyprwin-${VERSION}-windows-x64")
set(stage_root "${OUTPUT_DIR}/stage")
set(package_dir "${stage_root}/${package_name}")
set(archive "${OUTPUT_DIR}/${package_name}.zip")

file(REMOVE_RECURSE "${package_dir}")
file(MAKE_DIRECTORY "${package_dir}")

file(COPY_FILE "${EXE}" "${package_dir}/hyprwin.exe" ONLY_IF_DIFFERENT)
file(COPY_FILE "${RUNTIME_DIR}/lua51.dll" "${package_dir}/lua51.dll" ONLY_IF_DIFFERENT)
file(COPY_FILE "${SOURCE_DIR}/.luarc.json" "${package_dir}/.luarc.json" ONLY_IF_DIFFERENT)
file(COPY_FILE "${SOURCE_DIR}/hw_annotations.lua" "${package_dir}/hw_annotations.lua" ONLY_IF_DIFFERENT)
file(MAKE_DIRECTORY "${package_dir}/shaders")
file(COPY_FILE
  "${RUNTIME_DIR}/shaders/hyprwin_shader_api.hlsl"
  "${package_dir}/shaders/hyprwin_shader_api.hlsl"
  ONLY_IF_DIFFERENT
)

file(COPY_FILE
  "${RUNTIME_DIR}/d3dcompiler_47.dll"
  "${package_dir}/d3dcompiler_47.dll"
  ONLY_IF_DIFFERENT
)

file(REMOVE "${archive}")
file(ARCHIVE_CREATE
  OUTPUT "${archive}"
  PATHS "${package_name}"
  WORKING_DIRECTORY "${stage_root}"
  FORMAT zip
  COMPRESSION Deflate
  COMPRESSION_LEVEL 9
  THREADS 0
)

message(STATUS "Created ${archive}")
