# Core: storage and application logic. Must not depend on Qt Widgets.
add_library(
  inkwell_core STATIC
  core/AutosaveTimer.cpp
  core/BackgroundWriter.cpp
  core/Database.cpp
  core/FuzzyMatch.cpp
  core/Log.cpp
  core/NoteStore.cpp
  core/Schema.cpp
  core/Session.cpp)
target_include_directories(inkwell_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  inkwell_core
  PUBLIC Qt6::Core Qt6::Sql
  PRIVATE inkwell_warnings)

# UI: Qt Widgets front end.
add_library(
  inkwell_ui STATIC
  ui/EditorArea.cpp
  ui/EditorView.cpp
  ui/MainWindow.cpp
  ui/NavigationPane.cpp
  ui/PageTreeModel.cpp
  ui/QuickSwitcher.cpp)
target_link_libraries(
  inkwell_ui
  PUBLIC inkwell_core Qt6::Widgets
  PRIVATE inkwell_warnings)

qt_add_executable(inkwell WIN32 MACOSX_BUNDLE app/main.cpp app/TerminationSignals.cpp)

target_compile_definitions(inkwell PRIVATE INKWELL_VERSION="${PROJECT_VERSION}")
target_link_libraries(inkwell PRIVATE inkwell_ui inkwell_warnings)
qt_add_resources(inkwell "icons" PREFIX "/" BASE "${PROJECT_SOURCE_DIR}/data" FILES
                 "${PROJECT_SOURCE_DIR}/data/icons/io.github.shezitt.Inkwell.svg")

include(GNUInstallDirs)
install(
  TARGETS inkwell
  BUNDLE DESTINATION .
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(UNIX AND NOT APPLE)
  install(FILES "${PROJECT_SOURCE_DIR}/data/io.github.shezitt.Inkwell.desktop"
          DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES "${PROJECT_SOURCE_DIR}/data/icons/io.github.shezitt.Inkwell.svg"
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
