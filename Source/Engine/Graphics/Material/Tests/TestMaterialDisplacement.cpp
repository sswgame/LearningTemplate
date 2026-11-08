// MaterialDisplacementTest — 변위 테셀레이션 계수가 틈 없이(변의 두 끝점만으로) 정해지고 거리 · 상한을 따르며, 변위가 기준 높이에서 0 인지(nogpu).
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
