#pragma once

#include <VOG/Graphics/Typedefs.hpp>

#include <cstddef>
#include <vector>

namespace VOG::Graphics::Frame
{
class FrameObjects;
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
 * once per frame. All of them are made when the object is, so a descriptor heap that has no room
 * for them fails the constructor of the renderer, and not the render thread at its first frame. A
 * slot is found by its index. A frame writes the buffer of its slot only after the fences of the
 * slot were waited, so no draw of an earlier frame reads it any more.
 *
 * Render thread only, after the constructor.
 */
class FrameBlocks
{
public:
    /**
     * @throws std::runtime_error if the heap has no room for @p slotCount buffers.
     */
    FrameBlocks(const Graphics::Vulkan::DevicePtr&         device,
                const Graphics::Vulkan::DescriptorHeapPtr& heap,
                std::size_t                                slotCount);

    /** The buffer of @p slot. */
    [[nodiscard]] const Graphics::Vulkan::HeapBufferPtr&
    of(const Graphics::Frame::FrameObjects& slot) const;

private:
    /** Lets RendererTest read the buffers, one per slot in the order of the slots. */
    friend struct RendererTestAccess;

    std::vector<Graphics::Vulkan::HeapBufferPtr> mBlocks;
};
} // namespace VOG::Engine
