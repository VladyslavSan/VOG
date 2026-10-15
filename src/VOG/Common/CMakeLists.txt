find_package(glm REQUIRED)
find_package(spdlog REQUIRED)

add_library(VOG.Common)
add_library(${PROJECT_NAME}::Common ALIAS VOG.Common)

set_target_properties(VOG.Common PROPERTIES FOLDER VOG)

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
        include/VOG/Common/Signals.hpp
        include/VOG/Common/SurfaceHandle.hpp
        include/VOG/Common/ThreadsafeWrapper.hpp
        include/VOG/Common/Config/PlatformConfig.hpp
    PRIVATE
        src/VOG/Common/Assert.cpp
        src/VOG/Common/JSONContainer.cpp
        src/VOG/Common/Signals.cpp
)

target_link_libraries(VOG.Common
    PRIVATE
        spdlog::spdlog
)

add_subdirectory(Test)