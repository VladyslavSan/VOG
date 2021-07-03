cmake_minimum_required(VERSION 3.10.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_INCLUDE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/include)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

project(vulkan_dev)

option(ENABLE_EXTERNAL_LIBRARIES_TESTS "Enable tests from external libraries" OFF)

add_compile_definitions(NOMINMAX)

set(PROJECT_DEPENDENCIES_DIR ${PROJECT_SOURCE_DIR}/subprojects)
add_subdirectory(subprojects)

add_subdirectory(src)