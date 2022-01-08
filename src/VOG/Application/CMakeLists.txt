cmake_minimum_required(VERSION 3.10.0)

find_package(SDL2 REQUIRED)

utility_declare_project(VOG.Application)

set(APPLICATION_HEADERS
    ${INTERFACE_DIR}/VOG/Application/Application.hpp
)

set(APPLICATION_SOURCES
    ${IMPLEMENTATION_DIR}/VOG/Application/Application.cpp
    ${IMPLEMENTATION_DIR}/VOG/Application/SDL2.hpp
    ${IMPLEMENTATION_DIR}/VOG/Application/SDL2.cpp
)

if(APPLE)
    # For apple compile this file as an obj-c file.
    set_property(SOURCE ${IMPLEMENTATION_DIR}/VOG/Application/SDL2.cpp PROPERTY LANGUAGE OBJCXX)
endif()

utility_add_target(
    LIBRARY
    HEADERS ${APPLICATION_HEADERS}
    SOURCES ${APPLICATION_SOURCES}
    LINK_PUBLIC VOG.Common VOG.Engine
    LINK_PRIVATE sdl::sdl
    FOLDER VOG
)
