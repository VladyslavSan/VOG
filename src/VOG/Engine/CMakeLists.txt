find_package(spdlog REQUIRED)

add_library(Engine)
add_library(${PROJECT_NAME}::Engine ALIAS Engine)

set_target_properties(Engine PROPERTIES
    FOLDER VOG
)

target_include_directories(Engine
    PUBLIC
        include/
    PRIVATE
        src/
)

target_sources(Engine
    PUBLIC
        FILE_SET
            public_headers
        TYPE
            HEADERS
        BASE_DIRS
            include/
        FILES
            include/VOG/Engine/Scene/Scene.hpp
            include/VOG/Engine/Renderer/Renderer.hpp
            include/VOG/Engine/Scene/SceneObject.hpp
    PRIVATE
        src/VOG/Engine/Renderer/Renderer.cpp
        src/VOG/Engine/Scene/Scene.cpp
)

target_link_libraries(Engine
    PUBLIC
        ${PROJECT_NAME}::Common
        VOG.Math
        ${PROJECT_NAME}::Graphics
    PRIVATE
        spdlog::spdlog
)

install(TARGETS Engine
        FILE_SET public_headers
)