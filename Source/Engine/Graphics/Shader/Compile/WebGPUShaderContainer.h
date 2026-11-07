/**
 * @file WebGPUShaderContainer.h
 * @brief WebGPU 셰이더 바이너리(`.wgsl`) 꾸러미입니다 — DXC 가 낸 SPIR-V 와 Tint 가 옮긴 WGSL 을 함께 담습니다.
 * @details SPIR-V 를 남기는 까닭: 엔진의 리플렉션(머티리얼 패킹 · 정점 입력 · 바인딩 검사)은 SPIR-V 를 읽는다. WGSL 리플렉션을 따로 두면 같은 사실을 두 벌로 읽는다.
 *          WGSL 은 장치가 셰이더 모듈로 받는 글이다. 언리얼도 WebGPU 용 셰이더를 SPIR-V 를 거쳐 만들고 리플렉션을 SPIR-V 에서 뽑는다(기억).
 *          배치: `[u32 마법수 'SWGS'][u32 판][u32 SPIR-V 바이트 수][SPIR-V][u32 WGSL 바이트 수][WGSL]` — 모두 리틀 엔디언.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief WebGPU 셰이더 꾸러미를 짓고 읽습니다. 쿠커(ShaderCompiler)와 WebGPU 백엔드 · 리플렉션이 함께 씁니다. */
    struct SW_API WebGPUShaderContainer
    {
        static constexpr uint32 kMagic   = 0x53475753u; ///< 'SWGS'
        static constexpr uint32 kVersion = 1u;          ///< 배치가 바뀌면 올린다(쿠킹 지문이 바이트를 섞어 옛 꾸러미는 다시 굽는다)

        /** @brief @p spirv 와 @p wgsl 을 한 꾸러미로 묶어 @p outBytes 를 채웁니다. */
        static void pack( vector_reference<const uint8> spirv, string_view wgsl, vector<uint8>& outBytes );
        /** @brief 꾸러미 안의 SPIR-V 를 돌려줍니다(복사하지 않는다 — @p bytes 가 살아 있는 동안만 쓴다). 꾸러미가 아니거나 깨졌으면 false 입니다. */
        [[nodiscard]] static bool findSpirv( vector_reference<const uint8> bytes, vector_reference<const uint8>& outSpirv );
        /** @brief 꾸러미 안의 WGSL 글을 돌려줍니다(@p bytes 를 가리킨다). 꾸러미가 아니거나 깨졌으면 false 입니다. */
        [[nodiscard]] static bool findWgsl( vector_reference<const uint8> bytes, string_view& outWgsl );
        /**
         * @brief WGSL 글이 선언한 그룹 0 의 바인딩 번호를 모읍니다(`@group(0) @binding(N)`, 오름차순 · 중복 없음).
         * @details 파이프라인은 자동 레이아웃(`layout: auto`)이라 셰이더가 실제로 쓰는 바인딩만 그룹에 든다 — 바인드 그룹은 그 번호마다 정확히 한 항목이어야 한다.
         *          Tint 는 쓰지 않는 자원을 내보내지 않으므로 이 목록이 곧 자동 레이아웃의 항목이다.
         */
        static void collectBindings( string_view wgsl, vector<uint32>& outListBinding );
    };
} // namespace sw
