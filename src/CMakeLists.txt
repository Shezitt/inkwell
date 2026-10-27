qt_add_executable(inkwell WIN32 MACOSX_BUNDLE app/main.cpp)

target_compile_definitions(inkwell PRIVATE INKWELL_VERSION="${PROJECT_VERSION}")
target_link_libraries(inkwell PRIVATE Qt6::Widgets inkwell_warnings)

include(GNUInstallDirs)
install(
  TARGETS inkwell
  BUNDLE DESTINATION .
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
