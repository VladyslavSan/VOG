#include "VOG/Engine/Renderer/FrameBlocks.hpp"

#include "VOG/Engine/Renderer/DrawPushData.hpp"
#include <VOG/Graphics/Frame/FrameObjects.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/HeapBuffer.hpp>

#include <algorithm>

namespace VOG::Engine
{
FrameBlocks::FrameBlocks(Graphics::Vulkan::DevicePtr         device,
                         Graphics::Vulkan::DescriptorHeapPtr heap)
    : mDevice{std::move(device)}
    , mHeap{std::move(heap)}
{
}

Graphics::Vulkan::HeapBufferPtr
FrameBlocks::of(const Graphics::Frame::FrameObjectsPtr& slot)
{
    const auto found = std::ranges::find(mBlocks, slot, &decltype(mBlocks)::value_type::first);
    if (found != mBlocks.end())
    {
        return found->second;
    }

    return mBlocks.emplace_back(slot, mDevice->createHeapBuffer(mHeap, sizeof(FrameBlock))).second;
}
} // namespace VOG::Engine
