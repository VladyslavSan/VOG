cmake_minimum_required(VERSION 3.10.0)

utility_declare_project(VOG.Common)

set(VOG_COMMON_HEADERS
    ${INTERFACE_DIR}/VOG/Common/Assert.hpp
    ${INTERFACE_DIR}/VOG/Common/JSONContainer.hpp
    ${INTERFACE_DIR}/VOG/Common/NoCopyable.hpp
    ${INTERFACE_DIR}/VOG/Common/Signals.hpp
    ${INTERFACE_DIR}/VOG/Common/ThreadsafeWrapper.hpp
    ${INTERFACE_DIR}/VOG/Common/Config/GLMConfig.hpp
    ${INTERFACE_DIR}/VOG/Common/Config/PlatformConfig.hpp
    ${INTERFACE_DIR}/VOG/Common/Math/Math.hpp
)

set(VOG_COMMON_SOURCES
    ${IMPLEMENTATION_DIR}/VOG/Common/JSONContainer.cpp
    ${IMPLEMENTATION_DIR}/VOG/Common/Signals.cpp
)

utility_add_target(
    LIBRARY
    LINK_PUBLIC glm
    FOLDER VOG
    HEADERS ${VOG_COMMON_HEADERS}
    SOURCES ${VOG_COMMON_SOURCES}
)

add_subdirectory(Test)