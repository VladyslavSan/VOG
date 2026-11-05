#pragma once

#include <VOG/Graphics/Typedefs.hpp>

#include <utility>
#include <vector>

namespace VOG::Graphics::Frame
{
VOG_DECLARE_PTR(FrameObjects);
} // namespace VOG::Graphics::Frame

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(DescriptorHeap);
VOG_DECLARE_PTR(Device);
VOG_DECLARE_PTR(HeapBuffer);
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
/**
 * The buffers of the Frame block of the frame slots, one per slot, written by the render thread
 * once per frame. A slot is found by the object that FrameObjectManager hands out for it, and
 * gets its buffer the first time it is rendered into. A frame writes the buffer of its slot only
 * after the fences of the slot were waited, so no draw of an earlier frame reads it any more.
 *
 * Render thread only.
 */
class FrameBlocks
{
public:
    FrameBlocks(Graphics::Vulkan::DevicePtr device, Graphics::Vulkan::DescriptorHeapPtr heap);

    /** The buffer of @p slot; made by the first call for the slot. */
    [[nodiscard]] Graphics::Vulkan::HeapBufferPtr of(const Graphics::Frame::FrameObjectsPtr& slot);

private:
    /** Lets RendererTest read the buffers after the render thread stopped. */
    friend struct RendererTestAccess;

    Graphics::Vulkan::DevicePtr         mDevice;
    Graphics::Vulkan::DescriptorHeapPtr mHeap;

    std::vector<std::pair<Graphics::Frame::FrameObjectsPtr, Graphics::Vulkan::HeapBufferPtr>>
        mBlocks;
};
} // namespace VOG::Engine
