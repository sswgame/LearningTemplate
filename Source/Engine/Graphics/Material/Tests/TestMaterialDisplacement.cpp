// MaterialDisplacementTest — 변위 테셀레이션 계수가 틈 없이(변의 두 끝점만으로) 정해지고 거리 · 상한을 따르며, 변위가 기준 높이에서 0 이고,
// 높이 밉이 변마다 정점 간격을 따라 이웃 패치와 같은 값이며 다시 구한 노멀이 오르막 반대로 기우는지(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Material/MaterialDisplacement.h"

using namespace sw;

/** @brief [MaterialDisplacementTest] 끝점 순서를 바꿔도 같은 계수다 — 변을 나누는 두 패치가 같은 값을 내어 틈이 없다 */
SW_TEST_CASE( MaterialDisplacementTest, EdgeFactorIsSymmetric )
{
    const float3  a( 0.3f, 1.7f, -2.1f );
    const float3  b( 1.9f, 0.2f, 4.4f );
    const float32 forward  = MaterialDisplacement::computeEdgeTessFactor( a, b, 3.1f, 7.9f, 120.0f, 64.0f );
    const float32 backward = MaterialDisplacement::computeEdgeTessFactor( b, a, 7.9f, 3.1f, 120.0f, 64.0f );
    SW_EXPECT_EQUAL( forward, backward );
}

/** @brief [MaterialDisplacementTest] 가까운 변일수록 잘게 나눈다 — 같은 변을 두 배 거리에서 보면 계수가 절반이다(자르지 않는 구간) */
SW_TEST_CASE( MaterialDisplacementTest, EdgeFactorFallsWithDistance )
{
    const float3  a( 0.0f, 0.0f, 0.0f );
    const float3  b( 1.0f, 0.0f, 0.0f );
    const float32 near = MaterialDisplacement::computeEdgeTessFactor( a, b, 5.0f, 5.0f, 100.0f, 64.0f );
    const float32 far  = MaterialDisplacement::computeEdgeTessFactor( a, b, 10.0f, 10.0f, 100.0f, 64.0f );
    SW_EXPECT_NEAR( 20.0f, near, 1e-4f );
    SW_EXPECT_NEAR( 10.0f, far, 1e-4f );
}

/** @brief [MaterialDisplacementTest] 계수는 [1, 상한] 이다 — 머티리얼 상한이 하드웨어 상한(64)보다 커도 64 이고, 아주 먼 변도 1 아래로 내려가지 않는다 */
SW_TEST_CASE( MaterialDisplacementTest, EdgeFactorIsClamped )
{
    const float3 a( 0.0f, 0.0f, 0.0f );
    const float3 b( 10.0f, 0.0f, 0.0f );
    SW_EXPECT_EQUAL( 8.0f, MaterialDisplacement::computeEdgeTessFactor( a, b, 1.0f, 1.0f, 100.0f, 8.0f ) );
    SW_EXPECT_EQUAL( MaterialDisplacement::kMaxTessFactor, MaterialDisplacement::computeEdgeTessFactor( a, b, 1.0f, 1.0f, 100.0f, 1000.0f ) );
    SW_EXPECT_EQUAL( 1.0f, MaterialDisplacement::computeEdgeTessFactor( a, b, 1.0e6f, 1.0e6f, 100.0f, 64.0f ) );
    // 카메라에 붙은(깊이 0 · 음수) 변도 나눗셈 바닥 덕분에 유한한 상한 값이다.
    SW_EXPECT_EQUAL( 64.0f, MaterialDisplacement::computeEdgeTessFactor( a, b, 0.0f, -1.0f, 100.0f, 64.0f ) );
}

/** @brief [MaterialDisplacementTest] 안쪽 계수는 가장 잘게 나눈 변을 따른다 */
SW_TEST_CASE( MaterialDisplacementTest, InsideFactorIsEdgeMax )
{
    SW_EXPECT_EQUAL( 9.0f, MaterialDisplacement::computeInsideTessFactor( 3.0f, 9.0f, 5.0f ) );
}

