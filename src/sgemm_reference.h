#pragma once

#include <cstdint>
#include <vector>

struct MatrixShape
{
    std::uint32_t m = 0;
    std::uint32_t n = 0;
    std::uint32_t k = 0;
};

struct ErrorMetrics
{
    float max_abs_error = 0.0f;
    float max_rel_error = 0.0f;
    bool passed = false;
};

std::vector<float> MakeDeterministicMatrix(std::size_t rows, std::size_t cols, std::uint32_t seed_offset);
std::vector<float> ReferenceSgemm(const std::vector<float>& a, const std::vector<float>& b, const MatrixShape& shape);
ErrorMetrics CompareMatrices(const std::vector<float>& expected, const std::vector<float>& actual, float abs_tolerance, float rel_tolerance);
