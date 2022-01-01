cmake_minimum_required(VERSION 3.10.0)

utility_declare_project(VOG.CVarSystem)

set(VOG_CVARSYSTEM_HEADERS
    ${INTERFACE_DIR}/VOG/CVarSystem/Api.hpp
    ${INTERFACE_DIR}/VOG/CVarSystem/System.hpp
)

set(VOG_CVARSYSTEM_SOURCES
    ${IMPLEMENTATION_DIR}/VOG/CVarSystem/System.cpp
)

utility_add_target(
    SHARED_LIBRARY
    FOLDER VOG
    HEADERS ${VOG_CVARSYSTEM_HEADERS}
    SOURCES ${VOG_CVARSYSTEM_SOURCES}
)

target_compile_definitions(VOG.CVarSystem PRIVATE CVARSYSTEM_API_EXPORT)