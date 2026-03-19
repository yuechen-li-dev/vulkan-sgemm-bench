cmake_minimum_required(VERSION 3.20)
project(vulkan_sgemm_bench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)

enable_testing()

add_executable(vulkan_sgemm_bench
    src/benchmark_main.cpp
    src/sgemm_reference.cpp
    src/sgemm_vulkan.cpp
    src/vulkan_context.cpp
)

target_include_directories(vulkan_sgemm_bench PRIVATE src)
target_link_libraries(vulkan_sgemm_bench PRIVATE Vulkan::Vulkan)

target_compile_options(vulkan_sgemm_bench PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

set(SGEMM_SHADER_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sgemm_naive.comp)

find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/sgemm_naive.comp.spv
    COMMAND ${GLSLANG_VALIDATOR} -V ${SGEMM_SHADER_SOURCE} -o ${CMAKE_CURRENT_BINARY_DIR}/sgemm_naive.comp.spv
    DEPENDS ${SGEMM_SHADER_SOURCE}
    COMMENT "Compiling Vulkan compute shader"
    VERBATIM
)

add_custom_target(vulkan_sgemm_shader ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/sgemm_naive.comp.spv)
add_dependencies(vulkan_sgemm_bench vulkan_sgemm_shader)

target_compile_definitions(vulkan_sgemm_bench PRIVATE
    SGEMM_SHADER_PATH="$<SHELL_PATH:${CMAKE_CURRENT_BINARY_DIR}/sgemm_naive.comp.spv>"
)

add_test(
    NAME vulkan_sgemm_self_test
    COMMAND vulkan_sgemm_bench --self-test --output ${CMAKE_CURRENT_BINARY_DIR}/self_test_results.json
)
