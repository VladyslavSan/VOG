#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AttachmentInterface.hpp>
#include <VOG/Graphics/Vulkan/Attachment/Swapchain.hpp>
#include <VOG/Graphics/Vulkan/Containers.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/Limits.hpp>
#include <VOG/Graphics/Vulkan/MemoryAllocator.hpp>
#include <VOG/Graphics/Vulkan/Queue.hpp>
#include <VOG/Graphics/Vulkan/Semaphore.hpp>
#include <VOG/Graphics/Vulkan/Shader.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>
#include <VOG/Graphics/Vulkan/TimelineSemaphore.hpp>

#include <cstddef>
#include <span>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Buffer);
VOG_DECLARE_PTR(CommandBufferPool);
VOG_DECLARE_PTR(DescriptorHeap);
VOG_DECLARE_PTR(RenderBuffer);
VOG_DECLARE_PTR(Instance);
VOG_DECLARE_PTR(Texture);
VOG_DECLARE_PTR(UniformBlock);

class PhysicalDevice : private vk::raii::PhysicalDevice
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
     * Whether the transfer queue is taken from a family of its own when the device has one.
     * Off until an upload has run on a device with such a family (an NVIDIA or AMD GPU): the
     * cross-family path of CommandBufferRecorder::copyBuffer() and copyBufferToTexture() is only
     * checked by value (UploadBarriers.hpp), because no driver the tests run on has two families.
     * Then the transfer queue is the graphics family's queue.
     */
    static constexpr bool kDedicatedTransferFamily = false;

    /**
     * Picks the queue families from @p families, indexed as
     * vkGetPhysicalDeviceQueueFamilyProperties returns them. Graphics is the first family with the
     * graphics bit. Transfer is that same family, or with @p dedicatedTransfer the first family
     * that has the transfer bit and neither the graphics nor the compute bit (a DMA engine), if
     * there is one. A compute family is never picked for transfer.
     *
     * @throws std::runtime_error if no family has the graphics bit.
     */
    static QueueInfos selectQueueFamilies(std::span<const vk::QueueFamilyProperties> families,
                                          bool dedicatedTransfer);

    using vk::raii::PhysicalDevice::getMemoryProperties;
    using vk::raii::PhysicalDevice::getQueueFamilyProperties;
    using vk::raii::PhysicalDevice::getSurfaceCapabilitiesKHR;
    using vk::raii::PhysicalDevice::getSurfaceFormatsKHR;
    using vk::raii::PhysicalDevice::getSurfacePresentModesKHR;
    using vk::raii::PhysicalDevice::getSurfaceSupportKHR;
    using vk::raii::PhysicalDevice::operator*;

    /** For the raii objects that take a physical device; the base itself stays private. */
    const vk::raii::PhysicalDevice& raiiPhysicalDevice() const noexcept;

    /**
     * Instance this physical device was queried from. Held here, in the first base of Device, so
     * that it outlives the VkDevice and everything the device owns.
     */
    const InstancePtr instance;

    const QueueInfos queueInfos;
};

