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

    PhysicalDevice(InstancePtr instance, vk::raii::PhysicalDevice physicalDevice);

    /**
     * Instance this physical device was queried from. Held here, in the first base of Device, so
     * that it outlives the VkDevice and everything the device owns.
     */
    const InstancePtr instance;

    const QueueInfos queueInfos;
};

class Device
    : public PhysicalDevice
    , public vk::raii::Device
    , public std::enable_shared_from_this<Device>
{
    friend class Instance;

    Device(InstancePtr instance, vk::raii::PhysicalDevice physicalDevice);

public:
    ~Device(); // NOLINT(bugprone-derived-method-shadowing-base-method)

    using vk::raii::Device::operator*;
    using vk::raii::Device::getDispatcher;

    /**
     * @return The device's shared fence pool.
     */
    FencePool& getFencePool() const;

    /**
     * Compiles a single shader stage from a SPIR-V binary.
     *
     * @param stage   Shader stage (vertex, fragment, etc.).
     * @param binary  SPIR-V bytecode words.
     *
     * @return Compiled shader object.
     */
    ShaderPtr createShader(Shader::ShadingStage stage, std::span<const std::uint32_t> binary);

    /**
     * Links vertex and fragment shaders into a shader program.
     * Descriptor set layouts and push constant ranges are derived through shader reflection.
     *
     * @param stages  Vertex and fragment shader stage descriptors.
     *
     * @return Linked shader program with a reflected pipeline layout.
     */
    std::shared_ptr<ShaderProgram> createShaderProgram(ShaderProgram::ShadingStages stages);

    /**
     * Creates a graphics pipeline using a legacy render-pass-based description.
     *
     * @param createInfo  Pipeline parameters including the render pass object.
     *
     * @return Compiled graphics pipeline.
     */
    std::shared_ptr<GraphicsPipeline>
    createGraphicsPipeline(GraphicsPipeline::ParametersLegacy createInfo);

    /**
     * Creates a graphics pipeline from a renderpass description.
     *
     * @param createInfo  Pipeline parameters with a renderpass description.
     *
     * @return Compiled graphics pipeline.
     */
    std::shared_ptr<GraphicsPipeline>
    createGraphicsPipeline(GraphicsPipeline::Parameters createInfo);

    /**
     * Creates a render pass from attachment descriptions.
     *
     * @param colorAttachments  Descriptions of color attachments.
     * @param depthStencil      Description of the depth/stencil attachment.
     *
     * @return Render pass object.
     */
    std::shared_ptr<RenderPass>
    createRenderPass(const StaticVector<vk::AttachmentDescription, Limits::gMaxNumAttachments - 1u>&
                                                      colorAttachments,
                     const vk::AttachmentDescription& depthStencil);

    /**
     * Creates a framebuffer by binding attachments to a render pass.
     *
     * @param renderPass              Render pass the framebuffer is compatible with.
     * @param colorAttachments        Color attachment views.
     * @param depthStencilAttachment  Depth/stencil attachment view.
     *
     * @return Framebuffer object.
     */
    std::shared_ptr<Framebuffer> createFramebuffer(
        std::shared_ptr<RenderPass>                                           renderPass,
        StaticVector<AttachmentInterfacePtr, Limits::gMaxNumAttachments - 1u> colorAttachments,
        AttachmentInterfacePtr depthStencilAttachment);

    /**
     * Creates a command buffer pool for recording GPU commands.
     *
     * @note Each pool should be used from a single thread at a time.
     *
     * @return Command buffer pool.
     */
    std::shared_ptr<CommandBufferPool> createCommandBufferPool();

    /**
     * Creates a swapchain for presenting rendered frames to a window surface.
     *
     * @param parameters  Swapchain creation parameters (surface, format, extent, etc.).
     *
     * @return Swapchain object.
     */
    std::shared_ptr<Swapchain> createSwapchain(const Swapchain::SwapchainParameters& parameters);

    /**
     * Creates an image-backed render attachment (color or depth/stencil).
     *
     * @param usage             Intended attachment usage.
     * @param desiredFormat     Requested image format.
     * @param extent            Width and height in pixels.
     * @param sampleCount       MSAA sample count.
     * @param mipLevels         Number of mip levels.
     * @param arrayLevels       Number of array layers.
     * @param imageTiling       Tiling mode (optimal or linear).
     * @param initialLayout     Initial image layout.
     * @param memoryProperties  Required memory property flags.
     *
     * @return Render buffer object.
     */
    std::shared_ptr<RenderBuffer> createRenderBuffer(AttachmentUsage         usage,
                                                     vk::Format              desiredFormat,
                                                     vk::Extent2D            extent,
                                                     SampleCount             sampleCount,
                                                     std::uint32_t           mipLevels,
                                                     std::uint32_t           arrayLevels,
                                                     vk::ImageTiling         imageTiling,
                                                     vk::ImageLayout         initialLayout,
                                                     vk::MemoryPropertyFlags memoryProperties);

    /**
     * Creates a descriptor allocator pre-configured with the given pool sizes.
     *
     * @param params  Pool sizes and max set count.
     *
     * @return Descriptor allocator.
     */
    std::unique_ptr<DescriptorAllocator>
    createDescriptorAllocator(const DescriptorAllocator::ConstructionParameters& params);

    /**
     * Allocates a GPU buffer with the specified usage and memory properties.
     *
     * @param createInfo      Buffer size, usage flags, and sharing mode.
     * @param allocationInfo  VMA memory usage and required/preferred memory flags.
     *
     * @return Allocated buffer.
     */
    std::unique_ptr<Buffer>
    createBuffer(const vk::BufferCreateInfo&                  createInfo,
                 const MemoryAllocator::AllocationParameters& allocationInfo);

    /**
     * Creates a binary fence for CPU/GPU synchronization.
     * The fence starts in the unsignaled state.
     *
     * @return Fence object.
     */
    Fence createFence();

    /**
     * Creates a timeline semaphore for ordered multi-stage GPU synchronization.
     * The counter starts at zero and increments on each signal operation.
     *
     * @return Timeline semaphore.
     */
    TimelineSemaphore createTimelineSemaphore();

    const Vulkan::Queue           graphicsQueue;
    const Vulkan::Queue           transferQueue;
    const vk::raii::PipelineCache pipelineCache;

private:
    /**
     * Services owned by the device. They keep a Device& backref rather than a DevicePtr, and
     * must never cache anything that owns one, otherwise the device can never be destroyed.
     * Resources handed out to callers retain a DevicePtr and so keep the VkDevice alive.
     */
    std::unique_ptr<FencePool>       mFencePool;
    std::unique_ptr<MemoryAllocator> mMemoryAllocator;
};
} // namespace VOG::Graphics::Vulkan
