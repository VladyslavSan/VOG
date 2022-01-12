cmake_minimum_required(VERSION 3.10.0)

cmake_policy(SET CMP0076 NEW)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project(VOG)
enable_testing()

enable_language(CXX)
if(APPLE)
    enable_language(OBJCXX)
endif()

add_subdirectory(src)

