#include "sgemm_vulkan.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <numeric>
#include <vector>

namespace
{
struct OwnedBuffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
};

struct ComputeResources
{
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkQueryPool query_pool = VK_NULL_HANDLE;
    OwnedBuffer a_buffer;
    OwnedBuffer b_buffer;
    OwnedBuffer c_buffer;
};

struct PushConstants
{
    std::uint32_t m = 0;
    std::uint32_t n = 0;
    std::uint32_t k = 0;
};

Status MakeStatus(const bool ok, std::string message)
{
    Status status;
    status.ok = ok;
    status.message = std::move(message);
    return status;
}

void DestroyBuffer(const VkDevice device, OwnedBuffer& owned)
{
    if (owned.mapped != nullptr)
    {
        vkUnmapMemory(device, owned.memory);
        owned.mapped = nullptr;
    }

    if (owned.buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(device, owned.buffer, nullptr);
        owned.buffer = VK_NULL_HANDLE;
    }

    if (owned.memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, owned.memory, nullptr);
        owned.memory = VK_NULL_HANDLE;
    }
}

void DestroyResources(const VkDevice device, ComputeResources& resources)
{
    DestroyBuffer(device, resources.c_buffer);
    DestroyBuffer(device, resources.b_buffer);
    DestroyBuffer(device, resources.a_buffer);

    if (resources.query_pool != VK_NULL_HANDLE)
    {
        vkDestroyQueryPool(device, resources.query_pool, nullptr);
        resources.query_pool = VK_NULL_HANDLE;
    }

    if (resources.fence != VK_NULL_HANDLE)
    {
        vkDestroyFence(device, resources.fence, nullptr);
        resources.fence = VK_NULL_HANDLE;
    }

    if (resources.command_pool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(device, resources.command_pool, nullptr);
        resources.command_pool = VK_NULL_HANDLE;
        resources.command_buffer = VK_NULL_HANDLE;
    }

    if (resources.pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, resources.pipeline, nullptr);
        resources.pipeline = VK_NULL_HANDLE;
    }

    if (resources.pipeline_layout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, resources.pipeline_layout, nullptr);
        resources.pipeline_layout = VK_NULL_HANDLE;
    }

    if (resources.descriptor_pool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, resources.descriptor_pool, nullptr);
        resources.descriptor_pool = VK_NULL_HANDLE;
        resources.descriptor_set = VK_NULL_HANDLE;
    }

    if (resources.descriptor_set_layout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, resources.descriptor_set_layout, nullptr);
        resources.descriptor_set_layout = VK_NULL_HANDLE;
    }
}

std::vector<char> ReadBinaryFile(const std::string& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open())
    {
        return {};
    }

    const std::streamsize size = input.tellg();
    if (size <= 0)
    {
        return {};
    }

    std::vector<char> bytes(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    input.read(bytes.data(), size);
    if (!input)
    {
        return {};
    }

    return bytes;
}

std::uint32_t FindMemoryType(
    const VkPhysicalDevice physical_device,
    const std::uint32_t type_bits,
    const VkMemoryPropertyFlags required_properties)
{
    VkPhysicalDeviceMemoryProperties memory_properties = {};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index)
    {
        const bool type_matches = (type_bits & (1U << index)) != 0;
        const bool flags_match = (memory_properties.memoryTypes[index].propertyFlags & required_properties) == required_properties;
        if (type_matches && flags_match)
        {
            return index;
        }
    }

    return std::numeric_limits<std::uint32_t>::max();
}

