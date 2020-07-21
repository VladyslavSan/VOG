#include <VOG/Graphics/Typedefs.hpp>

namespace vk::raii
{
class SurfaceKHR;
}

namespace VOG::Common
{
class JSONContainer;
}

namespace VOG::Graphics::Api
{
VOG_DECLARE_PTR(GraphicsProvider);
}

namespace VOG::Graphics::Resources::helper
{
std::shared_ptr<vk::raii::SurfaceKHR> CreateRenderSurface(Api::GraphicsProviderPtr graphicsProvider,
                                                          const Common::JSONContainer& parameters,
                                                          bool throwOnFail);
}