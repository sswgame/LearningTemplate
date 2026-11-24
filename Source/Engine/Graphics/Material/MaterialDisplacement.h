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
        /** @brief 높이 밉 무게 합의 바닥입니다. 이보다 작으면 원래 꼭짓점으로 보고 그 꼭짓점의 밉으로 읽습니다. */
        static constexpr float32 kMinBarycentricWeight = 1.0e-8f;

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
         * @brief 변 하나의 높이 밉 단계입니다 — 그 변을 계수로 나눈 정점 간격(UV 변 / 변 계수)이 걸치는 텍셀 수의 log2 이고 0 이상입니다.
         * @details 도메인 스테이지에는 화면 미분이 없어 정점 간격으로 밉을 고른다 — 정점이 담을 수 없는 높이 주파수를 걸러 먼 표면의 읽기가 흩어지지 않고
         *          거리에 따라 계수가 바뀔 때 윤곽이 끓지 않는다(언리얼 테셀레이션 변위 · HDRP 가 거리로 밉을 고르는 것과 같은 생각).
         *          변의 두 끝점 UV 와 그 변의 계수만 읽어 끝점 순서를 바꿔도 같은 값이다 — 변을 나누는 두 패치가 그 변 위에서 같은 밉 · 같은 높이를 읽어 틈이 없다
         *          (패치의 세 변 · 안쪽 계수로 고르면 이웃 패치가 다른 밉으로 같은 변을 읽어 갈라진다).
         * @param textureSize 높이 맵 크기(텍셀).
         */
        [[nodiscard]] static float32 computeEdgeHeightMipLevel( const float2& uvA, const float2& uvB, float32 edgeFactor, const float2& textureSize ) noexcept
        {
            const float2  edge( ( uvB._x - uvA._x ) * textureSize._x, ( uvB._y - uvA._y ) * textureSize._y );
            const float32 texelSpan = edge.getLength() / MathUtil::max( edgeFactor, 1.0f );
            return MathUtil::log2( MathUtil::max( texelSpan, 1.0f ) );
        }

        /**
         * @brief 원래 꼭짓점의 높이 밉입니다 — 그 꼭짓점의 뷰 깊이에서 메시의 평균 변을 나눈 정점 간격이 걸치는 텍셀 수의 log2 이고 0 이상입니다(변의 밉과 같은 식).
         * @details 꼭짓점은 여러 패치가 나누는데 패치마다 변이 다르다 — 꼭짓점의 뷰 깊이와 배치 하나에 같은 값(`Mesh::getMeanEdgeLength` · `getUVDensity` 를 배치 표로)만 읽어
         *          어느 패치에서나 같은 밉이라 틈이 없다. 정점에 칸을 더하지 않는다. 변이 고르지 않은 메시는 평균만큼 어긋난다(같은 어긋남이라 갈라지지 않는다).
         * @param meanWorldEdge 평균 변의 월드 길이(인스턴스 배율 포함). @param meanUVEdge 평균 변의 UV 길이(UV 가 없으면 0 — 밉 0).
         */
        [[nodiscard]] static float32 computeVertexHeightMipLevel( float32 viewDepth, float32 meanWorldEdge, float32 meanUVEdge, float32 density, float32 maxFactor, const float2& textureSize ) noexcept
        {
            const float32 upper     = MathUtil::clamp( maxFactor, 1.0f, kMaxTessFactor );
            const float32 factor    = MathUtil::clamp( meanWorldEdge / MathUtil::max( viewDepth, kMinViewDepth ) * density, 1.0f, upper );
            const float32 texelSpan = meanUVEdge * MathUtil::max( textureSize._x, textureSize._y ) / factor;
            return MathUtil::log2( MathUtil::max( texelSpan, 1.0f ) );
        }

        /**
         * @brief 패치 안 한 점의 높이 밉입니다 — 세 변의 밉을 무게 b1·b2 · b2·b0 · b0·b1 로 섞는다(변 i 는 꼭짓점 i 의 맞은편).
         * @details 변 i 위(b_i = 0)에서는 그 변의 밉 하나만 남아 이웃 패치와 같은 값이다. 원래 꼭짓점(무게가 모두 0)은 그 꼭짓점의 밉 @p vertexMip 이다(`computeVertexHeightMipLevel`).
         * @param barycentric 무게 중심 좌표(SV_DomainLocation).
         */
        [[nodiscard]] static float32 blendEdgeHeightMipLevels( const float3& barycentric, float32 edgeMip0, float32 edgeMip1, float32 edgeMip2, const float3& vertexMip ) noexcept
        {
            const float32 weightSum = barycentric._y * barycentric._z + barycentric._z * barycentric._x + barycentric._x * barycentric._y;
            return weightSum > kMinBarycentricWeight ? blendEdges( barycentric, edgeMip0, edgeMip1, edgeMip2 ) : barycentric.dot( vertexMip );
        }

        /** @brief 세 변의 값을 무게 b1·b2 · b2·b0 · b0·b1 로 섞습니다 — 변 위에서는 그 변의 값 하나, 원래 꼭짓점은 0 입니다. */
        [[nodiscard]] static float32 blendEdges( const float3& barycentric, float32 edge0, float32 edge1, float32 edge2 ) noexcept
        {
            const float32 weight0 = barycentric._y * barycentric._z;
            const float32 weight1 = barycentric._z * barycentric._x;
            const float32 weight2 = barycentric._x * barycentric._y;
            const float32 sum     = weight0 + weight1 + weight2;
            return sum > kMinBarycentricWeight ? ( weight0 * edge0 + weight1 * edge1 + weight2 * edge2 ) / sum : 0.0f;
        }

        /**
         * @brief 이음매 누름입니다 — 이음매 꼭짓점(`RHIVertex::isOnSeam`)과 이음매 변(`RHIVertex::isSeamEdge`)에서 0, 테셀레이션 한 칸 안쪽부터 1 입니다. 변위 길이에 곱합니다.
         * @details 이음매 양쪽의 패치는 UV 가 달라 다른 높이를 읽는다 — 변위를 0 으로 눌러 같은 자리에 둔다. 변 위의 값은 그 변의 두 끝점 표시와 변 계수만 읽어, 변을 나누는 두 패치가 같다.
         * @param seam 꼭짓점마다 이음매면 1, 아니면 0 입니다.
         * @param edgeSeam 변마다(변 i 는 꼭짓점 i 의 맞은편) 이음매면 1, 아니면 0 입니다.
         */
        [[nodiscard]] static float32 computeSeamFade( const float3& barycentric, const float3& seam, const float3& edgeSeam, float32 edgeFactor0, float32 edgeFactor1, float32 edgeFactor2 ) noexcept
        {
            const float32 factor = blendEdges( barycentric, edgeFactor0, edgeFactor1, edgeFactor2 );
            float32       fade   = 1.0f;
            const auto    press  = [&fade, factor]( float32 distance, float32 seamWeight )
            { fade = MathUtil::min( fade, 1.0f + ( MathUtil::clamp( distance * factor, 0.0f, 1.0f ) - 1.0f ) * seamWeight ); };
            press( 1.0f - barycentric._x, seam._x );
            press( 1.0f - barycentric._y, seam._y );
            press( 1.0f - barycentric._z, seam._z );
            press( barycentric._x, edgeSeam._x );
            press( barycentric._y, edgeSeam._y );
            press( barycentric._z, edgeSeam._z );
            return fade;
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
            const float32  det     = uvEdge1._x * uvEdge2._y - uvEdge2._x * uvEdge1._y;
            UVTangentFrame frame{ float3::Zero, float3::Zero };
            if ( MathUtil::abs( det ) < kMinUVArea )
                return frame;
            frame._tangent   = ( edge1 * uvEdge2._y - edge2 * uvEdge1._y ) / det;
            frame._bitangent = ( edge2 * uvEdge1._x - edge1 * uvEdge2._x ) / det;
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
