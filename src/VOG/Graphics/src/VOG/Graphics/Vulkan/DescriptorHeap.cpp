#include "VOG/Graphics/Vulkan/DescriptorHeap.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>

#include <format>
#include <numeric>
#include <stdexcept>

namespace VOG::Graphics::Vulkan
{
namespace
{
/** A descriptor of @p size bytes at @p alignment fits a slot, and every slot start keeps it. */
constexpr bool
fitsSlot(vk::DeviceSize size, vk::DeviceSize alignment)
{
    return alignment != 0u && DescriptorHeapLayout::kSlotSize % alignment == 0u &&
           size <= DescriptorHeapLayout::kSlotSize;
}

void
requireFitsSlot(const char* kind, vk::DeviceSize size, vk::DeviceSize alignment)
{
    if (!fitsSlot(size, alignment))
    {
        throw std::runtime_error{
            std::format("A {} descriptor of {} bytes aligned to {} does not fit a heap slot of {} "
                        "bytes.",
                        kind,
                        size,
                        alignment,
                        DescriptorHeapLayout::kSlotSize)};
    }
}

void
requireWithinMax(const char* kind, vk::DeviceSize size, vk::DeviceSize max)
{
    if (size > max)
    {
        throw std::runtime_error{std::format(
            "The {} heap would need {} bytes, but the device allows {}.", kind, size, max)};
    }
}

/** A heap buffer: what the bind commands, the descriptor writes and the shaders read. */
BufferPtr
makeHeapBuffer(Device& device, const DescriptorHeapLayout::Heap& heap)
{
    BufferPtr buffer = device.createBuffer(
        {.size  = heap.size,
         .usage = vk::BufferUsageFlagBits::eDescriptorHeapEXT |
                  vk::BufferUsageFlagBits::eShaderDeviceAddress},
        {.flags = AllocationFlag::eHostSequentialWrite, .alignment = heap.alignment});

    // Untested: a driver with a heap alignment above what its memory gives back reaches it.
    if (buffer->deviceAddress() % heap.alignment != 0u)
    {
        throw std::runtime_error{std::format(
            "The heap buffer got an address that is not aligned to {} bytes.", heap.alignment)};
    }

    return buffer;
}

vk::BindHeapInfoEXT
bindInfo(const Buffer& buffer, const DescriptorHeapLayout::Heap& heap)
{
    return {.heapRange           = {.address = buffer.deviceAddress(), .size = heap.size},
            .reservedRangeOffset = heap.reservedRangeOffset,
            .reservedRangeSize   = heap.reservedRangeSize};
}
} // namespace

DescriptorHeapLayout
DescriptorHeapLayout::make(const vk::PhysicalDeviceDescriptorHeapPropertiesEXT& limits,
                           // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
                           std::uint32_t resourceSlots,
                           std::uint32_t samplerCount)
{
    if (resourceSlots == 0u || samplerCount == 0u)
    {
        throw std::invalid_argument{
            std::format("A descriptor heap needs at least one slot and one sampler; got {} and {}.",
                        resourceSlots,
                        samplerCount)};
    }
    requireFitsSlot("image", limits.imageDescriptorSize, limits.imageDescriptorAlignment);
    requireFitsSlot("buffer", limits.bufferDescriptorSize, limits.bufferDescriptorAlignment);
    if (limits.samplerDescriptorAlignment == 0u ||
        limits.samplerDescriptorSize % limits.samplerDescriptorAlignment != 0u)
    {
        throw std::runtime_error{std::format(
            "A sampler descriptor of {} bytes is not a multiple of its alignment of {}.",
            limits.samplerDescriptorSize,
            limits.samplerDescriptorAlignment)};
    }

    DescriptorHeapLayout layout;
    layout.imageDescriptorSize  = limits.imageDescriptorSize;
    layout.bufferDescriptorSize = limits.bufferDescriptorSize;
    layout.samplerStride        = limits.samplerDescriptorSize;

    // The reserved range follows the last slot. Slots are kSlotSize apart, which both descriptor
    // alignments divide, so the range starts aligned for either.
    layout.resources.reservedRangeOffset = slotOffset(resourceSlots);
    layout.resources.reservedRangeSize   = limits.minResourceHeapReservedRange;
    layout.resources.size =
        layout.resources.reservedRangeOffset + limits.minResourceHeapReservedRange;
    layout.resources.alignment = limits.resourceHeapAlignment;
    requireWithinMax("resource", layout.resources.size, limits.maxResourceHeapSize);

    // A multiple of the descriptor size, which the check above made a multiple of its alignment.
    layout.samplers.reservedRangeOffset = samplerCount * limits.samplerDescriptorSize;
    layout.samplers.reservedRangeSize   = limits.minSamplerHeapReservedRange;
    layout.samplers.size = layout.samplers.reservedRangeOffset + limits.minSamplerHeapReservedRange;
    layout.samplers.alignment = limits.samplerHeapAlignment;
    requireWithinMax("sampler", layout.samplers.size, limits.maxSamplerHeapSize);

    return layout;
}

DescriptorHeap::DescriptorHeap(DevicePtr                              device,
                               std::uint32_t                          resourceSlots,
                               std::span<const vk::SamplerCreateInfo> samplers)
    : mDevice{std::move(device)}
    , mLayout{DescriptorHeapLayout::make(
          mDevice->raiiPhysicalDevice()
              .getProperties2<vk::PhysicalDeviceProperties2,
                              vk::PhysicalDeviceDescriptorHeapPropertiesEXT>()
              .get<vk::PhysicalDeviceDescriptorHeapPropertiesEXT>(),
          resourceSlots,
          static_cast<std::uint32_t>(samplers.size()))}
    , mResourceHeap{makeHeapBuffer(*mDevice, mLayout.resources)}
    , mSamplerHeap{makeHeapBuffer(*mDevice, mLayout.samplers)}
    , mResourceBind{bindInfo(*mResourceHeap, mLayout.resources)}
    , mSamplerBind{bindInfo(*mSamplerHeap, mLayout.samplers)}
    , mFree(resourceSlots)
{
    // Lowest index last, so that it is handed out first.
    std::iota(mFree.rbegin(), mFree.rend(), 0u);

    std::vector<vk::HostAddressRangeEXT> ranges;
    ranges.reserve(samplers.size());
    auto mapping = mSamplerHeap->mapForWrite();
    for (std::size_t i = 0u; i < samplers.size(); ++i)
    {
        ranges.push_back(
            {.address = mapping.data + (i * mLayout.samplerStride), .size = mLayout.samplerStride});
    }
    mDevice->raii().writeSamplerDescriptorsEXT(samplers, ranges);
}

DescriptorHeap::~DescriptorHeap() = default;

DescriptorHeap::Slot::Slot(DescriptorHeapPtr heap, std::uint32_t index) noexcept
    : mHeap{std::move(heap)}
    , mIndex{index}
{
}

DescriptorHeap::Slot::Slot(Slot&& other) noexcept
    : mHeap{std::move(other.mHeap)}
    , mIndex{other.mIndex}
{
}

DescriptorHeap::Slot::~Slot()
{
    if (mHeap)
    {
        mHeap->release(mIndex);
    }
}

DescriptorHeap::Slot
DescriptorHeap::allocate()
{
    const std::scoped_lock lock{mMutex};
    if (mFree.empty())
    {
        throw std::runtime_error{"The descriptor heap is full."};
    }

    const std::uint32_t index = mFree.back();
    mFree.pop_back();

    return Slot{shared_from_this(), index};
}

// mFree was sized for every slot, and a slot is in it at most once, so the push_back cannot
// allocate.
void
// NOLINTNEXTLINE(bugprone-exception-escape)
DescriptorHeap::release(std::uint32_t index) noexcept
{
    const std::scoped_lock lock{mMutex};
    mFree.push_back(index);
}

void
DescriptorHeap::writeImage(const Slot&                    slot,
                           const vk::ImageViewCreateInfo& view,
                           vk::ImageLayout                layout)
{
    const vk::ImageDescriptorInfoEXT image{.pView = &view, .layout = layout};
    writeResource(slot, {.type = vk::DescriptorType::eSampledImage, .data = &image});
}

void
DescriptorHeap::writeUniformBuffer(const Slot& slot, const vk::DeviceAddressRangeKHR& range)
{
    writeResource(slot, {.type = vk::DescriptorType::eUniformBuffer, .data = &range});
}

void
DescriptorHeap::writeResource(const Slot& slot, const vk::ResourceDescriptorInfoEXT& descriptor)
{
    VOG_ASSERT_MSG(slot.mHeap.get() == this, "The slot belongs to another descriptor heap.");

    // The slot's owner is the only writer of its bytes, and the slot was free until it took it.
    // The lock is for the mapping: VMA does not allow two threads to map one allocation at once.
    const std::scoped_lock        lock{mMutex};
    const vk::DeviceSize          size    = descriptor.type == vk::DescriptorType::eSampledImage
                                              ? mLayout.imageDescriptorSize
                                              : mLayout.bufferDescriptorSize;
    auto                          mapping = mResourceHeap->mapForWrite();
    const vk::HostAddressRangeEXT range{
        .address = mapping.data + DescriptorHeapLayout::slotOffset(slot.index()), .size = size};
    mDevice->raii().writeResourceDescriptorsEXT(descriptor, range);
}
} // namespace VOG::Graphics::Vulkan
