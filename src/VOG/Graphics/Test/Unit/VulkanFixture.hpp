#pragma once

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "TestShaderCompiler.hpp"

namespace VOG::Tests
{
/**
 * Headless Instance and Device. The validation layer is off here; see VulkanValidationFixture.
 */
class VulkanFixture : public testing::Test
{
public:
    VulkanFixture()
        : VulkanFixture{false}
    {
    }

    Graphics::Vulkan::ShaderPtr
    createShader(Graphics::Vulkan::Shader::ShadingStage stage, const std::string& glslCode)
    {
        return VulkanDevice->createShader(stage, compileGLSL(stage, glslCode));
    }

protected:
    /**
     * @param withValidation  Enable VK_LAYER_KHRONOS_validation and record its ERROR messages
     *                        into validationErrors. Falls back to no layer when the layer or
     *                        VK_EXT_debug_utils is not installed; check validationActive().
     * @param withHeadlessSurface  Enable VK_EXT_headless_surface so tests can build a Swapchain
     *                        without a window. Falls back to off when the instance does not
     *                        offer it; check headlessSurfaceActive().
     */
    explicit VulkanFixture(bool withValidation, bool withHeadlessSurface = false)
        : mValidationActive{withValidation && validationInstalled()}
        , mHeadlessSurfaceActive{withHeadlessSurface &&
                                 instanceExtensionInstalled(gHeadlessSurfaceExtension)}
        , VulkanInstance{Graphics::Vulkan::Instance::create({
              .appName    = "VOG Test",
              .engineName = "Test",
              .layers     = mValidationActive ? std::vector<std::string>{gValidationLayer}
                                              : std::vector<std::string>{},
              .extensions = enabledExtensions(),
          })}
        , messenger{mValidationActive ? makeMessenger() : vk::raii::DebugUtilsMessengerEXT{nullptr}}
        , VulkanDevice{VulkanInstance->makeDevice()}
    {
    }

    bool
    validationActive() const
    {
        return mValidationActive;
    }

    bool
    headlessSurfaceActive() const
    {
        return mHeadlessSurfaceActive;
    }

    /** Barrier that takes a fresh color image to the layout beginRendering expects. */
    static vk::ImageMemoryBarrier2
    toColorAttachmentBarrier(vk::Image image)
    {
        return {.srcStageMask        = vk::PipelineStageFlagBits2::eTopOfPipe,
                .dstStageMask        = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                .dstAccessMask       = vk::AccessFlagBits2::eColorAttachmentWrite,
                .oldLayout           = vk::ImageLayout::eUndefined,
                .newLayout           = vk::ImageLayout::eColorAttachmentOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image               = image,
                .subresourceRange    = {.aspectMask     = vk::ImageAspectFlagBits::eColor,
                                        .baseMipLevel   = 0u,
                                        .levelCount     = VK_REMAINING_MIP_LEVELS,
                                        .baseArrayLayer = 0u,
                                        .layerCount     = VK_REMAINING_ARRAY_LAYERS}};
    }

    /** Weak views of the Instance and Device the fixture created. */
    struct Released
    {
        Graphics::Vulkan::InstanceWeakPtr instance;
        Graphics::Vulkan::DeviceWeakPtr   device;
    };

    /**
     * Drops the fixture's own references to the Device and, as far as tests can tell, the
     * Instance. The device lives on only if an object created from it still holds it. The
     * fixture keeps the Instance alive in secret until expectDeviceReleased(), because the
     * validation messenger must be destroyed before its Instance.
     */
    Released
    releaseDevice()
    {
        Released released{.instance = VulkanInstance, .device = VulkanDevice};
        mInstanceKeeper = std::move(VulkanInstance);
        VulkanDevice.reset();
        return released;
    }

    /**
     * Call once nothing but the fixture can still hold the Device: it must have expired, the
     * validation layer must have raised no error while it went away (a child object destroyed
     * after its device is reported by vkDestroyDevice, VUID-vkDestroyDevice-device-05137), and
     * the Instance must expire as soon as the fixture lets go of it.
     */
    void
    expectDeviceReleased(const Released& released)
    {
        EXPECT_TRUE(released.device.expired());
        EXPECT_TRUE(validationErrors.empty())
            << (validationErrors.empty() ? "" : validationErrors.front());

        messenger = vk::raii::DebugUtilsMessengerEXT{nullptr};
        mInstanceKeeper.reset();
        EXPECT_TRUE(released.instance.expired());
    }

