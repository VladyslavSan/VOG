cmake_minimum_required(VERSION 3.10.0)

find_package(Boost COMPONENTS thread REQUIRED)

add_library(VOG.Engine)

set_target_properties(VOG.Engine PROPERTIES
    FOLDER VOG
)

target_include_directories(VOG.Engine
    PUBLIC
        include/
    PRIVATE
        src/
)

target_sources(VOG.Engine
    PRIVATE
        include/VOG/Engine/Renderer/Renderer.hpp
        include/VOG/Engine/Scene/Scene.hpp
        include/VOG/Engine/Scene/SceneObject.hpp
    PRIVATE
        src/VOG/Engine/Renderer/Renderer.cpp
        src/VOG/Engine/Scene/Scene.cpp
)

target_link_libraries(VOG.Engine
    PUBLIC
        VOG.Common
        Boost::thread
        VOG.Graphics
)