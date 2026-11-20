#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/Ray.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/View/SceneViewQuery.h"

// SceneViewQueryTest — 카메라 질의: 절두체 안(점 · 구 · 상자, 기하만), 화면 크기(LOD 식과 같다), 월드 → 화면(왼쪽 위 원점, 카메라 뒤는 실패),
// 화면 → 광선(투영한 점을 다시 지난다, 직교는 앞 방향과 나란하다)(nogpu).

using namespace sw;

namespace
{
    constexpr float32 kWidth  = 800.0f;
    constexpr float32 kHeight = 400.0f;

    /** @brief (0, 0, -10) 에서 +Z 를 보는 시야각 90° · 2:1 원근 질의입니다. */
    SceneViewQuery makePerspectiveQuery()
    {
        const float3   eye  = float3{ 0.0f, 0.0f, -10.0f };
        const float4x4 view = float4x4::makeLookAt( eye, float3{ 0.0f, 0.0f, 0.0f }, float3{ 0.0f, 1.0f, 0.0f } );
        return SceneViewQuery::make( view * float4x4::makePerspectiveFieldOfView( MathUtil::kPi * 0.5f, kWidth / kHeight, 0.1f, 100.0f ), eye );
    }
} // namespace

/** @brief [SceneViewQueryTest] 절두체 판정은 렌더러와 같은 평면이다 — 앞의 점 · 걸친 구 · 상자는 안, 뒤 · 옆은 밖. 화면 크기는 LODView 식 그대로다 */
SW_TEST_CASE( SceneViewQueryTest, FrustumTestsAndScreenSizeMatchLODView )
{
    const SceneViewQuery query = makePerspectiveQuery();
    SW_EXPECT_TRUE( query.isPointInFrustum( float3{ 0.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_FALSE( query.isPointInFrustum( float3{ 0.0f, 0.0f, -20.0f } ) );
    SW_EXPECT_FALSE( query.isPointInFrustum( float3{ 0.0f, 15.0f, 0.0f } ) ); // 세로 반 높이는 거리 10 에서 10
    SW_EXPECT_TRUE( query.isSphereInFrustum( float3{ 0.0f, 10.5f, 0.0f }, 1.0f ) );
    SW_EXPECT_TRUE( query.isBoxInFrustum( AABB{
        float3{-1.0f,  9.5f, -1.0f},
        float3{ 1.0f, 11.0f,  1.0f}
    } ) );
    SW_EXPECT_FALSE( query.isBoxInFrustum( AABB{
        float3{-1.0f, -1.0f, -30.0f},
        float3{ 1.0f,  1.0f, -25.0f}
    } ) );
    SW_EXPECT_NEAR( 0.1f, query.computeScreenSize( float3{ 0.0f, 0.0f, 0.0f }, 1.0f ), 1.0e-4f );
    SW_EXPECT_EQUAL( LODView::computeScreenSize( query._view, float3{ 3.0f, 1.0f, 5.0f }, 2.0f ), query.computeScreenSize( float3{ 3.0f, 1.0f, 5.0f }, 2.0f ) );
    SW_EXPECT_EQUAL( 0.0f, query.computeScreenSize( float3{ 0.0f, 0.0f, -30.0f }, 1.0f ) );
}

/** @brief [SceneViewQueryTest] 월드 → 화면: 시선 위의 점은 화면 가운데, 위쪽 가장자리 점은 y = 0(왼쪽 위 원점), 카메라 뒤는 실패한다 */
SW_TEST_CASE( SceneViewQueryTest, WorldToScreenUsesTopLeftOrigin )
{
    const SceneViewQuery query = makePerspectiveQuery();
    const float2         screenSize{ kWidth, kHeight };
    float2               screen{};
    SW_REQUIRE_TRUE( query.worldToScreen( float3{ 0.0f, 0.0f, 5.0f }, screenSize, screen ) );
    SW_EXPECT_NEAR( kWidth * 0.5f, screen._x, 1.0e-2f );
    SW_EXPECT_NEAR( kHeight * 0.5f, screen._y, 1.0e-2f );
    SW_REQUIRE_TRUE( query.worldToScreen( float3{ 0.0f, 10.0f, 0.0f }, screenSize, screen ) ); // 거리 10, 시야각 90° 의 위쪽 끝
    SW_EXPECT_NEAR( 0.0f, screen._y, 1.0e-2f );
    SW_EXPECT_FALSE( query.worldToScreen( float3{ 0.0f, 0.0f, -15.0f }, screenSize, screen ) );
}

/** @brief [SceneViewQueryTest] 화면 → 광선: 투영한 화면 점의 광선은 그 월드 점을 지난다. 화면 크기가 비면 실패한다 */
SW_TEST_CASE( SceneViewQueryTest, ScreenRayPassesProjectedPoint )
{
    const SceneViewQuery query = makePerspectiveQuery();
    const float2         screenSize{ kWidth, kHeight };
    const float3         target{ 3.0f, -2.0f, 7.0f };
    float2               screen{};
    SW_REQUIRE_TRUE( query.worldToScreen( target, screenSize, screen ) );
    Ray ray{};
    SW_REQUIRE_TRUE( query.screenToWorldRay( screen, screenSize, ray ) );
    const float32 t = ( target._z - ray._origin._z ) / ray._direction._z;
    SW_EXPECT_NEAR( target._x, ray._origin._x + ray._direction._x * t, 1.0e-3f );
    SW_EXPECT_NEAR( target._y, ray._origin._y + ray._direction._y * t, 1.0e-3f );
    SW_EXPECT_FALSE( query.screenToWorldRay( screen, float2{}, ray ) );
}

/** @brief [SceneViewQueryTest] 직교: 화면 어디서 쏘아도 광선이 앞(+Z)과 나란하고, 투영은 거리와 무관하다 */
SW_TEST_CASE( SceneViewQueryTest, OrthographicRaysAreParallel )
{
    const SceneViewQuery query = SceneViewQuery::make( float4x4::makeOrthographic( 20.0f, 10.0f, 1.0f, 100.0f ), float3{ 0.0f, 0.0f, 0.0f } );
    const float2         screenSize{ kWidth, kHeight };
    Ray                  ray{};
    SW_REQUIRE_TRUE( query.screenToWorldRay( float2{ 10.0f, 390.0f }, screenSize, ray ) );
    SW_EXPECT_NEAR( 1.0f, ray._direction._z, 1.0e-5f );
    float2 nearScreen{};
    float2 farScreen{};
    SW_REQUIRE_TRUE( query.worldToScreen( float3{ 5.0f, 2.5f, 2.0f }, screenSize, nearScreen ) );
    SW_REQUIRE_TRUE( query.worldToScreen( float3{ 5.0f, 2.5f, 90.0f }, screenSize, farScreen ) );
    SW_EXPECT_NEAR( nearScreen._x, farScreen._x, 1.0e-3f );
    SW_EXPECT_NEAR( kWidth * 0.75f, nearScreen._x, 1.0e-2f );
    SW_EXPECT_NEAR( kHeight * 0.25f, nearScreen._y, 1.0e-2f );
}