    /**
     * Requirement 8 of ResourceOwnership.md. Call after releaseDevice(): the Device and the
     * Instance must still be alive, @p use makes a real Vulkan call through the dependent, and
     * both expire as soon as the dependent is dropped, without a validation error.
     */
    template <typename Dependent, typename Use>
    void
    expectDependentHoldsDevice(const Released& released, Dependent& dependent, Use&& use)
    {
        // Fatal: with a dead device the call below would be undefined behaviour.
        ASSERT_FALSE(released.device.expired());

        use(*dependent);
        dependent.reset();

        expectDeviceReleased(released);
    }

    /** For dependents that expose no Vulkan call; destroying them is the call under test. */
    template <typename Dependent>
    void
    expectDependentHoldsDevice(const Released& released, Dependent& dependent)
    {
        expectDependentHoldsDevice(released,
                                   dependent,
                                   [](auto&)
                                   {
                                   });
    }

    // Declared first, destroyed last: the messenger can still report while the device goes away.
    std::vector<std::string> validationErrors;

private:
    const bool mValidationActive;
    const bool mHeadlessSurfaceActive;

protected:
    Graphics::Vulkan::InstancePtr    VulkanInstance;
    Graphics::Vulkan::InstancePtr    mInstanceKeeper;
    vk::raii::DebugUtilsMessengerEXT messenger;
    Graphics::Vulkan::DevicePtr      VulkanDevice;

private:
    static constexpr const char* gValidationLayer          = "VK_LAYER_KHRONOS_validation";
    static constexpr const char* gDebugUtilsExtension      = "VK_EXT_debug_utils";
    static constexpr const char* gHeadlessSurfaceExtension = "VK_EXT_headless_surface";

    static bool
    instanceExtensionInstalled(std::string_view name)
    {
        vk::raii::Context context;

        const auto extensions = context.enumerateInstanceExtensionProperties();
        return std::ranges::any_of(extensions,
                                   [name](const vk::ExtensionProperties& extension)
                                   {
                                       return std::string_view{extension.extensionName} == name;
                                   });
    }

    static bool
    validationInstalled()
    {
        vk::raii::Context context;

        const auto layers = context.enumerateInstanceLayerProperties();
        const bool hasLayer =
            std::ranges::any_of(layers,
                                [](const vk::LayerProperties& layer)
                                {
                                    return std::string_view{layer.layerName} == gValidationLayer;
                                });

        return hasLayer && instanceExtensionInstalled(gDebugUtilsExtension);
    }

    std::vector<std::string>
    enabledExtensions() const
    {
        std::vector<std::string> extensions;
        if (mValidationActive)
        {
            extensions.emplace_back(gDebugUtilsExtension);
        }
        if (mHeadlessSurfaceActive)
        {
            extensions.emplace_back(gHeadlessSurfaceExtension);
        }
        return extensions;
    }

    static vk::Bool32
    onMessage(vk::DebugUtilsMessageSeverityFlagBitsEXT /*severity*/,
              vk::DebugUtilsMessageTypeFlagsEXT /*types*/,
              const vk::DebugUtilsMessengerCallbackDataEXT* pData,
              void*                                         pUserData)
    {
        if (pData->pMessage != nullptr)
        {
            static_cast<std::vector<std::string>*>(pUserData)->emplace_back(pData->pMessage);
        }
        return VK_FALSE;
    }

    vk::raii::DebugUtilsMessengerEXT
    makeMessenger()
    {
        return VulkanInstance->createDebugUtilsMessengerEXT(vk::DebugUtilsMessengerCreateInfoEXT{
            .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
            .messageType     = vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
            .pfnUserCallback = VulkanFixture::onMessage,
            .pUserData       = &validationErrors,
        });
    }
};
} // namespace VOG::Tests