Status CreateMappedStorageBuffer(
    VulkanContext& context,
    const VkDeviceSize size,
    OwnedBuffer& owned_buffer)
{
    const VkBufferCreateInfo buffer_info = {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr};

    Status status = CheckVk(vkCreateBuffer(context.device(), &buffer_info, nullptr, &owned_buffer.buffer), "vkCreateBuffer");
    if (!status.ok)
    {
        return status;
    }

    VkMemoryRequirements memory_requirements = {};
    vkGetBufferMemoryRequirements(context.device(), owned_buffer.buffer, &memory_requirements);

    const std::uint32_t memory_type_index = FindMemoryType(
        context.physical_device(),
        memory_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (memory_type_index == std::numeric_limits<std::uint32_t>::max())
    {
        return MakeStatus(false, "No host-visible coherent Vulkan memory type was found for storage buffer allocation.");
    }

    const VkMemoryAllocateInfo allocate_info = {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        nullptr,
        memory_requirements.size,
        memory_type_index};

    status = CheckVk(vkAllocateMemory(context.device(), &allocate_info, nullptr, &owned_buffer.memory), "vkAllocateMemory");
    if (!status.ok)
    {
        return status;
    }

    status = CheckVk(vkBindBufferMemory(context.device(), owned_buffer.buffer, owned_buffer.memory, 0), "vkBindBufferMemory");
    if (!status.ok)
    {
        return status;
    }

    status = CheckVk(vkMapMemory(context.device(), owned_buffer.memory, 0, size, 0, &owned_buffer.mapped), "vkMapMemory");
    if (!status.ok)
    {
        return status;
    }

    return MakeStatus(true, "");
}

Status CreateDescriptorSetLayout(const VkDevice device, ComputeResources& resources)
{
    const std::array<VkDescriptorSetLayoutBinding, 3> bindings = {{
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}}};

    const VkDescriptorSetLayoutCreateInfo layout_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        nullptr,
        0,
        static_cast<std::uint32_t>(bindings.size()),
        bindings.data()};

    return CheckVk(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &resources.descriptor_set_layout), "vkCreateDescriptorSetLayout");
}

Status CreateDescriptorResources(const VkDevice device, ComputeResources& resources)
{
    const VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3};
    const VkDescriptorPoolCreateInfo pool_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        nullptr,
        0,
        1,
        1,
        &pool_size};

    Status status = CheckVk(vkCreateDescriptorPool(device, &pool_info, nullptr, &resources.descriptor_pool), "vkCreateDescriptorPool");
    if (!status.ok)
    {
        return status;
    }

    const VkDescriptorSetAllocateInfo set_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        nullptr,
        resources.descriptor_pool,
        1,
        &resources.descriptor_set_layout};

    status = CheckVk(vkAllocateDescriptorSets(device, &set_info, &resources.descriptor_set), "vkAllocateDescriptorSets");
    if (!status.ok)
    {
        return status;
    }

    const VkDescriptorBufferInfo a_info = {resources.a_buffer.buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo b_info = {resources.b_buffer.buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo c_info = {resources.c_buffer.buffer, 0, VK_WHOLE_SIZE};

    const std::array<VkWriteDescriptorSet, 3> writes = {{
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, resources.descriptor_set, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &a_info, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, resources.descriptor_set, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &b_info, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, resources.descriptor_set, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &c_info, nullptr}}};

    vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    return MakeStatus(true, "");
}

