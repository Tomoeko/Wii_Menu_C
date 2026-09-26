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
add_subdirectory(src)
add_subdirectory(tools)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
