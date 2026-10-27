find_package(Qt6 REQUIRED COMPONENTS Test)

# inkwell_add_test(<name> SOURCES <files...> [LIBRARIES <targets...>])
#
# Builds a Qt Test executable and registers it with CTest. Tests run with the
# offscreen platform so they work on headless CI machines.
function(inkwell_add_test name)
  cmake_parse_arguments(ARG "" "" "SOURCES;LIBRARIES" ${ARGN})
  add_executable(${name} ${ARG_SOURCES})
  target_link_libraries(${name} PRIVATE Qt6::Test inkwell_warnings ${ARG_LIBRARIES})
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

inkwell_add_test(tst_database SOURCES tst_database.cpp LIBRARIES inkwell_core)
