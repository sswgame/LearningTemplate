/**
 * @file ValueNoise.h
 * @brief 씨앗 고정 2D 값 노이즈 — 지형 높이 · 바이옴 · 구름 · 바람처럼 부드럽게 변하는 절차 값입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @struct ValueNoise
     * @brief 격자 꼭짓점에 정수 해시(`GameHash`)로 값을 두고 smoothstep 으로 잇습니다. 같은 씨앗이면 어느 기계에서나 같은 값입니다.
     * @details 장르를 가리지 않는 기반이다(복셀 지형 · 농장의 들판 높낮이 · 공원 지형 · 2D 동굴).
     */
    struct SW_GF_API ValueNoise
    {
        /** @brief 정수 격자점의 값(0..1)입니다. */
        static float32 hashLattice( int32 x, int32 z, uint32 seed );
        /** @brief 한 옥타브(0..1)입니다. */
        static float32 sampleValue( float32 x, float32 z, uint32 seed );
        /** @brief 옥타브를 겹친 값(0..1)입니다. 옥타브마다 주파수 ×2 · 진폭 ×0.5. */
        static float32 sampleFractal( float32 x, float32 z, uint32 seed, int32 octaveCount );
    };
} // namespace sw
