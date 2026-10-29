#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Graphics/LOD/LODView.h"

#include "Core/Testing/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief (0, 0, -10) 에서 원점을 보는 시야각 90° · 정사각 화면의 뷰 — 투영 y 배율 cot( 45° ) = 1 이라 화면 크기 = 반지름 / 거리. */
    LODView makeTestView()
    {
        const float3   eye  = float3{ 0.0f, 0.0f, -10.0f };
        const float4x4 view = float4x4::makeLookAt( eye, float3{ 0.0f, 0.0f, 0.0f }, float3{ 0.0f, 1.0f, 0.0f } );
        const float4x4 proj = float4x4::makePerspectiveFieldOfView( MathUtil::kPi * 0.5f, 1.0f, 0.1f, 100.0f );
        return LODView::make( view * proj, eye );
    }
} // namespace

SW_TEST_CASE( LODViewTest, ScreenSizeIsRadiusOverDistanceForNinetyDegrees )
{
    const LODView view = makeTestView();
    SW_EXPECT_NEAR( 0.1f, LODViewUtil::computeScreenSize( view, float3{ 0.0f, 0.0f, 0.0f }, 1.0f ), 1.0e-4f );
    SW_EXPECT_NEAR( 0.2f, LODViewUtil::computeScreenSize( view, float3{ 0.0f, 0.0f, 0.0f }, 2.0f ), 1.0e-4f );
}

SW_TEST_CASE( LODViewTest, OutsideFrustumIsZeroAndEyeInsideIsOne )
{
    const LODView view = makeTestView();
    SW_EXPECT_EQUAL( 0.0f, LODViewUtil::computeScreenSize( view, float3{ 0.0f, 0.0f, -30.0f }, 1.0f ) ); // 눈 뒤
    SW_EXPECT_EQUAL( 1.0f, LODViewUtil::computeScreenSize( view, float3{ 0.0f, 0.0f, -10.0f }, 1.0f ) ); // 눈이 구 안
}
