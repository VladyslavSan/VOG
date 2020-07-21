#include "CreateRenderSurface.hpp"

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Graphics/Api/GraphicsProvider.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>

#ifdef PLATFORM_VIDEO_WINDOWS
#include <Windows.h>
#endif

#include <stdexcept>

namespace VOG::Graphics::Resources::helper
{
std::shared_ptr<vk::raii::SurfaceKHR>
CreateRenderSurface(Api::GraphicsProviderPtr graphicsProvider,
                    const Common::JSONContainer& parameters, bool throwOnFail)
{
    std::shared_ptr<vk::raii::SurfaceKHR> SurfaceHandle;

#ifdef PLATFORM_VIDEO_WINDOWS
    {
        void* windowHandle = parameters["window"]->GetValueOr<void*>(nullptr);
        void* instance = parameters["insntace"]->GetValueOr<void*>(nullptr);

        if (windowHandle)
        {
            vk::Win32SurfaceCreateInfoKHR createInfo{};
            createInfo.setHwnd(reinterpret_cast<HWND>(windowHandle))
                .setHinstance(GetModuleHandleA(0));

            SurfaceHandle = std::make_shared<vk::raii::SurfaceKHR>(*graphicsProvider->GetInstance(),
                                                                   createInfo);
        }
    }
#elif

#endif

    if (throwOnFail && !SurfaceHandle)
        throw std::runtime_error("CreateSurface failed");

    return SurfaceHandle;
}
} // namespace VOG::Graphics::Resources::helper
