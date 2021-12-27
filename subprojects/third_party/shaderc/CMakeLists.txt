cmake_minimum_required(VERSION 3.10.0)
project(shaderc_external)

include(ExternalProject)
cmake_policy(SET CMP0114 NEW)

set(BUILD_TYPE "Release")
if (WIN32)
    set(BUILD_TYPE "Debug")
endif()

set(CMAKE_ARGS -DSHADERC_SKIP_TESTS=ON -DSHADERC_SKIP_EXAMPLES=ON -DCMAKE_BUILD_TYPE=${BUILD_TYPE})

find_package(Python3 COMPONENTS Interpreter)
ExternalProject_Add(shaderc
    PREFIX ${CMAKE_BINARY_DIR}
    BINARY_DIR build
    INSTALL_DIR install
    GIT_REPOSITORY "https://github.com/google/shaderc.git"
    GIT_TAG "v2021.3"
    PATCH_COMMAND ${Python3_EXECUTABLE} ${CMAKE_BINARY_DIR}/src/shaderc/utils/git-sync-deps
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
    TEST_COMMAND ""
    CMAKE_ARGS ${CMAKE_ARGS}
)