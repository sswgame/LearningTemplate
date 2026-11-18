# astc-encoder 코어 코덱만 짓는다(원래 Source/cmake_core.cmake 의 정적 라이브러리 목록과 같다).
# SIMD 는 컴파일러 기본(x64 SSE2 · arm64 NEON)을 astcenc_mathlib.h 가 고른다 — -march 를 주지 않아 빌드 기계와 상관없이 같은 바이너리다.
# 부동 소수 불변(ASTCENC_INVARIANCE 기본 ON)과 같은 컴파일 옵션을 준다 — ISA · 컴파일러가 달라도 같은 블록을 낸다(쿠킹 결과가 DDC 키로 쓰인다).
cmake_minimum_required(VERSION 3.16)
project(astc-encoder CXX)

set(source_dir "${CMAKE_CURRENT_LIST_DIR}/../Source")

add_library(astcenc STATIC
    ${source_dir}/astcenc_averages_and_directions.cpp
    ${source_dir}/astcenc_block_sizes.cpp
    ${source_dir}/astcenc_color_quantize.cpp
    ${source_dir}/astcenc_color_unquantize.cpp
    ${source_dir}/astcenc_compress_symbolic.cpp
    ${source_dir}/astcenc_compute_variance.cpp
    ${source_dir}/astcenc_decompress_symbolic.cpp
    ${source_dir}/astcenc_diagnostic_trace.cpp
    ${source_dir}/astcenc_entry.cpp
    ${source_dir}/astcenc_find_best_partitioning.cpp
    ${source_dir}/astcenc_ideal_endpoints_and_weights.cpp
    ${source_dir}/astcenc_image.cpp
    ${source_dir}/astcenc_integer_sequence.cpp
    ${source_dir}/astcenc_mathlib.cpp
    ${source_dir}/astcenc_mathlib_softfloat.cpp
    ${source_dir}/astcenc_partition_tables.cpp
    ${source_dir}/astcenc_percentile_tables.cpp
    ${source_dir}/astcenc_pick_best_endpoint_format.cpp
    ${source_dir}/astcenc_quantization.cpp
    ${source_dir}/astcenc_symbolic_physical.cpp
    ${source_dir}/astcenc_weight_align.cpp
    ${source_dir}/astcenc_weight_quant_xfer_tables.cpp)

target_compile_features(astcenc PRIVATE cxx_std_14)
target_include_directories(astcenc PUBLIC
    $<BUILD_INTERFACE:${source_dir}>
    $<INSTALL_INTERFACE:include/astcenc>)

if(MSVC)
    target_compile_definitions(astcenc PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(astcenc PRIVATE /fp:precise)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(astcenc PRIVATE "SHELL:-Xclang -ffp-contract=off")
    endif()
else()
    target_compile_options(astcenc PRIVATE -ffp-contract=off -fno-math-errno)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(astcenc PRIVATE -fno-unsafe-math-optimizations)
    endif()
endif()

install(TARGETS astcenc EXPORT unofficial-astc-encoder-targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)
install(FILES ${source_dir}/astcenc.h DESTINATION include/astcenc)
install(EXPORT unofficial-astc-encoder-targets
    NAMESPACE unofficial::astc-encoder::
    FILE unofficial-astc-encoder-config.cmake
    DESTINATION share/unofficial-astc-encoder)
