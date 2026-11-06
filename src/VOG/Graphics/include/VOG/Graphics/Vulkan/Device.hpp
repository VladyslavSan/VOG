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
#include <memory>
#include <span>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Buffer);
VOG_DECLARE_PTR(CommandBufferPool);
VOG_DECLARE_PTR(DescriptorHeap);
VOG_DECLARE_PTR(HeapBuffer);
VOG_DECLARE_PTR(Instance);
VOG_DECLARE_PTR(RenderTarget);
VOG_DECLARE_PTR(Texture);

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

    /**
     * Picks the family to present from: the graphics family if it presents, else the transfer
     * family. These are the only families that makeDevice creates a queue for; presenting from any
     * other would ask the device for a queue it does not have.
     *
     * @param infos     The families that have a queue.
     * @param presents  Per family, indexed as vkGetPhysicalDeviceQueueFamilyProperties returns
     *                  them, whether it presents to the surface
     *                  (vkGetPhysicalDeviceSurfaceSupportKHR).
     *
     * @throws std::runtime_error if neither family presents.
     */
    static std::uint32_t selectPresentFamily(const QueueInfos&           infos,
                                             std::span<const vk::Bool32> presents);

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

    /** The limits of the physical device, read once. */
    const vk::PhysicalDeviceLimits limits;

    /** The limits of VK_EXT_descriptor_heap, read once. */
    const vk::PhysicalDeviceDescriptorHeapPropertiesEXT descriptorHeapProperties;
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
    [[nodiscard]] std::unique_ptr<Shader> createShader(Shader::ShadingStage           stage,
                                                       std::span<const std::uint32_t> binary);

    /**
     * Creates a graphics pipeline for dynamic rendering into the attachments that
     * Parameters::renderingDescription describes.
     *
     * @param createInfo  Pipeline parameters.
     *
     * @return Compiled graphics pipeline.
     */
    [[nodiscard]] std::unique_ptr<GraphicsPipeline>
    createGraphicsPipeline(GraphicsPipeline::Parameters createInfo);

    /**
     * Creates a command buffer pool for recording GPU commands for the graphics queue.
     *
     * @note Each pool should be used from a single thread at a time.
     *
     * @return Command buffer pool.
     */
    [[nodiscard]] std::shared_ptr<CommandBufferPool> createCommandBufferPool();

    /**
     * Like createCommandBufferPool(), for the queue family @p queueFamilyIndex, which must be the
     * family of the graphics or of the transfer queue. Queue::submit takes only command buffers
     * of its own family.
     */
    [[nodiscard]] std::shared_ptr<CommandBufferPool>
    createCommandBufferPool(std::uint32_t queueFamilyIndex);

    /**
     * Creates a swapchain for presenting rendered frames to a window surface.
     *
     * @param parameters  Swapchain creation parameters (surface, format, extent, etc.).
     *
     * @return Swapchain object.
     */
    [[nodiscard]] std::unique_ptr<Swapchain>
    createSwapchain(const Swapchain::SwapchainParameters& parameters);

    /**
     * Creates a 2D, single-mip, single-sample image that command buffers render into, in memory
     * that is preferably device-local. The image is undefined until a barrier moves it to the
     * layout of its first use.
     *
     * @param extent  Width and height in pixels.
     * @param format  Image format. A depth format gets a depth view, any other a color view. A
     *                format with a stencil aspect is refused: the recorder renders depth-only
     *                depth attachments. Whether the device renders into the format with this
     *                @p usage is not checked.
     * @param usage   How the image is used. Needs eColorAttachment or eDepthStencilAttachment;
     *                add eTransferSrc to copy a color target out with
     *                CommandBufferRecorder::copyImageToBuffer(). The target has no heap slot, so
     *                no shader samples it.
     *
     * @throws std::invalid_argument if a side of @p extent is zero or exceeds maxImageDimension2D
     *         of the device, @p usage names no attachment, or @p format has a stencil aspect.
     * @throws std::runtime_error if the image cannot be created.
     */
    [[nodiscard]] std::unique_ptr<RenderTarget>
    createRenderTarget(vk::Extent2D extent, vk::Format format, vk::ImageUsageFlags usage);

    /**
     * Allocates a GPU buffer. The allocator picks the memory type for the buffer's usage.
     *
     * @param createInfo      Buffer size, usage flags, and sharing mode.
     * @param allocationInfo  Allocation flags; pass AllocationFlag::eHostSequentialWrite to be
     *                        able to map the buffer with Buffer::mapForWrite().
     *
     * @return Allocated buffer.
     */
    [[nodiscard]] std::unique_ptr<Buffer>
    createBuffer(const vk::BufferCreateInfo&                  createInfo,
                 const MemoryAllocator::AllocationParameters& allocationInfo);

    /**
     * Creates the descriptor heaps that textures and heap buffers take their slots from.
     *
     * @param resourceSlots  Number of images and heap buffers the heap holds at once.
     * @param samplers       Samplers to write into the sampler heap; shaders read them by their
     *                       position in the span.
     *
     * @throws std::invalid_argument if @p resourceSlots is zero or @p samplers is empty.
     * @throws std::runtime_error if the device limits do not allow the heap.
     */
    [[nodiscard]] DescriptorHeapPtr
    createDescriptorHeap(std::uint32_t                          resourceSlots,
                         std::span<const vk::SamplerCreateInfo> samplers);

    /**
     * Creates a 2D, single-mip texture and writes its descriptor into a slot of @p heap. The
     * contents are undefined until CommandBufferRecorder::copyBufferToTexture() filled it.
     *
     * @throws std::invalid_argument if a side of @p extent exceeds maxImageDimension2D of the
     *         device. This is a bound on the extent only: the format must be one that the
     *         device samples and copies into, which nothing here checks.
     * @throws std::runtime_error if @p heap is full or the image cannot be created.
     */
    [[nodiscard]] std::unique_ptr<Texture>
    createTexture(const DescriptorHeapPtr& heap, vk::Extent2D extent, vk::Format format);

    /**
     * Creates a host-written buffer of @p size bytes and writes its uniform-buffer descriptor into
     * a slot of @p heap. The buffer is at least 16 bytes and zero until written, through
     * HeapBuffer::mapForWrite(); the descriptor is written once, here.
     *
     * @throws std::invalid_argument if @p size, or @p size rounded up to 16 bytes, exceeds
     *         maxUniformBufferRange of the device.
     * @throws std::runtime_error if @p heap is full, or the allocator places the buffer at an
     *         address off the device's uniform buffer offset alignment.
     */
    [[nodiscard]] std::unique_ptr<HeapBuffer> createHeapBuffer(const DescriptorHeapPtr& heap,
                                                               std::size_t              size);

    /**
     * Creates a binary semaphore. Pass it to Queue::submit and Swapchain::present: the command
     * buffers and the presentation retain it until the GPU is done with it.
     *
     * @return Binary semaphore.
     */
    [[nodiscard]] SemaphorePtr createSemaphore();

    /**
     * Creates a timeline semaphore at value zero. Pass it to Queue::submit as a Queue::TimelineWait
     * or Queue::TimelineSignal: the command buffers retain it until the GPU is done with it.
     *
     * @return Timeline semaphore.
     */
    [[nodiscard]] TimelineSemaphorePtr createTimelineSemaphore();

    /**
     * Hands out the graphics queue. The returned pointer shares ownership of this Device, so the
     * queue stays valid for as long as it is held.
     *
     * @return Graphics queue.
     */
    [[nodiscard]] std::shared_ptr<const Queue> getGraphicsQueue();

    /**
     * Hands out the transfer queue. The returned pointer shares ownership of this Device, like
     * getGraphicsQueue().
     *
     * @return Transfer queue.
     */
    [[nodiscard]] std::shared_ptr<const Queue> getTransferQueue();

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
