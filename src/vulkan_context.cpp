#include "vulkan_context.h"

#include <sstream>
#include <vector>

namespace
{
Status MakeStatus(const bool ok, std::string message)
{
    Status status;
    status.ok = ok;
    status.message = std::move(message);
    return status;
}
}

const char* VkResultToString(const VkResult result)
{
    switch (result)
    {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_EVENT_SET:
        return "VK_EVENT_SET";
    case VK_EVENT_RESET:
        return "VK_EVENT_RESET";
    case VK_INCOMPLETE:
        return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    default:
        return "VK_RESULT_UNKNOWN";
    }
}

Status CheckVk(const VkResult result, const char* context)
{
    if (result == VK_SUCCESS)
    {
        return MakeStatus(true, "");
    }

    std::ostringstream stream;
    stream << context << " failed with " << VkResultToString(result) << " (" << result << ")";
    return MakeStatus(false, stream.str());
}

std::string FormatVulkanApiVersion(const std::uint32_t version)
{
    std::ostringstream stream;
    stream << VK_API_VERSION_MAJOR(version)
           << "."
           << VK_API_VERSION_MINOR(version)
           << "."
           << VK_API_VERSION_PATCH(version);
    return stream.str();
}

VulkanContext::~VulkanContext()
{
    Destroy();
}

Status VulkanContext::Initialize()
{
    Destroy();

    const VkApplicationInfo app_info = {
        VK_STRUCTURE_TYPE_APPLICATION_INFO,
        nullptr,
        "vulkan_sgemm_bench",
        VK_MAKE_VERSION(0, 1, 0),
        "vulkan_sgemm_bench",
        VK_MAKE_VERSION(0, 1, 0),
        VK_API_VERSION_1_1};

    const VkInstanceCreateInfo instance_info = {
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        nullptr,
        0,
        &app_info,
        0,
        nullptr,
        0,
        nullptr};

    Status status = CheckVk(vkCreateInstance(&instance_info, nullptr, &instance_), "vkCreateInstance");
    if (!status.ok)
    {
        return status;
    }

    std::uint32_t physical_device_count = 0;
    status = CheckVk(vkEnumeratePhysicalDevices(instance_, &physical_device_count, nullptr), "vkEnumeratePhysicalDevices(count)");
    if (!status.ok)
    {
        return status;
    }

    if (physical_device_count == 0)
    {
        return MakeStatus(false, "No Vulkan physical devices found.");
    }

    std::vector<VkPhysicalDevice> devices(physical_device_count, VK_NULL_HANDLE);
    status = CheckVk(vkEnumeratePhysicalDevices(instance_, &physical_device_count, devices.data()), "vkEnumeratePhysicalDevices(list)");
    if (!status.ok)
    {
        return status;
    }

    for (VkPhysicalDevice candidate : devices)
    {
        std::uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queue_family_count, queue_families.data());

        for (std::uint32_t family_index = 0; family_index < queue_family_count; ++family_index)
        {
            if ((queue_families[family_index].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0)
            {
                continue;
            }

            physical_device_ = candidate;
            queue_family_index_ = family_index;
            device_info_.timestamp_valid_bits = queue_families[family_index].timestampValidBits;
            break;
        }

        if (physical_device_ != VK_NULL_HANDLE)
        {
            break;
        }
    }

    if (physical_device_ == VK_NULL_HANDLE)
    {
        return MakeStatus(false, "No Vulkan physical device with a compute queue was found.");
    }

    VkPhysicalDeviceProperties properties = {};
    vkGetPhysicalDeviceProperties(physical_device_, &properties);
    device_info_.gpu_name = properties.deviceName;
    device_info_.vendor_id = properties.vendorID;
    device_info_.device_id = properties.deviceID;
    device_info_.api_version = properties.apiVersion;
    device_info_.timestamp_period = properties.limits.timestampPeriod;

    const float queue_priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info = {
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        nullptr,
        0,
        queue_family_index_,
        1,
        &queue_priority};

    const VkDeviceCreateInfo device_info = {
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        nullptr,
        0,
        1,
        &queue_info,
        0,
        nullptr,
        0,
        nullptr,
        nullptr};

    status = CheckVk(vkCreateDevice(physical_device_, &device_info, nullptr, &device_), "vkCreateDevice");
    if (!status.ok)
    {
        return status;
    }

    vkGetDeviceQueue(device_, queue_family_index_, 0, &queue_);
    return MakeStatus(true, "");
}

void VulkanContext::Destroy()
{
    if (device_ != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device_);
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    if (instance_ != VK_NULL_HANDLE)
    {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }

    physical_device_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
    queue_family_index_ = 0;
    device_info_ = {};
}