class Device
    : public PhysicalDevice
    , private vk::raii::Device
    , public std::enable_shared_from_this<Device>
{
    friend class Instance;

    Device(InstancePtr instance, vk::raii::PhysicalDevice physicalDevice);

public:
    Device(const Device&) = delete;
    Device(Device&&)      = delete;

    ~Device(); // NOLINT(bugprone-derived-method-shadowing-base-method)

    using vk::raii::Device::getBufferAddress;
    using vk::raii::Device::getDispatcher;
    using vk::raii::Device::operator*;
    using vk::raii::Device::releaseSwapchainImagesEXT;
    using vk::raii::Device::resetFences;
    using vk::raii::Device::waitForFences;
    using vk::raii::Device::waitSemaphores;

    /** For the raii objects that take a device; the base itself stays private. */
    const vk::raii::Device& raii() const noexcept;

    /**
     * Compiles a single shader stage from a SPIR-V binary.
     *
     * @param stage   Shader stage (vertex, fragment, etc.).
     * @param binary  SPIR-V bytecode words.
     *
     * @return Compiled shader object.
     */
    std::unique_ptr<Shader> createShader(Shader::ShadingStage           stage,
                                         std::span<const std::uint32_t> binary);

    /**
     * Creates a graphics pipeline for dynamic rendering into the attachments that
     * Parameters::renderingDescription describes.
     *
     * @param createInfo  Pipeline parameters.
     *
     * @return Compiled graphics pipeline.
     */
    std::unique_ptr<GraphicsPipeline>
    createGraphicsPipeline(GraphicsPipeline::Parameters createInfo);

    /**
     * Creates a command buffer pool for recording GPU commands for the graphics queue.
     *
     * @note Each pool should be used from a single thread at a time.
     *
     * @return Command buffer pool.
     */
    std::shared_ptr<CommandBufferPool> createCommandBufferPool();

    /**
     * Like createCommandBufferPool(), for the queue family @p queueFamilyIndex, which must be the
     * family of the graphics or of the transfer queue. Queue::submit takes only command buffers
     * of its own family.
     */
    std::shared_ptr<CommandBufferPool> createCommandBufferPool(std::uint32_t queueFamilyIndex);

    /**
     * Creates a swapchain for presenting rendered frames to a window surface.
     *
     * @param parameters  Swapchain creation parameters (surface, format, extent, etc.).
     *
     * @return Swapchain object.
     */
    std::unique_ptr<Swapchain> createSwapchain(const Swapchain::SwapchainParameters& parameters);

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
    std::unique_ptr<RenderBuffer> createRenderBuffer(AttachmentUsage         usage,
                                                     vk::Format              desiredFormat,
                                                     vk::Extent2D            extent,
                                                     SampleCount             sampleCount,
                                                     std::uint32_t           mipLevels,
                                                     std::uint32_t           arrayLevels,
                                                     vk::ImageTiling         imageTiling,
                                                     vk::ImageLayout         initialLayout,
                                                     vk::MemoryPropertyFlags memoryProperties);

    /**
     * Allocates a GPU buffer. The allocator picks the memory type for the buffer's usage.
     *
     * @param createInfo      Buffer size, usage flags, and sharing mode.
     * @param allocationInfo  Allocation flags; pass AllocationFlag::eHostSequentialWrite to be
     *                        able to map the buffer with Buffer::mapForWrite().
     *
     * @return Allocated buffer.
     */
    std::unique_ptr<Buffer>
    createBuffer(const vk::BufferCreateInfo&                  createInfo,
                 const MemoryAllocator::AllocationParameters& allocationInfo);

    /**
     * Creates the descriptor heaps that textures and uniform blocks take their slots from.
     *
     * @param resourceSlots  Number of images and uniform blocks the heap holds at once.
     * @param samplers       Samplers to write into the sampler heap; shaders read them by their
     *                       position in the span.
     *
     * @throws std::invalid_argument if @p resourceSlots is zero or @p samplers is empty.
     * @throws std::runtime_error if the device limits do not allow the heap.
     */
    DescriptorHeapPtr createDescriptorHeap(std::uint32_t                          resourceSlots,
                                           std::span<const vk::SamplerCreateInfo> samplers);

    /**
     * Creates a 2D, single-mip texture and writes its descriptor into a slot of @p heap. The
     * contents are undefined until CommandBufferRecorder::copyBufferToTexture() filled it.
     *
     * @throws std::runtime_error if @p heap is full.
     */
    std::unique_ptr<Texture>
    createTexture(const DescriptorHeapPtr& heap, vk::Extent2D extent, vk::Format format);

    /**
     * Creates an immutable block that holds a copy of @p bytes and writes its descriptor into a
     * slot of @p heap. The block is at least 16 bytes; a shader reading past @p bytes reads
     * zeros up to that size.
     *
     * @throws std::runtime_error if @p heap is full.
     */
    std::unique_ptr<UniformBlock> createUniformBlock(const DescriptorHeapPtr&   heap,
                                                     std::span<const std::byte> bytes);

    /**
     * Creates a binary semaphore. Pass it to Queue::submit and Swapchain::present: the command
     * buffers and the presentation retain it until the GPU is done with it.
     *
     * @return Binary semaphore.
     */
    SemaphorePtr createSemaphore();

    /**
     * Creates a timeline semaphore at value zero. Pass it to Queue::submit as a Queue::TimelineWait
     * or Queue::TimelineSignal: the command buffers retain it until the GPU is done with it.
     *
     * @return Timeline semaphore.
     */
    TimelineSemaphorePtr createTimelineSemaphore();

    /**
     * Hands out the graphics queue. The returned pointer shares ownership of this Device, so the
     * queue stays valid for as long as it is held.
     *
     * @return Graphics queue.
     */
    std::shared_ptr<const Queue> getGraphicsQueue();

    /**
     * Hands out the transfer queue. The returned pointer shares ownership of this Device, like
     * getGraphicsQueue().
     *
     * @return Transfer queue.
     */
    std::shared_ptr<const Queue> getTransferQueue();

private:
    /** Handed out through getGraphicsQueue() / getTransferQueue(). */
    const Vulkan::Queue mGraphicsQueue;
    const Vulkan::Queue mTransferQueue;

public:
    const vk::raii::PipelineCache pipelineCache;

private:
    /**
     * Private VMA wrapper. Holds Device& only so it cannot pin the device. Buffers retain a
     * DevicePtr and free through this allocator while the device lives.
     */
    std::unique_ptr<MemoryAllocator> mMemoryAllocator;
};
} // namespace VOG::Graphics::Vulkan
