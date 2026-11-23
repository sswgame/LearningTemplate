// MinimapTest — 표시가 놓일 자리(보는 쪽이 위, 범위 밖은 가장자리)를 본다(nogpu, 화면 없음).
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Client/HUD/MinimapComponent.h"

using namespace sw;

/** @brief [MinimapTest] 요 0 이면 +Z 가 위(-Y), +X 가 오른쪽이고, 요 90° 면 +X 가 위다 */
SW_TEST_CASE( MinimapTest, ForwardIsUp )
{
    const float2           half{ 100.0f, 100.0f };
    const float3           center{ 5.0f, 0.0f, 5.0f };
    const MinimapPlacement ahead = MinimapComponent::computePlacement( center, 0.0f, 20.0f, half, float3{ 5.0f, 3.0f, 15.0f } );
    SW_EXPECT_TRUE( ahead._bInRange == SW_TRUE );
    SW_EXPECT_NEAR( 0.0f, ahead._offset._x, 1.0e-3f );
    SW_EXPECT_NEAR( -50.0f, ahead._offset._y, 1.0e-3f );

    const MinimapPlacement right = MinimapComponent::computePlacement( center, 0.0f, 20.0f, half, float3{ 15.0f, 0.0f, 5.0f } );
    SW_EXPECT_NEAR( 50.0f, right._offset._x, 1.0e-3f );
    SW_EXPECT_NEAR( 0.0f, right._offset._y, 1.0e-3f );

    const MinimapPlacement turned = MinimapComponent::computePlacement( center, MathUtil::kHalfPi, 20.0f, half, float3{ 15.0f, 0.0f, 5.0f } );
    SW_EXPECT_NEAR( 0.0f, turned._offset._x, 1.0e-3f );
    SW_EXPECT_NEAR( -50.0f, turned._offset._y, 1.0e-3f );
}

/** @brief [MinimapTest] 범위 밖은 같은 방향의 가장자리 점이고 범위 밖으로 표시된다 */
SW_TEST_CASE( MinimapTest, OutOfRangeSticksToTheEdge )
{
    const MinimapPlacement placement = MinimapComponent::computePlacement( float3{}, 0.0f, 10.0f, float2{ 60.0f, 60.0f }, float3{ 30.0f, 0.0f, 40.0f } );
    SW_EXPECT_TRUE( placement._bInRange == SW_FALSE );
    SW_EXPECT_NEAR( 36.0f, placement._offset._x, 1.0e-3f );
    SW_EXPECT_NEAR( -48.0f, placement._offset._y, 1.0e-3f );
}