/** @brief [MaterialDisplacementTest] 기준 높이에서 변위가 0, 위는 바깥(+), 아래는 안쪽(-)이다 */
SW_TEST_CASE( MaterialDisplacementTest, OffsetIsZeroAtMidLevel )
{
    SW_EXPECT_EQUAL( 0.0f, MaterialDisplacement::computeOffset( 0.5f, 0.3f, 0.5f ) );
    SW_EXPECT_NEAR( 0.15f, MaterialDisplacement::computeOffset( 1.0f, 0.3f, 0.5f ), 1e-6f );
    SW_EXPECT_NEAR( -0.15f, MaterialDisplacement::computeOffset( 0.0f, 0.3f, 0.5f ), 1e-6f );
}

/** @brief [MaterialDisplacementTest] 변의 높이 밉은 정점 간격을 따른다 — 변 계수를 두 배로 하면 한 단계 내려가고, 정점 간격이 한 텍셀보다 작으면 0, 끝점 순서를 바꿔도 같다 */
SW_TEST_CASE( MaterialDisplacementTest, EdgeHeightMipFollowsVertexSpacing )
{
    const float2  uv0( 0.0f, 0.0f );
    const float2  uv2( 1.0f, 1.0f ); // 대각선 변
    const float2  size( 256.0f, 256.0f );
    const float32 coarse = MaterialDisplacement::computeEdgeHeightMipLevel( uv2, uv0, 4.0f, size );
    const float32 fine   = MaterialDisplacement::computeEdgeHeightMipLevel( uv2, uv0, 8.0f, size );
    SW_EXPECT_NEAR( MathUtil::log2( 256.0f * 1.41421356f / 4.0f ), coarse, 1e-4f );
    SW_EXPECT_NEAR( coarse - 1.0f, fine, 1e-4f );
    SW_EXPECT_EQUAL( coarse, MaterialDisplacement::computeEdgeHeightMipLevel( uv0, uv2, 4.0f, size ) );
    SW_EXPECT_EQUAL( 0.0f, MaterialDisplacement::computeEdgeHeightMipLevel( uv2, uv0, 64.0f, float2( 16.0f, 16.0f ) ) );
}

/**
 * @brief [MaterialDisplacementTest] 패치 안의 밉 섞기 — 변 위에서는 그 변의 밉 하나라 나머지 두 변(이웃 패치마다 다르다)과 무관하고, 원래 꼭짓점은 어느 패치에서나 0, 가운데는 셋의 평균이다
 * @details 패치의 세 변 · 안쪽 계수로 밉을 고르면 같은 변을 나누는 두 패치가 다른 밉으로 읽어 변위가 갈라진다 — 그 틈을 막는 성질이다.
 */
SW_TEST_CASE( MaterialDisplacementTest, BlendedHeightMipMatchesNeighborOnSharedEdge )
{
    const float3 onEdge0( 0.0f, 0.3f, 0.7f );
    SW_EXPECT_NEAR( 2.5f, MaterialDisplacement::blendEdgeHeightMipLevels( onEdge0, 2.5f, 0.0f, 6.0f ), 1e-5f );
    SW_EXPECT_NEAR( 2.5f, MaterialDisplacement::blendEdgeHeightMipLevels( onEdge0, 2.5f, 4.0f, 1.0f ), 1e-5f );
    SW_EXPECT_EQUAL( 0.0f, MaterialDisplacement::blendEdgeHeightMipLevels( float3( 1.0f, 0.0f, 0.0f ), 2.5f, 4.0f, 1.0f ) );
    const float32 third = 1.0f / 3.0f;
    SW_EXPECT_NEAR( 3.0f, MaterialDisplacement::blendEdgeHeightMipLevels( float3( third, third, third ), 1.0f, 3.0f, 5.0f ), 1e-5f );
}

