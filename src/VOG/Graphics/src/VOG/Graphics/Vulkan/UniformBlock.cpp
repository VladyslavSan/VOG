#include "VOG/Graphics/Vulkan/UniformBlock.hpp"

#include <VOG/Graphics/Vulkan/Buffer.hpp>

namespace VOG::Graphics::Vulkan
{
UniformBlock::UniformBlock(DescriptorHeap::Slot slot, BufferPtr buffer)
    : mSlot{std::move(slot)}
    , mBuffer{std::move(buffer)}
{
}
} // namespace VOG::Graphics::Vulkan
