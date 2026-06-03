#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>
#include <VOG/Graphics/Vulkan/Queue.hpp>
#include <VOG/Graphics/Vulkan/Shader.hpp>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(FencePool);
VOG_DECLARE_PTR(Instance);
VOG_DECLARE_PTR(MemoryAllocator);

class PhysicalDevice : public vk::raii::PhysicalDevice
{
public:
    struct QueueFamilyInfo
    {
        std::uint32_t             familyIndex = 0u;
        vk::QueueFamilyProperties familyProperties{};
    };

    struct QueueInfos
    {
        QueueFamilyInfo graphics;
        QueueFamilyInfo transfer;
    };

    PhysicalDevice(vk::raii::PhysicalDevice physicalDevice);

    const QueueInfos queueInfos;
};

class Device
    : public PhysicalDevice
    , public vk::raii::Device
    , public std::enable_shared_from_this<Device>
{
    friend class Instance;

    Device(InstancePtr instance, vk::raii::PhysicalDevice physicalDevice);
    void init();

public:
    ~Device(); // NOLINT(bugprone-derived-method-shadowing-base-method)

    const PhysicalDevice& getPhysicalDevice() const;

    ShaderPtr createShader(Shader::ShadingStage stage, std::span<const std::uint32_t> binary);

    using vk::raii::Device::operator*;
    using vk::raii::Device::getDispatcher;

    const InstancePtr             instance;
    const FencePoolPtr            fencePool;
    const Vulkan::Queue           graphicsQueue;
    const Vulkan::Queue           transferQueue;
    const MemoryAllocatorPtr      memoryAllocator;
    const vk::raii::PipelineCache pipelineCache;
};
} // namespace VOG::Graphics::Vulkan
