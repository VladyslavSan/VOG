cmake_minimum_required(VERSION 3.10.0)

find_package(VulkanHeaders REQUIRED)
find_package(vulkan-memory-allocator REQUIRED)
find_package(Boost COMPONENTS container REQUIRED)
find_package(spirv-cross REQUIRED)
find_package(shaderc REQUIRED)

utility_declare_project(VOG.Graphics)

set(GRAPHICS_HEADERS
    ${INTERFACE_DIR}/VOG/Graphics/Config/VulkanConfig.hpp
    ${INTERFACE_DIR}/VOG/Graphics/Frame/FrameObjectManager.hpp
    ${INTERFACE_DIR}/VOG/Graphics/Frame/FrameObjects.hpp
    ${INTERFACE_DIR}/VOG/Graphics/Frame/ThreadObjects.hpp
    ${INTERFACE_DIR}/VOG/Graphics/Vulkan/Attachment/AttachmentInterface.hpp
    ${INTERFACE_DIR}/VOG/Graphics/Vulkan/Attachment/RenderBuffer.hpp
    ${INTERFACE_DIR}/VOG/Graphics/Vulkan/Attachment/Swapchain.hpp
    ${INTERFACE_DIR}/VOG/Graphics/Vulkan/CommandBuffer.hpp
    ${INTERFACE_DIR}/VOG/Graphics/Vulkan/CommandBufferPool.hpp
    ${INTERFACE_DIR}/VOG/Graphics/Vulkan/Device.hpp
    ${INTERFACE_DIR}/VOG/Graphics/Vulkan/Framebuffer.hpp
    ${INTERFACE_DIR}/VOG/Graphics/Vulkan/GraphicsPipeline.hpp
    ${INTERFACE_DIR}/VOG/Graphics/Vulkan/MemoryAllocator.hpp
    ${INTERFACE_DIR}/VOG/Graphics/Vulkan/Queue.hpp
    ${INTERFACE_DIR}/VOG/Graphics/Vulkan/RenderCommandRecorder.hpp
    ${INTERFACE_DIR}/VOG/Graphics/Vulkan/RenderPass.hpp
    ${INTERFACE_DIR}/VOG/Graphics/Vulkan/Shader.hpp
    ${INTERFACE_DIR}/VOG/Graphics/Vulkan/ShaderProgram.hpp
    ${INTERFACE_DIR}/VOG/Graphics/Vulkan/TimelineSemaphore.hpp
    ${INTERFACE_DIR}/VOG/Graphics/GraphicsProvider.hpp
    ${INTERFACE_DIR}/VOG/Graphics/RenderStates.hpp
    ${INTERFACE_DIR}/VOG/Graphics/ResourceManager.hpp
    ${INTERFACE_DIR}/VOG/Graphics/Typedefs.hpp
)

set(GRAPHICS_SOURCE
    ${IMPLEMENTATION_DIR}/VOG/Graphics/Config/VMAConfig.hpp
    ${IMPLEMENTATION_DIR}/VOG/Graphics/Frame/FrameObjectManager.cpp
    ${IMPLEMENTATION_DIR}/VOG/Graphics/Frame/FrameObjects.cpp
    ${IMPLEMENTATION_DIR}/VOG/Graphics/Frame/ThreadObjects.cpp
    ${IMPLEMENTATION_DIR}/VOG/Graphics/Vulkan/Attachment/Converters.hpp
    ${IMPLEMENTATION_DIR}/VOG/Graphics/Vulkan/Attachment/CreateRenderSurface.hpp
    ${IMPLEMENTATION_DIR}/VOG/Graphics/Vulkan/Attachment/CreateRenderSurface.cpp
    ${IMPLEMENTATION_DIR}/VOG/Graphics/Vulkan/Attachment/RenderBuffer.cpp
    ${IMPLEMENTATION_DIR}/VOG/Graphics/Vulkan/Attachment/Swapchain.cpp
    ${IMPLEMENTATION_DIR}/VOG/Graphics/Vulkan/CommandBuffer.cpp
    ${IMPLEMENTATION_DIR}/VOG/Graphics/Vulkan/CommandBufferPool.cpp
    ${IMPLEMENTATION_DIR}/VOG/Graphics/Vulkan/Framebuffer.cpp
    ${IMPLEMENTATION_DIR}/VOG/Graphics/Vulkan/GraphicsPipeline.cpp
    ${IMPLEMENTATION_DIR}/VOG/Graphics/Vulkan/MemoryAllocator.cpp
    ${IMPLEMENTATION_DIR}/VOG/Graphics/Vulkan/RenderCommandRecorder.cpp
    ${IMPLEMENTATION_DIR}/VOG/Graphics/Vulkan/RenderPass.cpp
    ${IMPLEMENTATION_DIR}/VOG/Graphics/Vulkan/Shader.cpp
    ${IMPLEMENTATION_DIR}/VOG/Graphics/Vulkan/TimelineSemaphore.cpp
    ${IMPLEMENTATION_DIR}/VOG/Graphics/GraphicsProvider.cpp
    ${IMPLEMENTATION_DIR}/VOG/Graphics/ResourceManager.cpp
)

set(GRAPHICS_RESOURCES
    ${RESOURCES_DIR}/shaders/src/BasicTriangle.frag
    ${RESOURCES_DIR}/shaders/src/BasicTriangle.vert
    ${RESOURCES_DIR}/shaders/src/TestVertexAttribute.vert
    ${RESOURCES_DIR}/shaders/src/TestVertexAttribute.frag
)

set(LINK_PUBLIC
    VOG.Common
    Vulkan::Headers
    Boost::container
)

set(LINK_PRIVATE
    vulkan-memory-allocator::vulkan-memory-allocator
    shaderc::shaderc
    # spirv-cross-reflect doesn't work without spirv-cross-core
    spirv-cross::spirv-cross-core
    spirv-cross::spirv-cross-reflect)

if(APPLE)
    find_library(QUARTZ_CORE QuartzCore REQUIRED)
    find_library(APPKIT AppKit REQUIRED)
    list(APPEND LINK_PRIVATE ${QUARTZ_CORE} ${APPKIT})
    
    # For apple compile this file as an obj-c file.
    set_property(SOURCE src/VOG/Graphics/Vulkan/Attachment/CreateRenderSurface.cpp PROPERTY LANGUAGE OBJCXX)
endif()

utility_add_target(
    LIBRARY
    LINK_PUBLIC ${LINK_PUBLIC}
    LINK_PRIVATE ${LINK_PRIVATE}
    FOLDER VOG
    HEADERS ${GRAPHICS_HEADERS}
    SOURCES ${GRAPHICS_SOURCE}
    RESOURCES ${GRAPHICS_RESOURCES}
)

add_subdirectory(Test)