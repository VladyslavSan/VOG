#include "VOG/Engine/Renderer/UploadTicket.hpp"

#include "VOG/Engine/Renderer/MeshDefinition.hpp"
#include "VOG/Engine/Renderer/TextureDefinition.hpp"
#include <VOG/Graphics/Vulkan/TimelineSemaphore.hpp>

namespace VOG::Engine
{
bool
UploadTicket::isResident() const
{
    // Acquire pairs with the release store of the render thread, which ran after the submit.
    const std::uint64_t submitted = value.load(std::memory_order_acquire);

    return submitted != 0u && timeline->counterValue() >= submitted;
}

bool
Mesh::isResident() const
{
    return mData && mData->ticket->isResident();
}

bool
Texture::isResident() const
{
    return mData && mData->ticket->isResident();
}
} // namespace VOG::Engine
