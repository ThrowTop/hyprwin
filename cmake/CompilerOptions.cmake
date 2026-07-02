include_guard(GLOBAL)

function(hyprwin_apply_compiler_options target)
  target_compile_options(${target} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:/permissive->
    $<$<COMPILE_LANGUAGE:CXX>:/W4>
    $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
    $<$<COMPILE_LANGUAGE:CXX>:/Zc:preprocessor>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/GL>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/Gw>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/fp:fast>
    $<$<COMPILE_LANGUAGE:RC>:/I${PROJECT_SOURCE_DIR}/src/res>
  )

  target_link_options(${target} PRIVATE
    $<$<CONFIG:Debug>:/ILK:${PROJECT_BINARY_DIR}/symbols/$<TARGET_FILE_BASE_NAME:${target}>.ilk>
    $<$<CONFIG:Release>:/LTCG>
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>
    $<$<CONFIG:Release>:/INCREMENTAL:NO>
  )
endfunction()
