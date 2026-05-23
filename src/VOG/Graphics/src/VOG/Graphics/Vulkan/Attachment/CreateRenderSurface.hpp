#include <VOG/Common/SurfaceHandle.hpp>
#include <VOG/Graphics/Typedefs.hpp>

namespace vk::raii
{
class SurfaceKHR;
}

namespace VOG::Common
{
class JSONContainer;
}

namespace VOG::Graphics::Vulkan
{
class Instance;
std::shared_ptr<vk::raii::SurfaceKHR> CreateRenderSurface(const Instance&              instance,
                                                          const Common::SurfaceHandle& surface,
                                                          bool                         throwOnFail);
} // namespace VOG::Graphics::Vulkan
