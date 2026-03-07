function(htj2k_transcoder_enable_sanitizers target_name)
  if(NOT HTJ2K_TRANSCODER_ENABLE_SANITIZERS)
    return()
  endif()

  if(MSVC)
    message(WARNING "Sanitizers are not configured for MSVC.")
    return()
  endif()

  target_compile_options(${target_name} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(${target_name} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
endfunction()
