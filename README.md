# Vulkan SGEMM Bench (M0)

This repository now contains an M0 Vulkan-first FP32 SGEMM benchmark prototype.

## What M0 does

M0 implements a correctness-first SGEMM path on Vulkan compute:

- `C = A * B` for runtime `M`, `N`, and `K`
- row-major matrices throughout the CPU and Vulkan paths
- one output element per shader invocation in a naive FP32 compute shader
- CPU reference validation in plain C++
- GPU timing with Vulkan timestamp queries
- benchmark fails clearly on devices/queue families without required timestamp query support
- machine-readable JSON benchmark artifacts
- a small built-in self-test covering:
  - `32x32x32`
  - `64x64x64`
  - `64x48x80`

## Current limitations

This is intentionally a small M0 scaffold:

- FP32 only
- no tiling or shared-memory optimization yet
- no subgroup optimizations yet
- no transpose variants
- no alpha/beta parameters
- no batched GEMM
- no vendor-specific fast paths
- no plotting or external benchmark framework
- buffers are allocated from simple host-visible Vulkan memory for readability
- correctness uses absolute tolerance `1e-4` and relative tolerance `1e-4`
- relative error uses `abs_error / max(abs(reference), 1.0)`, so near-zero reference values fall back to a stable denominator of `1.0`

## Build

Requirements:

- CMake 3.20+
- a C++20 compiler
- Vulkan loader and headers (`libvulkan-dev` on Debian/Ubuntu)
- `glslangValidator` for compiling the Vulkan compute shader during the build

The build compiles `shaders/sgemm_naive.comp` with `glslangValidator`; there is no checked-in SPIR-V fallback. The runtime benchmark still requires Vulkan timestamp query support on the selected compute queue family and fails fast if that timing path is unavailable.

```bash
cmake -S . -B build
cmake --build build
```

## Run benchmark

Example:

```bash
./build/vulkan_sgemm_bench --m 512 --n 512 --k 512 --warmup 3 --iters 10 --output results.json
```

CLI flags:

- `--m`, `--n`, `--k`: matrix sizes
- `--warmup`: warmup dispatch count
- `--iters`: measured dispatch count
- `--output`: JSON artifact path
- `--self-test`: run the built-in correctness cases instead of a single benchmark

Example self-test run:

```bash
./build/vulkan_sgemm_bench --self-test --output self_test_results.json
```

## Artifact format

Single benchmark runs emit JSON with at least these fields:

- `gpu_name`
- `vendor_id`
- `device_id`
- `vulkan_api_version`
- `M`, `N`, `K`
- `warmup_iterations`
- `measured_iterations`
- `min_time_ms`
- `median_time_ms`
- `mean_time_ms`
- `max_time_ms`
- `gflops_mean`
- `max_abs_error`
- `max_rel_error`
- `correctness_pass`
- `timestamp_period`

If timestamp query support is unavailable on the selected compute queue family, the benchmark exits with a clear error before emitting a benchmark artifact.

## M1 is intentionally deferred

M1 can focus on performance-oriented follow-up work such as tiling, better memory placement, and more detailed device/feature handling. None of that is added here yet because M0 is only meant to establish correctness, timing, and artifact plumbing.
