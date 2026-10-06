/**
 * @file SpirvConstants.h
 * @brief SPIR-V 명세의 번호(매직 · 연산 · 장식 · 저장 클래스 · 내장 변수)입니다. 리플렉션과 GL 패치가 같은 표를 봅니다.
 * @details 값은 Khronos SPIR-V 명세(spirv.core.grammar.json)의 것이다 — 바꾸지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw::spirv
{
    inline constexpr uint32 kMagic = 0x07230203u;

    inline constexpr uint32 kOpName             = 5u;
    inline constexpr uint32 kOpMemberName       = 6u;
    inline constexpr uint32 kOpEntryPoint       = 15u;
    inline constexpr uint32 kOpTypeBool         = 20u;
    inline constexpr uint32 kOpTypeInt          = 21u;
    inline constexpr uint32 kOpTypeFloat        = 22u;
    inline constexpr uint32 kOpTypeVector       = 23u;
    inline constexpr uint32 kOpTypeMatrix       = 24u;
    inline constexpr uint32 kOpTypeImage        = 25u;
    inline constexpr uint32 kOpTypeSampler      = 26u;
    inline constexpr uint32 kOpTypeSampledImage = 27u;
    inline constexpr uint32 kOpTypeArray        = 28u;
    inline constexpr uint32 kOpTypeRuntimeArray = 29u;
    inline constexpr uint32 kOpTypeStruct       = 30u;
    inline constexpr uint32 kOpTypePointer      = 32u;
    inline constexpr uint32 kOpConstant         = 43u;
    inline constexpr uint32 kOpVariable         = 59u;
    inline constexpr uint32 kOpDecorate         = 71u;
    inline constexpr uint32 kOpMemberDecorate   = 72u;

    /** @brief SPIR-V 1.3 이하: Uniform 클래스 + BufferBlock = SSBO 입니다. */
    inline constexpr uint32 kDecorationBufferBlock   = 3u;
    inline constexpr uint32 kDecorationArrayStride   = 6u;
    inline constexpr uint32 kDecorationBuiltIn       = 11u;
    inline constexpr uint32 kDecorationLocation      = 30u;
    inline constexpr uint32 kDecorationBinding       = 33u;
    inline constexpr uint32 kDecorationDescriptorSet = 34u;
    inline constexpr uint32 kDecorationOffset        = 35u;

    inline constexpr uint32 kExecutionModelVertex = 0u;

    inline constexpr uint32 kStorageClassUniformConstant = 0u;
    inline constexpr uint32 kStorageClassInput           = 1u;
    inline constexpr uint32 kStorageClassUniform         = 2u;
    inline constexpr uint32 kStorageClassStorageBuffer   = 12u;

    inline constexpr uint32 kBuiltInVertexId      = 5u;
    inline constexpr uint32 kBuiltInInstanceId    = 6u;
    inline constexpr uint32 kBuiltInVertexIndex   = 42u;
    inline constexpr uint32 kBuiltInInstanceIndex = 43u;
} // namespace sw::spirv
