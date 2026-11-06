#include <VOG/Graphics/Vulkan/DeviceRequirements.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace VOG::Tests
{
/**
 * The message sink sees the construction of the instance: the loader reports an extension it does
 * not know while vkCreateInstance runs, which is before the persistent messenger of the Instance
 * exists. Only the messenger that the sink puts on the pNext chain of the create info can catch
 * it.
 */
TEST(InstanceTest, sinkSeesAnErrorOfTheConstructionItself)
{
    using namespace VOG::Graphics::Vulkan;

    const vk::raii::Context context;
    if (!hasExtension(context.enumerateInstanceExtensionProperties(), "VK_EXT_debug_utils"))
    {
        GTEST_SKIP() << "VK_EXT_debug_utils not available";
    }

    std::mutex               mutex;
    std::vector<std::string> messages;

    EXPECT_THROW(std::ignore = Instance::create({.appName    = "VOG Instance Test",
                                                 .engineName = "Test",
                                                 .extensions = {"VK_FAKE_extension"},
                                                 .messageSink =
                                                     [&](std::string_view message)
                                                 {
                                                     const std::scoped_lock lock{mutex};
                                                     messages.emplace_back(message);
                                                 }}),
                 vk::ExtensionNotPresentError);

    const std::scoped_lock lock{mutex};
    EXPECT_TRUE(std::ranges::any_of(messages,
                                    [](const std::string& message)
                                    {
                                        return message.contains("VK_FAKE_extension");
                                    }));
}
} // namespace VOG::Tests
