#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

struct Status
{
    bool ok = false;
    std::string message;
};

struct DeviceInfo
{
    std::string gpu_name;
    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;
    std::uint32_t api_version = 0;
    float timestamp_period = 0.0f;
    std::uint32_t timestamp_valid_bits = 0;
};

class VulkanContext final
{
public:
    VulkanContext() = default;
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;
    ~VulkanContext();

    Status Initialize();

    VkInstance instance() const { return instance_; }
    VkPhysicalDevice physical_device() const { return physical_device_; }
    VkDevice device() const { return device_; }
    VkQueue queue() const { return queue_; }
    std::uint32_t queue_family_index() const { return queue_family_index_; }
    const DeviceInfo& device_info() const { return device_info_; }

private:
    void Destroy();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t queue_family_index_ = 0;
    DeviceInfo device_info_;
};

const char* VkResultToString(VkResult result);
Status CheckVk(VkResult result, const char* context);
std::string FormatVulkanApiVersion(std::uint32_t version);
