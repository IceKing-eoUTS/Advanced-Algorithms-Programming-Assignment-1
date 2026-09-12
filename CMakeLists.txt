cmake_minimum_required(VERSION 3.16)

project(ResizableArrayAssignment LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Build type. Use Release for empirical benchmarks." FORCE)
endif()

option(RESIZABLE_ARRAY_ENABLE_SANITIZERS
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer for tests where supported"
    OFF)

function(enable_project_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

function(enable_test_sanitizers target_name)
    if(NOT RESIZABLE_ARRAY_ENABLE_SANITIZERS)
        return()
    endif()

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /fsanitize=address)
        target_link_options(${target_name} PRIVATE /fsanitize=address)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target_name} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
            -g
        )
        target_link_options(${target_name} PRIVATE
            -fsanitize=address,undefined
        )
    else()
        message(WARNING
            "Sanitizers requested but are not configured for ${CMAKE_CXX_COMPILER_ID}")
    endif()
endfunction()

add_library(array_headers INTERFACE)
target_include_directories(array_headers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(resizable_array_demo
    src/resizable_array_demo.cpp
)
target_link_libraries(resizable_array_demo PRIVATE array_headers)
enable_project_warnings(resizable_array_demo)

add_executable(baseline_demo
    src/baseline_demo.cpp
)
target_link_libraries(baseline_demo PRIVATE array_headers)
enable_project_warnings(baseline_demo)

add_executable(empirical_study
    src/empirical_study.cpp
)
target_link_libraries(empirical_study PRIVATE array_headers)
enable_project_warnings(empirical_study)

add_executable(generate_input
    tools/generate_input.cpp
)
enable_project_warnings(generate_input)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

enable_testing()

add_executable(test_arrays
    tests/test_arrays.cpp
)
target_link_libraries(test_arrays PRIVATE array_headers)
enable_project_warnings(test_arrays)
enable_test_sanitizers(test_arrays)

add_test(NAME test_arrays COMMAND test_arrays)
set_tests_properties(test_arrays PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
