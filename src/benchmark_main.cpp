#include "sgemm_reference.h"
#include "sgemm_vulkan.h"
#include "vulkan_context.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace
{
constexpr float kAbsTolerance = 1.0e-4f;
constexpr float kRelTolerance = 1.0e-4f;

struct CliOptions
{
    BenchmarkConfig config;
    std::string output_path = "benchmark_results.json";
    bool self_test = false;
};

struct FullRunResult
{
    DeviceInfo device_info;
    BenchmarkConfig config;
    BenchmarkStats stats;
    ErrorMetrics errors;
};

struct ParseResult
{
    bool ok = false;
    CliOptions options;
    std::string message;
};

Status MakeStatus(const bool ok, std::string message)
{
    Status status;
    status.ok = ok;
    status.message = std::move(message);
    return status;
}

std::optional<std::uint32_t> ParseUnsigned(const std::string& text)
{
    if (text.empty())
    {
        return std::nullopt;
    }

    std::uint64_t value = 0;
    for (char ch : text)
    {
        if (ch < '0' || ch > '9')
        {
            return std::nullopt;
        }

        value = value * 10U + static_cast<std::uint64_t>(ch - '0');
        if (value > 0xffffffffULL)
        {
            return std::nullopt;
        }
    }

    return static_cast<std::uint32_t>(value);
}

ParseResult ParseArguments(const int argc, char** argv)
{
    CliOptions options;
    options.config.shape = {256, 256, 256};

    for (int index = 1; index < argc; ++index)
    {
        const std::string arg = argv[index];
        const auto read_value = [&](const std::string&) -> std::optional<std::string>
        {
            if (index + 1 >= argc)
            {
                return std::nullopt;
            }

            ++index;
            return std::string(argv[index]);
        };

        if (arg == "--help" || arg == "-h")
        {
            return {false, {}, "Usage: vulkan_sgemm_bench [--m M] [--n N] [--k K] [--warmup ITERS] [--iters ITERS] [--output PATH] [--self-test]"};
        }
        if (arg == "--self-test")
        {
            options.self_test = true;
            continue;
        }

        if (arg == "--m")
        {
            const std::optional<std::string> value = read_value(arg);
            if (!value.has_value())
            {
                return {false, {}, "Missing value for --m."};
            }
            const std::optional<std::uint32_t> parsed = ParseUnsigned(*value);
            if (!parsed.has_value())
            {
                return {false, {}, "Invalid unsigned integer for --m."};
            }
            options.config.shape.m = *parsed;
            continue;
        }
        if (arg == "--n")
        {
            const std::optional<std::string> value = read_value(arg);
            if (!value.has_value())
            {
                return {false, {}, "Missing value for --n."};
            }
            const std::optional<std::uint32_t> parsed = ParseUnsigned(*value);
            if (!parsed.has_value())
            {
                return {false, {}, "Invalid unsigned integer for --n."};
            }
            options.config.shape.n = *parsed;
            continue;
        }
        if (arg == "--k")
        {
            const std::optional<std::string> value = read_value(arg);
            if (!value.has_value())
            {
                return {false, {}, "Missing value for --k."};
            }
            const std::optional<std::uint32_t> parsed = ParseUnsigned(*value);
            if (!parsed.has_value())
            {
                return {false, {}, "Invalid unsigned integer for --k."};
            }
            options.config.shape.k = *parsed;
            continue;
        }
        if (arg == "--warmup")
        {
            const std::optional<std::string> value = read_value(arg);
            if (!value.has_value())
            {
                return {false, {}, "Missing value for --warmup."};
            }
            const std::optional<std::uint32_t> parsed = ParseUnsigned(*value);
            if (!parsed.has_value())
            {
                return {false, {}, "Invalid unsigned integer for --warmup."};
            }
            options.config.warmup_iterations = *parsed;
            continue;
        }
        if (arg == "--iters")
        {
            const std::optional<std::string> value = read_value(arg);
            if (!value.has_value())
            {
                return {false, {}, "Missing value for --iters."};
            }
            const std::optional<std::uint32_t> parsed = ParseUnsigned(*value);
            if (!parsed.has_value())
            {
                return {false, {}, "Invalid unsigned integer for --iters."};
            }
            options.config.measured_iterations = *parsed;
            continue;
        }
        if (arg == "--output")
        {
            const std::optional<std::string> value = read_value(arg);
            if (!value.has_value())
            {
                return {false, {}, "Missing value for --output."};
            }
            options.output_path = *value;
            continue;
        }

        return {false, {}, "Unknown argument: " + arg};
    }

    if (options.config.shape.m == 0 || options.config.shape.n == 0 || options.config.shape.k == 0)
    {
        return {false, {}, "M, N, and K must be greater than zero."};
    }

    if (options.config.measured_iterations == 0)
    {
        return {false, {}, "--iters must be greater than zero."};
    }

    return {true, options, ""};
}

Status WriteJsonArtifact(const std::string& output_path, const FullRunResult& result)
{
    std::ofstream output(output_path, std::ios::binary);
    if (!output.is_open())
    {
        return MakeStatus(false, "Failed to open output artifact path: " + output_path);
    }

    output << "{\n";
    output << "  \"gpu_name\": \"" << result.device_info.gpu_name << "\",\n";
    output << "  \"vendor_id\": " << result.device_info.vendor_id << ",\n";
    output << "  \"device_id\": " << result.device_info.device_id << ",\n";
    output << "  \"vulkan_api_version\": \"" << FormatVulkanApiVersion(result.device_info.api_version) << "\",\n";
    output << "  \"M\": " << result.config.shape.m << ",\n";
    output << "  \"N\": " << result.config.shape.n << ",\n";
    output << "  \"K\": " << result.config.shape.k << ",\n";
    output << "  \"warmup_iterations\": " << result.config.warmup_iterations << ",\n";
    output << "  \"measured_iterations\": " << result.config.measured_iterations << ",\n";
    output << "  \"min_time_ms\": " << result.stats.min_time_ms << ",\n";
    output << "  \"median_time_ms\": " << result.stats.median_time_ms << ",\n";
    output << "  \"mean_time_ms\": " << result.stats.mean_time_ms << ",\n";
    output << "  \"max_time_ms\": " << result.stats.max_time_ms << ",\n";
    output << "  \"gflops_mean\": " << result.stats.gflops_mean << ",\n";
    output << "  \"max_abs_error\": " << result.errors.max_abs_error << ",\n";
    output << "  \"max_rel_error\": " << result.errors.max_rel_error << ",\n";
    output << "  \"correctness_pass\": " << (result.errors.passed ? "true" : "false") << ",\n";
    output << "  \"timestamp_period\": " << result.device_info.timestamp_period << "\n";
    output << "}\n";

    return MakeStatus(true, "");
}

Status RunSingleBenchmark(VulkanContext& context, const BenchmarkConfig& config, const std::optional<std::string>& output_path, FullRunResult& out_result)
{
    const std::vector<float> a = MakeDeterministicMatrix(config.shape.m, config.shape.k, 1);
    const std::vector<float> b = MakeDeterministicMatrix(config.shape.k, config.shape.n, 2);
    const std::vector<float> reference = ReferenceSgemm(a, b, config.shape);

    BenchmarkRunResult gpu_result = RunVulkanSgemm(context, config, a, b, SGEMM_SHADER_PATH);
    if (!gpu_result.status.ok)
    {
        return gpu_result.status;
    }

    out_result.device_info = context.device_info();
    out_result.config = config;
    out_result.stats = gpu_result.stats;
    out_result.errors = CompareMatrices(reference, gpu_result.output, kAbsTolerance, kRelTolerance);

    if (output_path.has_value())
    {
        Status status = WriteJsonArtifact(*output_path, out_result);
        if (!status.ok)
        {
            return status;
        }
    }

    if (!out_result.errors.passed)
    {
        std::ostringstream stream;
        stream << "Correctness validation failed. max_abs_error=" << out_result.errors.max_abs_error
               << ", max_rel_error=" << out_result.errors.max_rel_error;
        return MakeStatus(false, stream.str());
    }

    return MakeStatus(true, "");
}

Status RunSelfTest(VulkanContext& context, const std::string& output_path)
{
    const std::vector<MatrixShape> shapes = {
        {32, 32, 32},
        {64, 64, 64},
        {64, 48, 80}};

    std::ofstream output(output_path, std::ios::binary);
    if (!output.is_open())
    {
        return MakeStatus(false, "Failed to open self-test output artifact path: " + output_path);
    }

    output << "[\n";

    for (std::size_t index = 0; index < shapes.size(); ++index)
    {
        BenchmarkConfig config;
        config.shape = shapes[index];
        config.warmup_iterations = 1;
        config.measured_iterations = 3;

        FullRunResult result;
        Status status = RunSingleBenchmark(context, config, std::nullopt, result);
        if (!status.ok)
        {
            return status;
        }

        output << "  {\n";
        output << "    \"M\": " << result.config.shape.m << ",\n";
        output << "    \"N\": " << result.config.shape.n << ",\n";
        output << "    \"K\": " << result.config.shape.k << ",\n";
        output << "    \"max_abs_error\": " << result.errors.max_abs_error << ",\n";
        output << "    \"max_rel_error\": " << result.errors.max_rel_error << ",\n";
        output << "    \"correctness_pass\": " << (result.errors.passed ? "true" : "false") << "\n";
        output << "  }";
        if (index + 1 != shapes.size())
        {
            output << ",";
        }
        output << "\n";
    }

    output << "]\n";
    return MakeStatus(true, "");
}
}

