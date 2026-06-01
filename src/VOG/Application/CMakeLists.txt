find_package(SDL3 REQUIRED)
find_package(Boost COMPONENTS headers program_options process REQUIRED)
find_package(spdlog COMPONENTS)

add_library(Application)
add_library(${PROJECT_NAME}::Application ALIAS Application)

set_target_properties(Application PROPERTIES FOLDER VOG)

target_include_directories(Application
    PUBLIC
        include/
    PRIVATE
        src/
)

target_sources(Application
    PRIVATE
        include/VOG/Application/Application.hpp
    PRIVATE
        src/VOG/Application/Application.cpp
        src/VOG/Application/SDL.hpp
        src/VOG/Application/SDL.cpp
)

target_link_libraries(Application
    PUBLIC
        ${PROJECT_NAME}::Engine
        sdl::sdl
    PRIVATE
        Boost::headers
        Boost::program_options
        Boost::process
        spdlog::spdlog
)

if(APPLE)
    # For apple compile this file as an obj-c file.
    set_property(SOURCE src/VOG/Application/SDL.cpp PROPERTY LANGUAGE OBJCXX)
endif()



