/**
 * @file Core/Math/ValueNoise.h
 * @brief 시드 고정 노이즈(1D · 2D · 3D 값 노이즈와 1D 펄린)입니다. 지형 높이, 바이옴, 구름, 바람, 카메라 흔들림, 메시 변형처럼 부드럽게 변하는 절차 값을 만듭니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    struct float3;

    /**
     * @struct ValueNoise
     * @brief 격자 꼭짓점에 정수 해시로 값을 두고 smoothstep 으로 보간합니다. 같은 시드면 어느 기계에서나 같은 값입니다.
     * @details 장르를 가리지 않는 기반이다(복셀 지형, 농장의 들판 높낮이, 공원 지형, 2D 동굴). 언리얼 `FMath::PerlinNoise1D/2D/3D` 처럼 Core 에 둔다.
     *          차원마다 격자 해시가 다르다. 모두 축약을 끈 결정성 TU(`sw_mark_deterministic_sources`)의 줄 밖 함수라 구성 · 플랫폼을 넘어 같은 비트다.
     *          카메라 흔들림(1D 펄린) · 메시 모디파이어(1D · 3D)가 이것만 부른다. 같은 식을 다른 TU 에 따로 두지 않는다.
     */
    struct SW_API ValueNoise
    {
        /** @brief 정수 격자점의 값(0..1)입니다. `HashUtil::hashCoord` 를 씁니다. */
        static float32 hashLattice( int32 x, int32 z, uint32 seed );
        /** @brief 한 옥타브(0..1)입니다. */
        static float32 sampleValue( float32 x, float32 z, uint32 seed );
        /** @brief 옥타브를 겹친 값(0..1)입니다. 옥타브마다 주파수 ×2 · 진폭 ×0.5. */
        static float32 sampleFractal( float32 x, float32 z, uint32 seed, int32 octaveCount );

        /** @brief 1D 값 노이즈(-1..1)입니다. 정수 칸 사이를 smoothstep 으로 보간합니다. x 는 ±1e6 으로 자릅니다. */
        static float32 sampleValue1D( float32 x, uint32 seed );
        /** @brief 3D 값 노이즈(-1..1)입니다. 격자점 해시(murmur3 마무리 섞기)를 smoothstep 삼선형 보간합니다. */
        static float32 sampleValue3D( const float3& point, uint32 seed );
        /** @brief 1D 펄린(기울기) 노이즈(-1..1)입니다. 정수 격자점에서 0 이고, 페이드는 6t⁵ − 15t⁴ + 10t³ 입니다. */
        static float32 samplePerlin1D( float32 x, uint32 seed );
    };
} // namespace sw
