cmake_minimum_required(VERSION 3.21)

project(
  inkwell
  VERSION 0.3.0
  DESCRIPTION "A fast, native notes app that never loses what you wrote"
  HOMEPAGE_URL "https://github.com/Shezitt/inkwell"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE
      RelWithDebInfo
      CACHE STRING "Build type" FORCE)
endif()

option(INKWELL_BUILD_TESTS "Build the test suite" ${PROJECT_IS_TOP_LEVEL})
option(INKWELL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(INKWELL_ENABLE_SANITIZERS "Build with AddressSanitizer and UBSan" OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Sql)
qt_standard_project_setup()

# Keep string handling explicit and fast across the whole code base.
add_compile_definitions(
  QT_NO_CAST_FROM_ASCII
  QT_NO_CAST_TO_ASCII
  QT_NO_CAST_FROM_BYTEARRAY
  QT_NO_URL_CAST_FROM_STRING
  QT_USE_QSTRINGBUILDER)

add_subdirectory(src)

if(INKWELL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
