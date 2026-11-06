#include "VOG/Engine/Renderer/FrameBlocks.hpp"

#include "VOG/Engine/Renderer/DrawPushData.hpp"
#include <VOG/Graphics/Frame/FrameObjects.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/HeapBuffer.hpp>

namespace VOG::Engine
{
FrameBlocks::FrameBlocks(const Graphics::Vulkan::DevicePtr&         device,
                         const Graphics::Vulkan::DescriptorHeapPtr& heap,
                         std::size_t                                slotCount)
{
    mBlocks.reserve(slotCount);
    for (std::size_t slot = 0u; slot < slotCount; ++slot)
    {
        mBlocks.push_back(device->createHeapBuffer(heap, sizeof(FrameBlock)));
    }
}

const Graphics::Vulkan::HeapBufferPtr&
FrameBlocks::of(const Graphics::Frame::FrameObjects& slot) const
{
    return mBlocks.at(slot.index());
}
} // namespace VOG::Engine
