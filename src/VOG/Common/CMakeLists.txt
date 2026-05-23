cmake_minimum_required(VERSION 3.10.0)

find_package(glm REQUIRED)

add_library(Common)
add_library(${PROJECT_NAME}::Common ALIAS Common)

set_target_properties(Common PROPERTIES FOLDER VOG)

target_include_directories(Common
    PUBLIC
        include/
    PRIVATE
        src/
)

target_sources(Common
    PRIVATE
        include/VOG/Common/Assert.hpp
        include/VOG/Common/Concepts.hpp
        include/VOG/Common/JSONContainer.hpp
        include/VOG/Common/NoCopyable.hpp
        include/VOG/Common/Signals.hpp
        include/VOG/Common/SurfaceHandle.hpp
        include/VOG/Common/ThreadsafeWrapper.hpp
        include/VOG/Common/Config/PlatformConfig.hpp
    PRIVATE
        src/VOG/Common/JSONContainer.cpp
        src/VOG/Common/Signals.cpp
)

add_subdirectory(Test)