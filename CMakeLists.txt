cmake_minimum_required(VERSION 3.23.0)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project(VOG)
enable_testing()

enable_language(CXX)
if (APPLE)
    enable_language(OBJCXX)
endif ()

add_subdirectory(src)

