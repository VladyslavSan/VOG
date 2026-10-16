#pragma once

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferRecorder.hpp>
#include <VOG/Graphics/Vulkan/DescriptorHeap.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/DeviceRequirements.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>
#include <VOG/Graphics/Vulkan/Texture.hpp>
#include <VOG/Graphics/Vulkan/UniformBlock.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "TestShaderCompiler.hpp"

namespace VOG::Tests
{
/** A command buffer that records nothing, ready to submit. */
inline Graphics::Vulkan::ExecutableCommandBuffer
recordEmpty(const Graphics::Vulkan::CommandBufferPoolPtr& pool)
{
    auto recorder = pool->record();
    return recorder.finish();
}

/**
 * Headless Instance and Device. The validation layer is off here; see VulkanValidationFixture.
 */
class VulkanFixture : public testing::Test
{
public:
    struct Options
    {
        /** Enable VK_LAYER_KHRONOS_validation and record its ERROR messages into
         *  mValidationErrors. Falls back to no layer when the layer or VK_EXT_debug_utils is not
         *  installed; check validationActive(). */
        bool validation = false;

        /** Enable VK_EXT_headless_surface so tests can build a Swapchain without a window. Falls
         *  back to off when the instance does not offer it; check headlessSurfaceActive(). */
        bool headlessSurface = false;

        /** Turn on the synchronization validation of the layer, which reports a missing
         *  dependency between two accesses (core validation cannot see one). Needs validation. The
         *  setting reaches the layer through an environment variable that only the layer's
         *  version may know, so a negative control must show that it works. */
        bool syncValidation = false;
    };

    VulkanFixture()
        : VulkanFixture{Options{}}
    {
    }

    Graphics::Vulkan::ShaderPtr
    createShader(Graphics::Vulkan::Shader::ShadingStage stage, const std::string& glslCode)
    {
        return mDevice->createShader(stage, compileGLSL(stage, glslCode));
    }

    /** A descriptor heap of @p slots resource slots and one linear sampler. */
    Graphics::Vulkan::DescriptorHeapPtr
    createHeap(std::uint32_t slots = 8u)
    {
        const vk::SamplerCreateInfo sampler{.magFilter = vk::Filter::eLinear,
                                            .minFilter = vk::Filter::eLinear};
        return mDevice->createDescriptorHeap(slots, {&sampler, 1u});
    }

    /** A host-written buffer holding @p bytes, to copy from. */
    Graphics::Vulkan::BufferPtr
    createStaging(std::span<const std::byte> bytes)
    {
        Graphics::Vulkan::BufferPtr staging = mDevice->createBuffer(
            {.size = bytes.size(), .usage = vk::BufferUsageFlagBits::eTransferSrc},
            {.flags = Graphics::Vulkan::AllocationFlag::eHostSequentialWrite});
        auto mapping = staging->mapForWrite();
        std::ranges::copy(bytes, mapping.data);

        return staging;
    }

protected:
    explicit VulkanFixture(Options options)
        : mValidationActive{options.validation && validationInstalled()}
        , mHeadlessSurfaceActive{options.headlessSurface &&
                                 instanceExtensionInstalled(kHeadlessSurfaceExtension)}
        , mInstance{createInstance(options.syncValidation && mValidationActive,
                                   {
                                       .appName    = "VOG Test",
                                       .engineName = "Test",
                                       .layers     = mValidationActive
                                                       ? std::vector<std::string>{kValidationLayer}
                                                       : std::vector<std::string>{},
                                       .extensions = enabledExtensions(),
                                   })}
        , mMessenger{mValidationActive ? makeMessenger()
                                       : vk::raii::DebugUtilsMessengerEXT{nullptr}}
        , mDevice{mInstance->makeDevice()}
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

public:
    /**
     * Barrier that takes a fresh color image to the layout beginRendering expects. Leaves .image
     * null: setImageBarrier fills it from the attachment.
     */
    static vk::ImageMemoryBarrier2
    toColorAttachmentBarrier()
    {
        return {.srcStageMask        = vk::PipelineStageFlagBits2::eTopOfPipe,
                .dstStageMask        = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                .dstAccessMask       = vk::AccessFlagBits2::eColorAttachmentWrite,
                .oldLayout           = vk::ImageLayout::eUndefined,
                .newLayout           = vk::ImageLayout::eColorAttachmentOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .subresourceRange    = {.aspectMask     = vk::ImageAspectFlagBits::eColor,
                                        .baseMipLevel   = 0u,
                                        .levelCount     = VK_REMAINING_MIP_LEVELS,
                                        .baseArrayLayer = 0u,
                                        .layerCount     = VK_REMAINING_ARRAY_LAYERS}};
    }

protected:
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
        Released released{.instance = mInstance, .device = mDevice};
        mInstanceKeeper = std::move(mInstance);
        mDevice.reset();
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
        EXPECT_TRUE(mValidationErrors.empty())
            << (mValidationErrors.empty() ? "" : mValidationErrors.front());