/** @brief [MaterialDisplacementTest] 위치 미분 — UV 가 월드 xz 를 2 배로 늘인 판이면 dP/du = (0.5, 0, 0), dP/dv = (0, 0, 0.5), UV 가 퇴화하면 0 이다 */
SW_TEST_CASE( MaterialDisplacementTest, UVTangentFrameSolveUVMapping )
{
    const MaterialDisplacement::UVTangentFrame frame = MaterialDisplacement::computeUVTangentFrame(
        float3( 0.0f, 0.0f, 0.0f ), float3( 1.0f, 0.0f, 0.0f ), float3( 0.0f, 0.0f, 1.0f ), float2( 0.0f, 0.0f ), float2( 2.0f, 0.0f ), float2( 0.0f, 2.0f ) );
    SW_EXPECT_NEAR( 0.5f, frame._tangent._x, 1e-6f );
    SW_EXPECT_NEAR( 0.0f, frame._tangent._z, 1e-6f );
    SW_EXPECT_NEAR( 0.5f, frame._bitangent._z, 1e-6f );
    SW_EXPECT_NEAR( 0.0f, frame._bitangent._x, 1e-6f );
    const MaterialDisplacement::UVTangentFrame degenerate = MaterialDisplacement::computeUVTangentFrame(
        float3( 0.0f, 0.0f, 0.0f ), float3( 1.0f, 0.0f, 0.0f ), float3( 0.0f, 0.0f, 1.0f ), float2( 0.0f, 0.0f ), float2( 1.0f, 1.0f ), float2( 2.0f, 2.0f ) );
    SW_EXPECT_EQUAL( 0.0f, degenerate._tangent.getLength() );
    SW_EXPECT_EQUAL( 0.0f, degenerate._bitangent.getLength() );
    // 퇴화한 틀은 노멀을 그대로 둔다.
    SW_EXPECT_NEAR( 1.0f, MaterialDisplacement::computeDisplacementNormal( float3( 0.0f, 1.0f, 0.0f ), degenerate, 1.0f, 1.0f, 0.5f )._y, 1e-6f );
}

/** @brief [MaterialDisplacementTest] 다시 구한 노멀 — 평평한 높이는 그대로, +u 오르막은 -u 쪽으로 기울고, UV 손잡이를 뒤집어도(거울 UV) 같은 쪽이다 */
SW_TEST_CASE( MaterialDisplacementTest, DisplacementNormalTiltsAwayFromSlope )
{
    const float3                               up( 0.0f, 1.0f, 0.0f );
    const MaterialDisplacement::UVTangentFrame frame{ float3( 1.0f, 0.0f, 0.0f ), float3( 0.0f, 0.0f, 1.0f ) };
    const float3                               flat = MaterialDisplacement::computeDisplacementNormal( up, frame, 0.0f, 0.0f, 0.5f );
    SW_EXPECT_NEAR( 1.0f, flat._y, 1e-6f );

    // 높이 = u(= 월드 x) 인 경사를 0.5 m 세기로 — 기울기 0.5 라 노멀은 (-0.5, 1, 0) 방향이다.
    const float3 sloped   = MaterialDisplacement::computeDisplacementNormal( up, frame, 1.0f, 0.0f, 0.5f );
    const float3 expected = float3( -0.5f, 1.0f, 0.0f ).normalize();
    SW_EXPECT_NEAR( expected._x, sloped._x, 1e-5f );
    SW_EXPECT_NEAR( expected._y, sloped._y, 1e-5f );
    SW_EXPECT_NEAR( 0.0f, sloped._z, 1e-6f );

    const MaterialDisplacement::UVTangentFrame mirroredFrame{ float3( 1.0f, 0.0f, 0.0f ), float3( 0.0f, 0.0f, -1.0f ) };
    const float3                               mirroredFrameSloped = MaterialDisplacement::computeDisplacementNormal( up, mirroredFrame, 1.0f, 0.0f, 0.5f );
    SW_EXPECT_NEAR( expected._x, mirroredFrameSloped._x, 1e-5f );
}
