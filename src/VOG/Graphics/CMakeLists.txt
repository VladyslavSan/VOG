cmake_minimum_required(VERSION 3.10.0)

find_package(VulkanHeaders REQUIRED)
find_package(vulkan-memory-allocator REQUIRED)
find_package(spirv-cross REQUIRED)
find_package(glslang REQUIRED)

find_package(Boost COMPONENTS container REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)

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
        include/VOG/Graphics/Config/VmaConfig.hpp
        include/VOG/Graphics/Config/VulkanConfig.hpp
        include/VOG/Graphics/Frame/FrameObjectManager.hpp
        include/VOG/Graphics/Frame/FrameObjects.hpp
        include/VOG/Graphics/Vulkan/Attachment/AttachmentInterface.hpp
        include/VOG/Graphics/Vulkan/Attachment/RenderBuffer.hpp
        include/VOG/Graphics/Vulkan/Attachment/Swapchain.hpp
        include/VOG/Graphics/Vulkan/Buffer.hpp
        include/VOG/Graphics/Vulkan/CommandBuffer.hpp
        include/VOG/Graphics/Vulkan/CommandBufferPool.hpp
        include/VOG/Graphics/Vulkan/CommandBufferRecorder.hpp
        include/VOG/Graphics/Vulkan/Common.hpp
        include/VOG/Graphics/Vulkan/Containers.hpp
        include/VOG/Graphics/Vulkan/DescriptorAllocator.hpp
        include/VOG/Graphics/Vulkan/Device.hpp
        include/VOG/Graphics/Vulkan/Fence.hpp
        include/VOG/Graphics/Vulkan/FencePool.hpp
        include/VOG/Graphics/Vulkan/Framebuffer.hpp
        include/VOG/Graphics/Vulkan/GraphicsPipeline.hpp
        include/VOG/Graphics/Vulkan/Instance.hpp
        include/VOG/Graphics/Vulkan/Limits.hpp
        include/VOG/Graphics/Vulkan/MemoryAllocator.hpp
        include/VOG/Graphics/Vulkan/Queue.hpp
        include/VOG/Graphics/Vulkan/RenderPass.hpp
        include/VOG/Graphics/Vulkan/Shader.hpp
        include/VOG/Graphics/Vulkan/ShaderProgram.hpp
        include/VOG/Graphics/Vulkan/TimelineSemaphore.hpp
        include/VOG/Graphics/ShaderProgramCache.hpp
        include/VOG/Graphics/Typedefs.hpp

    PRIVATE
        src/VOG/Graphics/Frame/FrameObjectManager.cpp
        src/VOG/Graphics/Frame/FrameObjects.cpp
        src/VOG/Graphics/Vulkan/Attachment/Converters.hpp
        src/VOG/Graphics/Vulkan/Attachment/CreateRenderSurface.hpp
        src/VOG/Graphics/Vulkan/Attachment/CreateRenderSurface.cpp
        src/VOG/Graphics/Vulkan/Attachment/RenderBuffer.cpp
        src/VOG/Graphics/Vulkan/Attachment/Swapchain.cpp
        src/VOG/Graphics/Vulkan/Buffer.cpp
        src/VOG/Graphics/Vulkan/CommandBuffer.cpp
        src/VOG/Graphics/Vulkan/CommandBufferPool.cpp
        src/VOG/Graphics/Vulkan/CommandBufferRecorder.cpp
        src/VOG/Graphics/Vulkan/Common.cpp
        src/VOG/Graphics/Vulkan/DescriptorAllocator.cpp
        src/VOG/Graphics/Vulkan/Device.cpp
        src/VOG/Graphics/Vulkan/Fence.cpp
        src/VOG/Graphics/Vulkan/FencePool.cpp
        src/VOG/Graphics/Vulkan/Framebuffer.cpp
        src/VOG/Graphics/Vulkan/GraphicsPipeline.cpp
        src/VOG/Graphics/Vulkan/Instance.cpp
        src/VOG/Graphics/Vulkan/MemoryAllocator.cpp
        src/VOG/Graphics/Vulkan/MemoryAllocatorVmaImpl.cpp
        src/VOG/Graphics/Vulkan/RenderPass.cpp
        src/VOG/Graphics/Vulkan/Shader.cpp
        src/VOG/Graphics/Vulkan/ShaderProgram.cpp
        src/VOG/Graphics/Vulkan/TimelineSemaphore.cpp
        src/VOG/Graphics/ShaderProgramCache.cpp
)

target_link_libraries(VOG.Graphics
    PUBLIC
        VOG.Common
        Vulkan::Headers
        vulkan-memory-allocator::vulkan-memory-allocator
        Boost::container
    PRIVATE
        nlohmann_json::nlohmann_json                                                                                
        spirv-cross::spirv-cross
        spdlog::spdlog
        glslang::glslang
        glslang::SPIRV
)

if (APPLE)
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
endif ()

add_subdirectory(Test)