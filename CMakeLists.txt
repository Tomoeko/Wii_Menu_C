cmake_minimum_required(VERSION 3.20)

project(WiiMenuC VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Keep the application and command-line tools at the documented build paths.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(APPLE)
    set(WM_BACKEND "metal" CACHE STRING "Graphics backend: metal or gles2")
else()
    set(WM_BACKEND "gles2" CACHE STRING "Graphics backend: metal or gles2")
endif()
set_property(CACHE WM_BACKEND PROPERTY STRINGS metal gles2)

option(WM_BUILD_APP "Build the menu application and selected platform backend" ON)

include(cmake/WiiMenuBuild.cmake)
add_subdirectory(Common)
add_subdirectory(src)
add_subdirectory(tools)

find_program(WM_CLANG_FORMAT NAMES clang-format clang-format-16)
if(APPLE AND NOT WM_CLANG_FORMAT)
    execute_process(
        COMMAND xcrun --find clang-format
        OUTPUT_VARIABLE WM_CLANG_FORMAT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE WM_FORMAT_LOOKUP_RESULT)
    if(NOT WM_FORMAT_LOOKUP_RESULT EQUAL 0)
        unset(WM_CLANG_FORMAT)
    endif()
endif()
if(WM_CLANG_FORMAT)
    file(GLOB_RECURSE WM_FORMAT_SOURCES CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/include/*.c"
        "${PROJECT_SOURCE_DIR}/include/*.h"
        "${PROJECT_SOURCE_DIR}/src/*.c"
        "${PROJECT_SOURCE_DIR}/src/*.h"
        "${PROJECT_SOURCE_DIR}/src/*.m"
        "${PROJECT_SOURCE_DIR}/tests/*.c"
        "${PROJECT_SOURCE_DIR}/tests/*.h"
        "${PROJECT_SOURCE_DIR}/tools/*.c"
        "${PROJECT_SOURCE_DIR}/tools/*.h")
    add_custom_target(wm-check-format
        COMMAND "${WM_CLANG_FORMAT}" --dry-run --Werror ${WM_FORMAT_SOURCES}
        VERBATIM)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
