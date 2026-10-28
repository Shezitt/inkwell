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
inkwell_add_test(tst_schema SOURCES tst_schema.cpp LIBRARIES inkwell_core)
inkwell_add_test(tst_notestore SOURCES tst_notestore.cpp LIBRARIES inkwell_core)
inkwell_add_test(tst_backgroundwriter SOURCES tst_backgroundwriter.cpp LIBRARIES inkwell_core)

add_executable(crash_writer helpers/crash_writer.cpp)
target_link_libraries(crash_writer PRIVATE inkwell_core inkwell_warnings)
inkwell_add_test(tst_crashrecovery SOURCES tst_crashrecovery.cpp LIBRARIES inkwell_core)
target_compile_definitions(tst_crashrecovery PRIVATE CRASH_WRITER_PATH="$<TARGET_FILE:crash_writer>")
add_dependencies(tst_crashrecovery crash_writer)
inkwell_add_test(tst_pagetreemodel SOURCES tst_pagetreemodel.cpp LIBRARIES inkwell_ui)
inkwell_add_test(tst_navigationpane SOURCES tst_navigationpane.cpp LIBRARIES inkwell_ui)
inkwell_add_test(tst_quickswitcher SOURCES tst_quickswitcher.cpp LIBRARIES inkwell_ui)
inkwell_add_test(tst_pagesearch SOURCES tst_pagesearch.cpp LIBRARIES inkwell_ui)
inkwell_add_test(tst_autosavetimer SOURCES tst_autosavetimer.cpp LIBRARIES inkwell_core)
inkwell_add_test(tst_editorview SOURCES tst_editorview.cpp TestNotebook.h LIBRARIES inkwell_ui)
inkwell_add_test(tst_editorarea SOURCES tst_editorarea.cpp TestNotebook.h LIBRARIES inkwell_ui)
inkwell_add_test(tst_fuzzymatch SOURCES tst_fuzzymatch.cpp LIBRARIES inkwell_core)
inkwell_add_test(tst_session SOURCES tst_session.cpp LIBRARIES inkwell_core)
inkwell_add_test(tst_mainwindow SOURCES tst_mainwindow.cpp TestNotebook.h LIBRARIES inkwell_ui)

inkwell_add_test(tst_shutdown SOURCES tst_shutdown.cpp LIBRARIES inkwell_core)
target_compile_definitions(tst_shutdown PRIVATE INKWELL_APP_PATH="$<TARGET_FILE:inkwell>")
add_dependencies(tst_shutdown inkwell)
