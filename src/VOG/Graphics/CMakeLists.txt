cmake_minimum_required(VERSION 3.10.0)

find_package(VulkanHeaders REQUIRED)
find_package(vulkan-memory-allocator REQUIRED)
find_package(Boost COMPONENTS container REQUIRED)
find_package(spirv-cross REQUIRED)
find_package(shaderc REQUIRED)
find_package(nlohmann_json REQUIRED)

add_library(VOG.Graphics)
set_target_properties(VOG.Graphics PROPERTIES
    FOLDER VOG
)

target_include_directories(VOG.Graphics
    PUBLIC
        include/
    PRIVATE
        src/
)

target_sources(VOG.Graphics
    PRIVATE
        include/VOG/Graphics/Config/VulkanConfig.hpp
        include/VOG/Graphics/Frame/FrameObjectManager.hpp
        include/VOG/Graphics/Frame/FrameObjects.hpp
        include/VOG/Graphics/Frame/ThreadObjects.hpp
        include/VOG/Graphics/Vulkan/Attachment/AttachmentInterface.hpp
        include/VOG/Graphics/Vulkan/Attachment/RenderBuffer.hpp
        include/VOG/Graphics/Vulkan/Attachment/Swapchain.hpp
        include/VOG/Graphics/Vulkan/CommandBuffer.hpp
        include/VOG/Graphics/Vulkan/CommandBufferPool.hpp
        include/VOG/Graphics/Vulkan/Device.hpp
        include/VOG/Graphics/Vulkan/Framebuffer.hpp
        include/VOG/Graphics/Vulkan/GraphicsPipeline.hpp
        include/VOG/Graphics/Vulkan/MemoryAllocator.hpp
        include/VOG/Graphics/Vulkan/Queue.hpp
        include/VOG/Graphics/Vulkan/RenderCommandRecorder.hpp
        include/VOG/Graphics/Vulkan/RenderPass.hpp
        include/VOG/Graphics/Vulkan/Shader.hpp
        include/VOG/Graphics/Vulkan/ShaderProgram.hpp
        include/VOG/Graphics/Vulkan/TimelineSemaphore.hpp
        include/VOG/Graphics/GraphicsProvider.hpp
        include/VOG/Graphics/ResourceManager.hpp
        include/VOG/Graphics/Typedefs.hpp
    
    PRIVATE
        src/VOG/Graphics/Config/VMAConfig.hpp
        src/VOG/Graphics/Frame/FrameObjectManager.cpp
        src/VOG/Graphics/Frame/FrameObjects.cpp
        src/VOG/Graphics/Frame/ThreadObjects.cpp
        src/VOG/Graphics/Vulkan/Attachment/Converters.hpp
        src/VOG/Graphics/Vulkan/Attachment/CreateRenderSurface.hpp
        src/VOG/Graphics/Vulkan/Attachment/CreateRenderSurface.cpp
        src/VOG/Graphics/Vulkan/Attachment/RenderBuffer.cpp
        src/VOG/Graphics/Vulkan/Attachment/Swapchain.cpp
        src/VOG/Graphics/Vulkan/CommandBuffer.cpp
        src/VOG/Graphics/Vulkan/CommandBufferPool.cpp
        src/VOG/Graphics/Vulkan/Framebuffer.cpp
        src/VOG/Graphics/Vulkan/GraphicsPipeline.cpp
        src/VOG/Graphics/Vulkan/MemoryAllocator.cpp
        src/VOG/Graphics/Vulkan/RenderCommandRecorder.cpp
        src/VOG/Graphics/Vulkan/RenderPass.cpp
        src/VOG/Graphics/Vulkan/Shader.cpp
        src/VOG/Graphics/Vulkan/TimelineSemaphore.cpp
        src/VOG/Graphics/GraphicsProvider.cpp
        src/VOG/Graphics/ResourceManager.cpp
        src/VOG/Graphics/ShaderProgramCache.hpp
        src/VOG/Graphics/ShaderProgramCache.cpp
)

target_link_libraries(VOG.Graphics
    PUBLIC
        VOG.Common
        Vulkan::Headers
        boost::container
    PRIVATE
        nlohmann_json::nlohmann_json
        vulkan-memory-allocator::vulkan-memory-allocator
        shaderc::shaderc
        spirv-cross::spirv-cross
)

if(APPLE)
    find_library(QUARTZ_CORE QuartzCore REQUIRED)
    find_library(APPKIT AppKit REQUIRED)
    list(APPEND LINK_PRIVATE ${QUARTZ_CORE} ${APPKIT})

    target_link_libraries(VOG.Graphics
        PRIVATE
            ${QUARTZ_CORE}
            ${APPKIT}
    )
    
    # For apple compile this file as an obj-c file.
    set_property(
        SOURCE src/VOG/Graphics/Vulkan/Attachment/CreateRenderSurface.cpp
        PROPERTY LANGUAGE OBJCXX
    )
endif()

add_subdirectory(Test)