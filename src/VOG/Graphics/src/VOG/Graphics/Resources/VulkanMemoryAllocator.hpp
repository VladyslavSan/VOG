#pragma once
#include <VOG/Graphics/Typedefs.hpp>

namespace VOG::Graphics::Api
{
VOG_DECLARE_PTR(GraphicsProvider);
}

namespace VOG::Graphics::Resources
{
class VulkanMemoryAllocator
{
public:
    VulkanMemoryAllocator(const Api::GraphicsProviderPtr& GraphicsProvider);

protected:
    Api::GraphicsProviderPtr m_graphicsProvider;

    bool m_initialized;
    std::shared_ptr<void> m_vmaHandle;
};
} // namespace VOG::Graphics::Resources