int main(int argc, char** argv)
{
    const ParseResult parse_result = ParseArguments(argc, argv);
    if (!parse_result.ok)
    {
        std::cerr << parse_result.message << "\n";
        return (parse_result.message.rfind("Usage:", 0) == 0) ? 0 : 1;
    }

    VulkanContext context;
    Status status = context.Initialize();
    if (!status.ok)
    {
        std::cerr << status.message << "\n";
        return 1;
    }

    if (parse_result.options.self_test)
    {
        status = RunSelfTest(context, parse_result.options.output_path);
        if (!status.ok)
        {
            std::cerr << status.message << "\n";
            return 1;
        }

        std::cout << "Self-test passed on GPU: " << context.device_info().gpu_name << "\n";
        return 0;
    }

    FullRunResult result;
    status = RunSingleBenchmark(context, parse_result.options.config, parse_result.options.output_path, result);
    if (!status.ok)
    {
        std::cerr << status.message << "\n";
        return 1;
    }

    std::cout << "GPU: " << result.device_info.gpu_name << "\n";
    std::cout << "Vulkan API: " << FormatVulkanApiVersion(result.device_info.api_version) << "\n";
    std::cout << "Shape: M=" << result.config.shape.m << " N=" << result.config.shape.n << " K=" << result.config.shape.k << "\n";
    std::cout << "Mean GPU time (ms): " << result.stats.mean_time_ms << "\n";
    std::cout << "Mean GFLOP/s: " << result.stats.gflops_mean << "\n";
    std::cout << "Max abs error: " << result.errors.max_abs_error << "\n";
    std::cout << "Max rel error: " << result.errors.max_rel_error << "\n";
    std::cout << "Artifact: " << parse_result.options.output_path << "\n";
    return 0;
}
