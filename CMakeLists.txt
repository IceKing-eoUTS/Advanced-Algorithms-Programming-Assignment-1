cmake_minimum_required(VERSION 3.16)

project(ResizableArrayAssignment LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(array_headers INTERFACE)
target_include_directories(array_headers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(array_headers INTERFACE /W4 /permissive-)
else()
    target_compile_options(array_headers INTERFACE -Wall -Wextra -Wpedantic)
endif()

add_executable(resizable_array_demo
    src/resizable_array_demo.cpp
)
target_link_libraries(resizable_array_demo PRIVATE array_headers)

add_executable(baseline_demo
    src/baseline_demo.cpp
)
target_link_libraries(baseline_demo PRIVATE array_headers)

add_executable(empirical_study
    src/empirical_study.cpp
)
target_link_libraries(empirical_study PRIVATE array_headers)

add_executable(generate_input
    tools/generate_input.cpp
)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

enable_testing()

add_executable(test_arrays
    tests/test_arrays.cpp
)
target_link_libraries(test_arrays PRIVATE array_headers)

add_test(NAME test_arrays COMMAND test_arrays)
