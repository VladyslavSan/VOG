#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AttachmentInterface.hpp>
#include <VOG/Graphics/Vulkan/Attachment/Swapchain.hpp>
#include <VOG/Graphics/Vulkan/Containers.hpp>
#include <VOG/Graphics/Vulkan/DescriptorAllocator.hpp>
#include <VOG/Graphics/Vulkan/Fence.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/Limits.hpp>
#include <VOG/Graphics/Vulkan/MemoryAllocator.hpp>
#include <VOG/Graphics/Vulkan/Queue.hpp>
#include <VOG/Graphics/Vulkan/Shader.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>
#include <VOG/Graphics/Vulkan/TimelineSemaphore.hpp>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Buffer);
VOG_DECLARE_PTR(CommandBufferPool);
VOG_DECLARE_PTR(Framebuffer);
VOG_DECLARE_PTR(RenderBuffer);
VOG_DECLARE_PTR(RenderPass);
VOG_DECLARE_PTR(FencePool);
VOG_DECLARE_PTR(Instance);

class PhysicalDevice : public vk::raii::PhysicalDevice
{
public:
    struct QueueFamilyInfo
    {
        std::uint32_t             familyIndex = 0u;
        vk::QueueFamilyProperties familyProperties{};
    };

    struct QueueInfos
    {
        QueueFamilyInfo graphics;
        QueueFamilyInfo transfer;
    };

    PhysicalDevice(vk::raii::PhysicalDevice physicalDevice);

    const QueueInfos queueInfos;
};

class Device
    : public PhysicalDevice
    , public vk::raii::Device
    , public std::enable_shared_from_this<Device>
{
    friend class Instance;

    Device(InstancePtr instance, vk::raii::PhysicalDevice physicalDevice);
    void init();

public:
    ~Device(); // NOLINT(bugprone-derived-method-shadowing-base-method)

    const PhysicalDevice& getPhysicalDevice() const;

    ShaderPtr createShader(Shader::ShadingStage stage, std::span<const std::uint32_t> binary);

    Fence             createFence();
    TimelineSemaphore createTimelineSemaphore();

    std::shared_ptr<RenderPass>
    createRenderPass(const StaticVector<vk::AttachmentDescription, Limits::gMaxNumAttachments - 1u>&
                                                      colorAttachments,
                     const vk::AttachmentDescription& depthStencil);
    std::shared_ptr<CommandBufferPool> createCommandBufferPool();
    std::shared_ptr<Framebuffer>       createFramebuffer(
        std::shared_ptr<RenderPass>                                           renderPass,
        StaticVector<AttachmentInterfacePtr, Limits::gMaxNumAttachments - 1u> colorAttachments,
        AttachmentInterfacePtr depthStencilAttachment);
    std::shared_ptr<ShaderProgram> createShaderProgram(ShaderProgram::ShadingStages stages);
    std::shared_ptr<GraphicsPipeline>
    createGraphicsPipeline(GraphicsPipeline::ParametersLegacy createInfo);
    std::shared_ptr<GraphicsPipeline>
                                  createGraphicsPipeline(GraphicsPipeline::Parameters createInfo);
    std::shared_ptr<Swapchain>    createSwapchain(const Swapchain::SwapchainParameters& parameters);
    std::shared_ptr<RenderBuffer> createRenderBuffer(AttachmentUsage         usage,
                                                     vk::Format              desiredFormat,
                                                     vk::Extent2D            extent,
                                                     SampleCount             sampleCount,
                                                     std::uint32_t           mipLevels,
                                                     std::uint32_t           arrayLevels,
                                                     vk::ImageTiling         imageTiling,
                                                     vk::ImageLayout         initialLayout,
                                                     vk::MemoryPropertyFlags memoryProperties);
    std::unique_ptr<DescriptorAllocator>
    createDescriptorAllocator(const DescriptorAllocator::ConstructionParameters& params);
    std::unique_ptr<Buffer>
    createBuffer(const vk::BufferCreateInfo&                  createInfo,
                 const MemoryAllocator::AllocationParameters& allocationInfo);

    using vk::raii::Device::operator*;
    using vk::raii::Device::getDispatcher;

    const FencePoolPtr& getFencePool() const;

    const InstancePtr             instance;
    const Vulkan::Queue           graphicsQueue;
    const Vulkan::Queue           transferQueue;
    const vk::raii::PipelineCache pipelineCache;

private:
    FencePoolPtr       mFencePool;
    MemoryAllocatorPtr mMemoryAllocator;
};
} // namespace VOG::Graphics::Vulkan
