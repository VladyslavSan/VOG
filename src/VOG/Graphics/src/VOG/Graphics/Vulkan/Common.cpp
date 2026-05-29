#include <VOG/Graphics/Vulkan/Common.hpp>

namespace VOG::Graphics::Vulkan
{
bool
isDepthFormat(vk::Format format)
{
    return format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint ||
           format == vk::Format::eD16Unorm || format == vk::Format::eD32Sfloat ||
           format == vk::Format::eD16UnormS8Uint;
}
bool
isStencilFormat(vk::Format format)
{
    return format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint ||
           format == vk::Format::eD16UnormS8Uint || format == vk::Format::eS8Uint;
}
} // namespace VOG::Graphics::Vulkan