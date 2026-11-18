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
        /** @brief UV 넓이 · 행렬식의 바닥입니다. 이보다 작으면 퇴화로 보고 노멀을 다시 구하지 않습니다. */
        static constexpr float32 kMinUVArea = 1.0e-12f;

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

        /**
         * @brief 높이 맵을 읽을 밉 단계입니다 — 테셀레이션이 만든 정점 간격(패치의 UV 변 / 안쪽 계수)이 걸치는 텍셀 수의 log2 이고 0 이상입니다.
         * @details 도메인 스테이지에는 화면 미분이 없어 정점 간격으로 밉을 고른다 — 정점이 담을 수 없는 높이 주파수를 걸러 먼 표면의 읽기가 흩어지지 않고
         *          거리에 따라 계수가 바뀔 때 윤곽이 끓지 않는다(언리얼 테셀레이션 변위 · HDRP 가 거리로 밉을 고르는 것과 같은 생각).
         * @param textureSize 높이 맵 크기(텍셀).
         */
        [[nodiscard]] static float32 computeHeightMipLevel( const float2& uv0, const float2& uv1, const float2& uv2, float32 insideFactor, const float2& textureSize ) noexcept
        {
            const float2  edge0( ( uv1.x - uv0.x ) * textureSize.x, ( uv1.y - uv0.y ) * textureSize.y );
            const float2  edge1( ( uv2.x - uv1.x ) * textureSize.x, ( uv2.y - uv1.y ) * textureSize.y );
            const float2  edge2( ( uv0.x - uv2.x ) * textureSize.x, ( uv0.y - uv2.y ) * textureSize.y );
            const float32 texelSpan = MathUtil::max( MathUtil::max( edge0.getLength(), edge1.getLength() ), edge2.getLength() ) / MathUtil::max( insideFactor, 1.0f );
            return MathUtil::log2( MathUtil::max( texelSpan, 1.0f ) );
        }

        /** @brief 패치의 UV 에 대한 위치 미분 — 탄젠트 dP/du · 바이탄젠트 dP/dv(정규화하지 않는다). UV 가 퇴화한 패치(넓이 0)는 둘 다 0 입니다. */
        struct UVTangentFrame
        {
            float3 _tangent;
            float3 _bitangent;
        };

        /** @brief 세 꼭짓점의 위치 · UV 에서 dP/du · dP/dv 를 풉니다(고전 탄젠트 식과 같은 2x2 풀이). */
        [[nodiscard]] static UVTangentFrame computeUVTangentFrame( const float3& position0, const float3& position1, const float3& position2, const float2& uv0,
                                                                   const float2& uv1, const float2& uv2 ) noexcept
        {
            const float3   edge1   = position1 - position0;
            const float3   edge2   = position2 - position0;
            const float2   uvEdge1 = uv1 - uv0;
            const float2   uvEdge2 = uv2 - uv0;
            const float32  det     = uvEdge1.x * uvEdge2.y - uvEdge2.x * uvEdge1.y;
            UVTangentFrame frame{ float3::Zero, float3::Zero };
            if ( MathUtil::abs( det ) < kMinUVArea )
                return frame;
            frame._tangent   = ( edge1 * uvEdge2.y - edge2 * uvEdge1.y ) / det;
            frame._bitangent = ( edge2 * uvEdge1.x - edge1 * uvEdge2.x ) / det;
            return frame;
        }

        /**
         * @brief 변위가 만든 기울기로 다시 구한 노멀입니다 — N' = normalize( N - scale × 표면 기울기(h) ).
         * @details 표면 기울기 틀(Mikkelsen 2020 "Surface Gradient Based Bump Mapping", HDRP 가 쓰는 식)이다. ∇u = (dP/dv × N) / det, ∇v = (N × dP/du) / det,
         *          det = N · (dP/du × dP/dv) 라 UV 손잡이(거울 UV)에 상관없이 오르막 반대로 기운다. 퇴화(det ≈ 0)하면 N 그대로다.
         * @param heightSlopeU 높이 맵(0 ~ 1)의 u 미분. @param heightSlopeV v 미분. @param scale 변위 세기(m, `_displacementScale`).
         */
        [[nodiscard]] static float3 computeDisplacementNormal( const float3& normal, const UVTangentFrame& frame, float32 heightSlopeU, float32 heightSlopeV, float32 scale ) noexcept
        {
            const float32 det = normal.dot( frame._tangent.cross( frame._bitangent ) );
            if ( MathUtil::abs( det ) < kMinUVArea )
                return normal;
            const float3 surfaceGradient = ( frame._bitangent.cross( normal ) * heightSlopeU + normal.cross( frame._tangent ) * heightSlopeV ) * ( scale / det );
            return ( normal - surfaceGradient ).normalize();
        }

        /** @brief 노멀 방향 변위 길이(m)입니다 — 높이가 기준 높이(`_displacementMidLevel`)이면 0, 위는 바깥, 아래는 안쪽입니다(언리얼 Displacement Center 와 같다). */
        [[nodiscard]] static float32 computeOffset( float32 height, float32 scale, float32 midLevel ) noexcept
        {
            return ( height - midLevel ) * scale;
        }
    };
} // namespace sw
