#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "sgemm_reference.h"
#include "vulkan_context.h"

struct BenchmarkConfig
{
    MatrixShape shape;
    std::uint32_t warmup_iterations = 2;
    std::uint32_t measured_iterations = 10;
};

struct BenchmarkStats
{
    std::vector<double> gpu_times_ms;
    double min_time_ms = 0.0;
    double median_time_ms = 0.0;
    double mean_time_ms = 0.0;
    double max_time_ms = 0.0;
    double gflops_mean = 0.0;
};

struct BenchmarkRunResult
{
    Status status;
    std::vector<float> output;
    BenchmarkStats stats;
};

BenchmarkRunResult RunVulkanSgemm(
    VulkanContext& context,
    const BenchmarkConfig& config,
    const std::vector<float>& a,
    const std::vector<float>& b,
    const std::string& shader_path);