        mMessenger = vk::raii::DebugUtilsMessengerEXT{nullptr};
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
    std::vector<std::string> mValidationErrors;

private:
    const bool mValidationActive;
    const bool mHeadlessSurfaceActive;

protected:
    Graphics::Vulkan::InstancePtr    mInstance;
    Graphics::Vulkan::InstancePtr    mInstanceKeeper;
    vk::raii::DebugUtilsMessengerEXT mMessenger;
    Graphics::Vulkan::DevicePtr      mDevice;

private:
    static constexpr const char* kValidationLayer          = "VK_LAYER_KHRONOS_validation";
    static constexpr const char* kDebugUtilsExtension      = "VK_EXT_debug_utils";
    static constexpr const char* kHeadlessSurfaceExtension = "VK_EXT_headless_surface";
    static constexpr const char* kSyncValidationVariable   = "VK_KHRONOS_VALIDATION_VALIDATE_SYNC";

    /**
     * The layer reads its settings in vkCreateInstance. The environment is process-wide, so the
     * variable is set for that call only and then restored to what it was: a test binary that runs
     * later fixtures does not inherit the setting.
     */
    static Graphics::Vulkan::InstancePtr
    createInstance(bool                                                  syncValidation,
                   const Graphics::Vulkan::Instance::InstanceParameters& parameters)
    {
        struct Setting
        {
            explicit Setting(bool active)
                : mActive{active}
            {
                if (mActive)
                {
                    if (const char* previous = std::getenv(kSyncValidationVariable))
                    {
                        mPrevious = previous;
                    }
                    set("true");
                }
            }

            ~Setting()
            {
                if (mActive)
                {
                    set(mPrevious ? mPrevious->c_str() : nullptr);
                }
            }

            Setting(const Setting&)            = delete;
            Setting& operator=(const Setting&) = delete;

            static void
            set(const char* value)
            {
#ifdef _WIN32
                _putenv_s(kSyncValidationVariable, value != nullptr ? value : "");
#else
                if (value != nullptr)
                {
                    setenv(kSyncValidationVariable, value, 1);
                }
                else
                {
                    unsetenv(kSyncValidationVariable);
                }
#endif
            }

            bool                       mActive;
            std::optional<std::string> mPrevious;
        } const setting{syncValidation};

        return Graphics::Vulkan::Instance::create(parameters);
    }

    static bool
    instanceExtensionInstalled(std::string_view name)
    {
        return Graphics::Vulkan::hasExtension(
            vk::raii::Context{}.enumerateInstanceExtensionProperties(), name);
    }

    static bool
    validationInstalled()
    {
        return Graphics::Vulkan::hasLayer(vk::raii::Context{}.enumerateInstanceLayerProperties(),
                                          kValidationLayer) &&
               instanceExtensionInstalled(kDebugUtilsExtension);
    }

    std::vector<std::string>
    enabledExtensions() const
    {
        std::vector<std::string> extensions;
        if (mValidationActive)
        {
            extensions.emplace_back(kDebugUtilsExtension);
        }
        if (mHeadlessSurfaceActive)
        {
            extensions.emplace_back(kHeadlessSurfaceExtension);
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
        return mInstance->createDebugUtilsMessengerEXT(vk::DebugUtilsMessengerCreateInfoEXT{
            .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
            .messageType     = vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
            .pfnUserCallback = VulkanFixture::onMessage,
            .pUserData       = &mValidationErrors,
        });
    }
};
} // namespace VOG::Tests