Status CreatePipeline(VulkanContext& context, const std::string& shader_path, ComputeResources& resources)
{
    const std::vector<char> shader_bytes = ReadBinaryFile(shader_path);
    if (shader_bytes.empty())
    {
        return MakeStatus(false, "Failed to read compute shader SPIR-V file at: " + shader_path);
    }

    const VkShaderModuleCreateInfo module_info = {
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        nullptr,
        0,
        static_cast<std::size_t>(shader_bytes.size()),
        reinterpret_cast<const std::uint32_t*>(shader_bytes.data())};

    VkShaderModule shader_module = VK_NULL_HANDLE;
    Status status = CheckVk(vkCreateShaderModule(context.device(), &module_info, nullptr, &shader_module), "vkCreateShaderModule");
    if (!status.ok)
    {
        return status;
    }

    const VkPushConstantRange push_constant_range = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants)};
    const VkPipelineLayoutCreateInfo layout_info = {
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        nullptr,
        0,
        1,
        &resources.descriptor_set_layout,
        1,
        &push_constant_range};

    status = CheckVk(vkCreatePipelineLayout(context.device(), &layout_info, nullptr, &resources.pipeline_layout), "vkCreatePipelineLayout");
    if (!status.ok)
    {
        vkDestroyShaderModule(context.device(), shader_module, nullptr);
        return status;
    }

    const VkPipelineShaderStageCreateInfo shader_stage = {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        nullptr,
        0,
        VK_SHADER_STAGE_COMPUTE_BIT,
        shader_module,
        "main",
        nullptr};

    const VkComputePipelineCreateInfo pipeline_info = {
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        nullptr,
        0,
        shader_stage,
        resources.pipeline_layout,
        VK_NULL_HANDLE,
        0};

    status = CheckVk(
        vkCreateComputePipelines(context.device(), VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &resources.pipeline),
        "vkCreateComputePipelines");

    vkDestroyShaderModule(context.device(), shader_module, nullptr);
    return status;
}

Status CreateCommandResources(VulkanContext& context, ComputeResources& resources)
{
    const VkCommandPoolCreateInfo pool_info = {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        nullptr,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        context.queue_family_index()};

    Status status = CheckVk(vkCreateCommandPool(context.device(), &pool_info, nullptr, &resources.command_pool), "vkCreateCommandPool");
    if (!status.ok)
    {
        return status;
    }

    const VkCommandBufferAllocateInfo command_buffer_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        nullptr,
        resources.command_pool,
        VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        1};

    status = CheckVk(vkAllocateCommandBuffers(context.device(), &command_buffer_info, &resources.command_buffer), "vkAllocateCommandBuffers");
    if (!status.ok)
    {
        return status;
    }

    const VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0};
    status = CheckVk(vkCreateFence(context.device(), &fence_info, nullptr, &resources.fence), "vkCreateFence");
    if (!status.ok)
    {
        return status;
    }

    const VkQueryPoolCreateInfo query_info = {
        VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        nullptr,
        0,
        VK_QUERY_TYPE_TIMESTAMP,
        2,
        0};

    status = CheckVk(vkCreateQueryPool(context.device(), &query_info, nullptr, &resources.query_pool), "vkCreateQueryPool");
    if (!status.ok)
    {
        return status;
    }

    return MakeStatus(true, "");
}

Status RecordCommandBuffer(const BenchmarkConfig& config, ComputeResources& resources)
{
    Status status = CheckVk(vkResetCommandBuffer(resources.command_buffer, 0), "vkResetCommandBuffer");
    if (!status.ok)
    {
        return status;
    }

    const VkCommandBufferBeginInfo begin_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        0,
        nullptr};

    status = CheckVk(vkBeginCommandBuffer(resources.command_buffer, &begin_info), "vkBeginCommandBuffer");
    if (!status.ok)
    {
        return status;
    }

    // This brackets the GPU queue interval around the recorded dispatch region:
    // bind/push/dispatch are inside the timestamps; host submit/wait and JSON emission are outside.
    vkCmdResetQueryPool(resources.command_buffer, resources.query_pool, 0, 2);
    vkCmdWriteTimestamp(resources.command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, resources.query_pool, 0);
    vkCmdBindPipeline(resources.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, resources.pipeline);
    vkCmdBindDescriptorSets(
        resources.command_buffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        resources.pipeline_layout,
        0,
        1,
        &resources.descriptor_set,
        0,
        nullptr);

    const PushConstants push_constants = {config.shape.m, config.shape.n, config.shape.k};
    vkCmdPushConstants(
        resources.command_buffer,
        resources.pipeline_layout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(PushConstants),
        &push_constants);

    const std::uint32_t group_x = (config.shape.n + 15U) / 16U;
    const std::uint32_t group_y = (config.shape.m + 15U) / 16U;
    vkCmdDispatch(resources.command_buffer, group_x, group_y, 1);
    vkCmdWriteTimestamp(resources.command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, resources.query_pool, 1);

    const VkMemoryBarrier barrier = {
        VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        nullptr,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_HOST_READ_BIT};

    vkCmdPipelineBarrier(
        resources.command_buffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0,
        1,
        &barrier,
        0,
        nullptr,
        0,
        nullptr);

    return CheckVk(vkEndCommandBuffer(resources.command_buffer), "vkEndCommandBuffer");
}

