cmake_minimum_required(VERSION 3.10.0)

find_package(glm REQUIRED)

add_library(VOG.Common)
set_target_properties(VOG.Common PROPERTIES
    FOLDER VOG
)

target_include_directories(VOG.Common
    PUBLIC
        include/
    PRIVATE
        src/
)

target_sources(VOG.Common
    PRIVATE
        include/VOG/Common/Assert.hpp
        include/VOG/Common/Concepts.hpp
        include/VOG/Common/JSONContainer.hpp
        include/VOG/Common/NoCopyable.hpp
        include/VOG/Common/Signals.hpp
        include/VOG/Common/ThreadsafeWrapper.hpp
        include/VOG/Common/Config/GLMConfig.hpp
        include/VOG/Common/Config/PlatformConfig.hpp
        include/VOG/Common/Math/Math.hpp
    PRIVATE
        src/VOG/Common/JSONContainer.cpp
        src/VOG/Common/Signals.cpp
)

target_link_libraries(VOG.Common
    PUBLIC
        glm::glm
)

add_subdirectory(Test)