#include "sgemm_reference.h"

#include <algorithm>
#include <cmath>

std::vector<float> MakeDeterministicMatrix(const std::size_t rows, const std::size_t cols, const std::uint32_t seed_offset)
{
    std::vector<float> values(rows * cols, 0.0f);

    for (std::size_t row = 0; row < rows; ++row)
    {
        for (std::size_t col = 0; col < cols; ++col)
        {
            const std::size_t index = row * cols + col;
            const std::uint32_t value = static_cast<std::uint32_t>((index + seed_offset * 17U) % 29U);
            values[index] = (static_cast<float>(value) - 14.0f) / 7.0f;
        }
    }

    return values;
}

std::vector<float> ReferenceSgemm(const std::vector<float>& a, const std::vector<float>& b, const MatrixShape& shape)
{
    std::vector<float> c(static_cast<std::size_t>(shape.m) * static_cast<std::size_t>(shape.n), 0.0f);

    for (std::uint32_t row = 0; row < shape.m; ++row)
    {
        for (std::uint32_t col = 0; col < shape.n; ++col)
        {
            float sum = 0.0f;
            for (std::uint32_t inner = 0; inner < shape.k; ++inner)
            {
                const std::size_t a_index = static_cast<std::size_t>(row) * shape.k + inner;
                const std::size_t b_index = static_cast<std::size_t>(inner) * shape.n + col;
                sum += a[a_index] * b[b_index];
            }

            c[static_cast<std::size_t>(row) * shape.n + col] = sum;
        }
    }

    return c;
}

ErrorMetrics CompareMatrices(
    const std::vector<float>& expected,
    const std::vector<float>& actual,
    const float abs_tolerance,
    const float rel_tolerance)
{
    ErrorMetrics metrics;
    metrics.passed = expected.size() == actual.size();

    if (!metrics.passed)
    {
        return metrics;
    }

    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        const float abs_error = std::fabs(expected[index] - actual[index]);
        const float denom = std::max(std::fabs(expected[index]), 1.0f);
        const float rel_error = abs_error / denom;

        metrics.max_abs_error = std::max(metrics.max_abs_error, abs_error);
        metrics.max_rel_error = std::max(metrics.max_rel_error, rel_error);
    }

    metrics.passed = metrics.max_abs_error <= abs_tolerance && metrics.max_rel_error <= rel_tolerance;
    return metrics;
}
