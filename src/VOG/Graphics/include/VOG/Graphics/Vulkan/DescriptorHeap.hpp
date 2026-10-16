#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Buffer);
VOG_DECLARE_PTR(Device);
VOG_DECLARE_PTR(DescriptorHeap);

/**
 * Where the descriptors sit in the two heap buffers, worked out from the device limits. Pure, so
 * that tests can feed it the limits of any GPU.
 */
struct DescriptorHeapLayout
{
    /**
     * Bytes between two resource slots. Coupled to `descriptor_stride = 128` in every heap shader:
     * a shader reaches slot i at byte i * kSlotSize, and nothing here would notice a mismatch.
     */
    static constexpr vk::DeviceSize kSlotSize = 128u;

    struct Heap
    {
        /** Bytes of the heap buffer: the descriptors, then the reserved range. */
        vk::DeviceSize size = 0u;
        /** Where the driver's reserved range starts; the descriptors end there. */
        vk::DeviceSize reservedRangeOffset = 0u;
        vk::DeviceSize reservedRangeSize   = 0u;
        /** Alignment of the heap buffer's address. */
        vk::DeviceSize alignment = 0u;
    };

    /** Images and uniform buffers share one slot space: slot i is at byte i * kSlotSize. */
    Heap resources;
    /** Samplers are packed with samplerStride bytes between them. */
    Heap           samplers;
    vk::DeviceSize samplerStride        = 0u;
    vk::DeviceSize imageDescriptorSize  = 0u;
    vk::DeviceSize bufferDescriptorSize = 0u;

    /**
     * @throws std::invalid_argument if @p resourceSlots or @p samplerCount is zero.
     * @throws std::runtime_error if a descriptor does not fit a slot, or a heap would exceed the
     *         device maximum.
     */
    static DescriptorHeapLayout make(const vk::PhysicalDeviceDescriptorHeapPropertiesEXT& limits,
                                     std::uint32_t resourceSlots,
                                     std::uint32_t samplerCount);

    static constexpr vk::DeviceSize
    slotOffset(std::uint32_t index) noexcept
    {
        return index * kSlotSize;
    }
};

/**
 * The resource heap and the sampler heap of VK_EXT_descriptor_heap: two buffers that hold the
 * descriptors shaders read by index. Textures and uniform blocks take a Slot, which writes their
 * descriptor and gives the index back when the resource dies. The heap has a fixed number of
 * slots. Bind it with CommandBufferRecorder::bindDescriptorHeap(), which retains it.
 *
 * A Slot is freed only when its resource dies, and a resource that a command buffer recorded
 * (RenderingPass::useResource) dies only after that command buffer's fence has signaled. So a
 * slot is not rewritten while the GPU can still read it, provided every draw that reads a slot
 * recorded useResource for its resource; nothing checks that.
 *
 * Any thread: allocation and freeing take a lock.
 */
class DescriptorHeap : public std::enable_shared_from_this<DescriptorHeap>
{
    friend class Device;
    friend class CommandBufferRecorder;

    DescriptorHeap(DevicePtr                              device,
                   std::uint32_t                          resourceSlots,
                   std::span<const vk::SamplerCreateInfo> samplers);

public:
    /** One resource slot. Frees the index when destroyed; keeps the heap alive until then. */
    class Slot
    {
        friend class DescriptorHeap;

        Slot(DescriptorHeapPtr heap, std::uint32_t index) noexcept;

    public:
        ~Slot();

        Slot(Slot&& other) noexcept;
        Slot(const Slot&)            = delete;
        Slot& operator=(const Slot&) = delete;
        Slot& operator=(Slot&&)      = delete;

        std::uint32_t
        index() const noexcept
        {
            return mIndex;
        }

    private:
        DescriptorHeapPtr mHeap;
        std::uint32_t     mIndex;
    };

    ~DescriptorHeap();

    DescriptorHeap(const DescriptorHeap&)            = delete;
    DescriptorHeap& operator=(const DescriptorHeap&) = delete;

private:
    /** @throws std::runtime_error if every slot is taken. */
    [[nodiscard]] Slot allocate();

    /** Writes the sampled-image descriptor of the image that @p view describes into @p slot. */
    void writeImage(const Slot& slot, const vk::ImageViewCreateInfo& view, vk::ImageLayout layout);

    /** Writes the uniform-buffer descriptor of @p range into @p slot. */
    void writeUniformBuffer(const Slot& slot, const vk::DeviceAddressRangeKHR& range);

    void release(std::uint32_t index) noexcept;

    /** Writes @p descriptor into the resource heap at the slot of @p slot. */
    void writeResource(const Slot& slot, const vk::ResourceDescriptorInfoEXT& descriptor);

    const DevicePtr            mDevice;
    const DescriptorHeapLayout mLayout;
    const BufferPtr            mResourceHeap;
    const BufferPtr            mSamplerHeap;

    /** What the bind commands take; the addresses are fixed for the heap's life. */
    const vk::BindHeapInfoEXT mResourceBind;
    const vk::BindHeapInfoEXT mSamplerBind;

    std::mutex                 mMutex;
    std::vector<std::uint32_t> mFree;
};
} // namespace VOG::Graphics::Vulkan
