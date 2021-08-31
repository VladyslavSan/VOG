#include <VOG/Graphics/Typedefs.hpp>

namespace vk::raii
{
class SurfaceKHR;
}

namespace VOG::Common
{
class JSONContainer;
}

namespace VOG::Graphics
{
VOG_DECLARE_PTR(GraphicsProvider);
}

namespace VOG::Graphics::Vulkan
{
std::shared_ptr<vk::raii::SurfaceKHR> CreateRenderSurface(GraphicsProviderPtr graphicsProvider,
                                                          const Common::JSONContainer& parameters,
                                                          bool                         throwOnFail);
}