Status SubmitAndWait(VulkanContext& context, ComputeResources& resources)
{
    Status status = CheckVk(vkResetFences(context.device(), 1, &resources.fence), "vkResetFences");
    if (!status.ok)
    {
        return status;
    }

    const VkSubmitInfo submit_info = {
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        0,
        nullptr,
        nullptr,
        1,
        &resources.command_buffer,
        0,
        nullptr};

    status = CheckVk(vkQueueSubmit(context.queue(), 1, &submit_info, resources.fence), "vkQueueSubmit");
    if (!status.ok)
    {
        return status;
    }

    return CheckVk(vkWaitForFences(context.device(), 1, &resources.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
}

Status ReadTimestampMs(VulkanContext& context, ComputeResources& resources, double& out_time_ms)
{
    std::array<std::uint64_t, 2> timestamps = {0, 0};
    const VkResult result = vkGetQueryPoolResults(
        context.device(),
        resources.query_pool,
        0,
        2,
        sizeof(timestamps),
        timestamps.data(),
        sizeof(std::uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

    Status status = CheckVk(result, "vkGetQueryPoolResults");
    if (!status.ok)
    {
        return status;
    }

    const std::uint64_t delta = timestamps[1] - timestamps[0];
    out_time_ms = static_cast<double>(delta) * static_cast<double>(context.device_info().timestamp_period) / 1000000.0;
    return MakeStatus(true, "");
}

BenchmarkStats MakeStats(const BenchmarkConfig& config, std::vector<double> gpu_times_ms)
{
    BenchmarkStats stats;
    stats.gpu_times_ms = std::move(gpu_times_ms);
    if (stats.gpu_times_ms.empty())
    {
        return stats;
    }

    std::sort(stats.gpu_times_ms.begin(), stats.gpu_times_ms.end());
    stats.min_time_ms = stats.gpu_times_ms.front();
    stats.max_time_ms = stats.gpu_times_ms.back();
    const std::size_t middle_index = stats.gpu_times_ms.size() / 2;
    if ((stats.gpu_times_ms.size() % 2U) == 0U)
    {
        stats.median_time_ms = (stats.gpu_times_ms[middle_index - 1] + stats.gpu_times_ms[middle_index]) / 2.0;
    }
    else
    {
        stats.median_time_ms = stats.gpu_times_ms[middle_index];
    }

    const double total = std::accumulate(stats.gpu_times_ms.begin(), stats.gpu_times_ms.end(), 0.0);
    stats.mean_time_ms = total / static_cast<double>(stats.gpu_times_ms.size());

    const double operations = 2.0 * static_cast<double>(config.shape.m) * static_cast<double>(config.shape.n) * static_cast<double>(config.shape.k);
    const double mean_time_s = stats.mean_time_ms / 1000.0;
    if (mean_time_s > 0.0)
    {
        stats.gflops_mean = operations / mean_time_s / 1.0e9;
    }

    return stats;
}
}

Status ValidateTimingSupport(const VulkanContext& context)
{
    if (context.device_info().timestamp_valid_bits == 0)
    {
        return MakeStatus(false, "Selected compute queue family does not support Vulkan timestamp queries required by this benchmark.");
    }

    if (context.device_info().timestamp_period <= 0.0f)
    {
        return MakeStatus(false, "Selected Vulkan device reported a non-positive timestamp period, so GPU timing is unavailable.");
    }

    return MakeStatus(true, "");
}

BenchmarkRunResult RunVulkanSgemm(
    VulkanContext& context,
    const BenchmarkConfig& config,
    const std::vector<float>& a,
    const std::vector<float>& b,
    const std::string& shader_path)
{
    BenchmarkRunResult result;
    ComputeResources resources;

    result.status = ValidateTimingSupport(context);
    if (!result.status.ok)
    {
        return result;
    }

    const VkDeviceSize a_size = static_cast<VkDeviceSize>(a.size() * sizeof(float));
    const VkDeviceSize b_size = static_cast<VkDeviceSize>(b.size() * sizeof(float));
    const VkDeviceSize c_size = static_cast<VkDeviceSize>(static_cast<std::size_t>(config.shape.m) * config.shape.n * sizeof(float));

    result.status = CreateMappedStorageBuffer(context, a_size, resources.a_buffer);
    if (!result.status.ok)
    {
        DestroyResources(context.device(), resources);
        return result;
    }

    result.status = CreateMappedStorageBuffer(context, b_size, resources.b_buffer);
    if (!result.status.ok)
    {
        DestroyResources(context.device(), resources);
        return result;
    }

    result.status = CreateMappedStorageBuffer(context, c_size, resources.c_buffer);
    if (!result.status.ok)
    {
        DestroyResources(context.device(), resources);
        return result;
    }

    std::memcpy(resources.a_buffer.mapped, a.data(), static_cast<std::size_t>(a_size));
    std::memcpy(resources.b_buffer.mapped, b.data(), static_cast<std::size_t>(b_size));
    std::memset(resources.c_buffer.mapped, 0, static_cast<std::size_t>(c_size));

    result.status = CreateDescriptorSetLayout(context.device(), resources);
    if (!result.status.ok)
    {
        DestroyResources(context.device(), resources);
        return result;
    }

    result.status = CreateDescriptorResources(context.device(), resources);
    if (!result.status.ok)
    {
        DestroyResources(context.device(), resources);
        return result;
    }

    result.status = CreatePipeline(context, shader_path, resources);
    if (!result.status.ok)
    {
        DestroyResources(context.device(), resources);
        return result;
    }

    result.status = CreateCommandResources(context, resources);
    if (!result.status.ok)
    {
        DestroyResources(context.device(), resources);
        return result;
    }

    result.status = RecordCommandBuffer(config, resources);
    if (!result.status.ok)
    {
        DestroyResources(context.device(), resources);
        return result;
    }

    for (std::uint32_t iteration = 0; iteration < config.warmup_iterations; ++iteration)
    {
        result.status = SubmitAndWait(context, resources);
        if (!result.status.ok)
        {
            DestroyResources(context.device(), resources);
            return result;
        }
    }

    std::vector<double> gpu_times_ms;
    gpu_times_ms.reserve(config.measured_iterations);
    for (std::uint32_t iteration = 0; iteration < config.measured_iterations; ++iteration)
    {
        result.status = SubmitAndWait(context, resources);
        if (!result.status.ok)
        {
            DestroyResources(context.device(), resources);
            return result;
        }

        double time_ms = 0.0;
        result.status = ReadTimestampMs(context, resources, time_ms);
        if (!result.status.ok)
        {
            DestroyResources(context.device(), resources);
            return result;
        }

        gpu_times_ms.push_back(time_ms);
    }

    result.output.resize(static_cast<std::size_t>(config.shape.m) * config.shape.n);
    std::memcpy(result.output.data(), resources.c_buffer.mapped, static_cast<std::size_t>(c_size));
    result.stats = MakeStats(config, std::move(gpu_times_ms));
    result.status = MakeStatus(true, "");

    DestroyResources(context.device(), resources);
    return result;
}
