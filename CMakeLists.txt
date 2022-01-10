cmake_minimum_required(VERSION 3.10.0)

cmake_policy(SET CMP0076 NEW)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project(vulkan_dev)

enable_language(CXX)
if(APPLE)
enable_language(OBJCXX)
endif()

enable_testing()

add_subdirectory(src)