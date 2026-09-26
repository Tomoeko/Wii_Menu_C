# Apply warning policies per target without changing compiler defaults elsewhere.
function(wm_enable_warnings target)
    cmake_parse_arguments(WARNINGS "CONVERSION;ERROR" "" "" ${ARGN})
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(WARNINGS_CONVERSION)
            target_compile_options(${target} PRIVATE -Wconversion)
        endif()
        if(WARNINGS_ERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# Test names and executable names share the existing "-test" convention.
function(wm_add_test name)
    cmake_parse_arguments(TEST "" "WORKING_DIRECTORY"
        "SOURCES;LIBRARIES;INCLUDE_DIRECTORIES;ARGUMENTS;DEPENDENCIES" ${ARGN})
    set(target "${name}-test")
    add_executable(${target} ${TEST_SOURCES})
    if(TEST_LIBRARIES)
        target_link_libraries(${target} PRIVATE ${TEST_LIBRARIES})
    endif()
    if(TEST_INCLUDE_DIRECTORIES)
        target_include_directories(${target} PRIVATE ${TEST_INCLUDE_DIRECTORIES})
    endif()
    if(TEST_DEPENDENCIES)
        add_dependencies(${target} ${TEST_DEPENDENCIES})
    endif()

    # Tests use assert() for checks and some setup calls, including in Release.
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE -UNDEBUG)
    endif()

    add_test(NAME ${name} COMMAND ${target} ${TEST_ARGUMENTS})
    if(NOT TEST_WORKING_DIRECTORY)
        set(TEST_WORKING_DIRECTORY "${PROJECT_BINARY_DIR}")
    endif()
    set_tests_properties(${name} PROPERTIES
        WORKING_DIRECTORY "${TEST_WORKING_DIRECTORY}")
endfunction()
