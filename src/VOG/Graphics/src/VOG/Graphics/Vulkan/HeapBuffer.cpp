#include "VOG/Graphics/Vulkan/HeapBuffer.hpp"

#include <utility>

namespace VOG::Graphics::Vulkan
{
HeapBuffer::HeapBuffer(DescriptorHeap::Slot slot, BufferPtr buffer)
    : mSlot{std::move(slot)}
    , mBuffer{std::move(buffer)}
{
}
} // namespace VOG::Graphics::Vulkan
