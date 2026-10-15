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
            include/VOG/Engine/Renderer/CameraView.hpp
            include/VOG/Engine/Renderer/Extent.hpp
            include/VOG/Engine/Renderer/FrameContext.hpp
            include/VOG/Engine/Renderer/Material.hpp
            include/VOG/Engine/Renderer/Mesh.hpp
            include/VOG/Engine/Renderer/Renderable.hpp
            include/VOG/Engine/Renderer/Renderer.hpp
            include/VOG/Engine/Scene/Components.hpp
            include/VOG/Engine/Scene/Registry.hpp
            include/VOG/Engine/Scene/Scene.hpp
    PRIVATE
        src/VOG/Engine/Renderer/CameraView.cpp
        src/VOG/Engine/Renderer/FramePacketQueue.hpp
        src/VOG/Engine/Renderer/MaterialDefinition.hpp
        src/VOG/Engine/Renderer/MeshDefinition.hpp
        src/VOG/Engine/Renderer/PipelineCache.cpp
        src/VOG/Engine/Renderer/PipelineCache.hpp
        src/VOG/Engine/Renderer/Renderer.cpp
        src/VOG/Engine/Scene/Scene.cpp
)

# Every header under include/ must be in the public header set: VOG.Engine.PublicHeadersTest is
# generated from the set, so a header left out of it would escape the check that no public header
# pulls in Graphics or Vulkan.
file(GLOB_RECURSE onDiskHeaders CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    include/*.hpp)
get_target_property(setHeaders Engine HEADER_SET_public_headers)
foreach(header IN LISTS onDiskHeaders)
    cmake_path(ABSOLUTE_PATH header BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE headerPath)
    if(NOT headerPath IN_LIST setHeaders)
        message(FATAL_ERROR "${header} is not in the public_headers FILE_SET of Engine; "
                            "add it, or it escapes VOG.Engine.PublicHeadersTest.")
    endif()
endforeach()

target_link_libraries(Engine
    PUBLIC
        ${PROJECT_NAME}::Common
        ${PROJECT_NAME}::Math
    PRIVATE
        ${PROJECT_NAME}::Graphics
        spdlog::spdlog
)

install(TARGETS Engine
        FILE_SET public_headers
)

add_subdirectory(Test)
