# Enables AddressSanitizer and UndefinedBehaviorSanitizer for every target
# defined after this file is included, when INKWELL_ENABLE_SANITIZERS is ON.

if(NOT INKWELL_ENABLE_SANITIZERS)
  return()
endif()

if(MSVC)
  message(WARNING "INKWELL_ENABLE_SANITIZERS is not supported with MSVC; ignoring")
  return()
endif()

set(_inkwell_sanitizer_flags -fsanitize=address,undefined -fno-omit-frame-pointer
                             -fno-sanitize-recover=undefined)
add_compile_options(${_inkwell_sanitizer_flags})
add_link_options(${_inkwell_sanitizer_flags})
