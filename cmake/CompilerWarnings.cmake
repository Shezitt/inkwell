# Provides the `inkwell_warnings` interface target. Link it to any first-party
# target to get a consistent, strict set of compiler warnings.

add_library(inkwell_warnings INTERFACE)

if(MSVC)
  target_compile_options(inkwell_warnings INTERFACE /W4 /permissive-)
  if(INKWELL_WARNINGS_AS_ERRORS)
    target_compile_options(inkwell_warnings INTERFACE /WX)
  endif()
else()
  target_compile_options(
    inkwell_warnings
    INTERFACE -Wall
              -Wextra
              -Wpedantic
              -Wshadow
              -Wnon-virtual-dtor
              -Woverloaded-virtual
              -Wcast-align
              -Wnull-dereference
              -Wimplicit-fallthrough)
  if(INKWELL_WARNINGS_AS_ERRORS)
    target_compile_options(inkwell_warnings INTERFACE -Werror)
  endif()
endif()
