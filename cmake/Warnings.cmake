function(htj2k_transcoder_set_warnings target_name)
  if(MSVC)
    target_compile_options(${target_name} PRIVATE /W4)
    if(HTJ2K_TRANSCODER_WARNINGS_AS_ERRORS)
      target_compile_options(${target_name} PRIVATE /WX)
    endif()
    return()
  endif()

  target_compile_options(
    ${target_name}
    PRIVATE
      $<$<COMPILE_LANGUAGE:C>:-Wall>
      $<$<COMPILE_LANGUAGE:C>:-Wextra>
      $<$<COMPILE_LANGUAGE:C>:-Wpedantic>
      $<$<COMPILE_LANGUAGE:CXX>:-Wall>
      $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
      $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>
      $<$<COMPILE_LANGUAGE:CXX>:-Wconversion>
      $<$<COMPILE_LANGUAGE:CXX>:-Wsign-conversion>
      $<$<COMPILE_LANGUAGE:CXX>:-Wshadow>
      $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>
      $<$<COMPILE_LANGUAGE:CXX>:-Wold-style-cast>
      $<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>
      $<$<COMPILE_LANGUAGE:CXX>:-Wnull-dereference>
      $<$<COMPILE_LANGUAGE:CXX>:-Wdouble-promotion>)

  if(HTJ2K_TRANSCODER_WARNINGS_AS_ERRORS)
    target_compile_options(${target_name} PRIVATE -Werror)
  endif()
endfunction()
