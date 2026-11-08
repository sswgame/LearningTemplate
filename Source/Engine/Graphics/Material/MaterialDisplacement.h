/**
 * @file MaterialDisplacement.h
 * @brief 머티리얼 변위(`MATERIAL_DISPLACEMENT`)의 테셀레이션 계수 · 변위 식입니다. 셰이더 짝은 `material_displacement.hlsli` 의 `Displacement` 이고 식이 같아야 합니다.
 * @details 셰이더는 단위 테스트가 돌릴 수 없어 같은 식을 여기 두고 테스트가 성질(틈 없음 · 상한 · 거리)을 지킨다. 한쪽을 고치면 다른 쪽도 고친다.
 *          언리얼(Nanite 이전의 테셀레이션 · 지금의 Nanite 변위)도 변 계수를 변의 두 끝점만으로 정해 이웃 패치가 같은 값을 낸다(기억에 의존한 비교).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @brief 변위 테셀레이션의 계수와 변위 길이입니다. 셰이더 `Displacement` 와 같은 식입니다. */
    struct MaterialDisplacement
    {
        /** @brief 테셀레이션 계수의 상한입니다. D3D11 · Vulkan(`maxTessellationGenerationLevel` 최소 보장)이 받는 값이고 셰이더 `[maxtessfactor]` 와 같습니다. */
        static constexpr float32 kMaxTessFactor = 64.0f;
        /** @brief 거리 나눗셈의 바닥(m)입니다. 카메라에 붙은 변이 무한 계수를 내지 않게 합니다. */
        static constexpr float32 kMinViewDepth = 0.05f;

        /**
         * @brief 변 하나의 테셀레이션 계수입니다 — 변의 월드 길이를 두 끝점의 평균 뷰 깊이로 나눈 각 크기(rad) × 밀도(rad 당 분할 수)를 [1, 상한] 으로 자릅니다.
         * @details 두 끝점만 읽고 순서를 바꿔도 같은 값이라(덧셈 · 거리 제곱이 교환적이다) 변을 나누는 두 패치가 같은 계수를 내어 틈이 없다.
         * @param viewDepth0 끝점 0 의 카메라 뷰 깊이(m, 카메라 뷰-투영의 w). 그림자 패스도 카메라의 값이라 그림자와 본 패스가 같은 계수다.
         * @param density 각 1 rad 당 분할 수(머티리얼 `_tessellationDensity`).
         * @param maxFactor 머티리얼의 상한(`_tessellationMaxFactor`). `kMaxTessFactor` 를 넘으면 자른다.
         */
        [[nodiscard]] static float32 computeEdgeTessFactor( const float3& position0, const float3& position1, float32 viewDepth0, float32 viewDepth1, float32 density,
                                                            float32 maxFactor ) noexcept
        {
            const float32 edgeLength = float3::getDistance( position0, position1 );
            const float32 viewDepth  = MathUtil::max( ( viewDepth0 + viewDepth1 ) * 0.5f, kMinViewDepth );
            const float32 upper      = MathUtil::clamp( maxFactor, 1.0f, kMaxTessFactor );
            return MathUtil::clamp( edgeLength / viewDepth * density, 1.0f, upper );
        }

        /** @brief 안쪽 계수입니다 — 세 변 계수 중 가장 큰 값(가장 잘게 나눈 변을 안쪽도 따른다). */
        [[nodiscard]] static float32 computeInsideTessFactor( float32 edge0, float32 edge1, float32 edge2 ) noexcept
        {
            return MathUtil::max( MathUtil::max( edge0, edge1 ), edge2 );
        }

        /** @brief 노멀 방향 변위 길이(m)입니다 — 높이가 기준 높이(`_displacementMidLevel`)이면 0, 위는 바깥, 아래는 안쪽입니다(언리얼 Displacement Center 와 같다). */
        [[nodiscard]] static float32 computeOffset( float32 height, float32 scale, float32 midLevel ) noexcept
        {
            return ( height - midLevel ) * scale;
        }
    };
} // namespace sw
