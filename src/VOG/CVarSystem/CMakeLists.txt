cmake_minimum_required(VERSION 3.10.0)

find_package(GTest REQUIRED)

add_library(VOG.CVarSystem SHARED)
add_library(${PROJECT_NAME}::CVarSystem ALIAS VOG.CVarSystem)
set_target_properties(VOG.CVarSystem PROPERTIES FOLDER "VOG")

target_include_directories(VOG.CVarSystem
    PUBLIC
        include
    PRIVATE
        src
)

target_sources(VOG.CVarSystem
    PRIVATE
        include/VOG/CVarSystem/Api.hpp
        include/VOG/CVarSystem/System.hpp
    PRIVATE
        src/VOG/CVarSystem/System.cpp
)

target_link_libraries(VOG.CVarSystem
    PUBLIC
        VOG.Common
)

target_compile_definitions(VOG.CVarSystem PRIVATE CVARSYSTEM_API_EXPORT)