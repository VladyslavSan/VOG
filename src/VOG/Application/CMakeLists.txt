cmake_minimum_required(VERSION 3.10.0)

find_package(SDL2 REQUIRED)

add_library(VOG.Application)
set_target_properties(VOG.Application PROPERTIES
    FOLDER VOG
)

target_include_directories(VOG.Application
    PUBLIC
        include/
    PRIVATE
        src/
)

target_sources(VOG.Application
    PRIVATE
        include/VOG/Application/Application.hpp
    PRIVATE
        src/VOG/Application/Application.cpp
        src/VOG/Application/SDL2.hpp
        src/VOG/Application/SDL2.cpp
)

target_link_libraries(VOG.Application
    PUBLIC
        VOG.Common VOG.Engine
    PRIVATE
        sdl::sdl
)

if(APPLE)
    # For apple compile this file as an obj-c file.
    set_property(SOURCE src/VOG/Application/SDL2.cpp PROPERTY LANGUAGE OBJCXX)
endif()

