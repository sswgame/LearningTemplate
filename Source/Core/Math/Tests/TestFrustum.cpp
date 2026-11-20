#include "pch.h"

#include "Core/Math/AABB.h"
#include "Core/Math/Frustum.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Testing/TestFramework.h"

// FrustumTest — 절두체 평면 추출과 겹침 판정의 경계 사례: 걸친 상자 · 구는 남고(가짜 컬링 없음) 바깥 · 뒤 · 먼 면 너머는 빠진다.
// 원근 · 직교 · 매우 넓은 시야각 · 퇴화 행렬(아무것도 거르지 않는다) · reversed-Z(평면 집합이 같다) · 회전한 상자를 다시 감싼 월드 상자(nogpu).

using namespace sw;

namespace
{
    /** @brief 원점에서 +Z 를 보는 시야각 90° · 정사각 원근(깊이 z 에서 반 너비 = z), 가까운 면 1 · 먼 면 100 입니다. */
    float4x4 makePerspective90()
    {
        const float4x4 view = float4x4::makeLookAt( float3{ 0.0f, 0.0f, 0.0f }, float3{ 0.0f, 0.0f, 1.0f }, float3{ 0.0f, 1.0f, 0.0f } );
        return view * float4x4::makePerspectiveFieldOfView( MathUtil::kPi * 0.5f, 1.0f, 1.0f, 100.0f );
    }

    /** @brief 클립 z 를 w - z 로 뒤집는 행렬(reversed-Z — 가까운 면이 깊이 1, 먼 면이 0)입니다. 행 벡터 규약이라 오른쪽에 곱합니다. */
    float4x4 makeReverseZ()
    {
        return float4x4{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f };
    }

    AABB makeBox( float32 minX, float32 minY, float32 minZ, float32 maxX, float32 maxY, float32 maxZ )
    {
        return AABB{
            float3{minX, minY, minZ},
            float3{maxX, maxY, maxZ}
        };
    }

    bool overlaps( const Frustum& frustum, const AABB& box )
    {
        return frustum.overlapsBox( box._min, box._max );
    }

    /** @brief 원근 절두체 하나에 같은 상자 묶음을 묻고 기대와 맞는지 봅니다(원근 · reversed-Z 가 같은 답을 내야 한다). */
    void expectPerspectiveCases( const Frustum& frustum )
    {
        SW_EXPECT_TRUE( overlaps( frustum, makeBox( -0.5f, -0.5f, 9.5f, 0.5f, 0.5f, 10.5f ) ) );        // 정면
        SW_EXPECT_TRUE( overlaps( frustum, makeBox( -10.5f, -0.5f, 9.5f, -9.5f, 0.5f, 10.5f ) ) );      // 왼쪽 면에 걸침
        SW_EXPECT_FALSE( overlaps( frustum, makeBox( -12.0f, -0.5f, 9.5f, -11.0f, 0.5f, 10.5f ) ) );    // 왼쪽 밖
        SW_EXPECT_FALSE( overlaps( frustum, makeBox( -0.5f, -0.5f, -5.0f, 0.5f, 0.5f, -4.0f ) ) );      // 카메라 뒤
        SW_EXPECT_TRUE( overlaps( frustum, makeBox( -0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 1.5f ) ) );         // 가까운 면에 걸침
        SW_EXPECT_FALSE( overlaps( frustum, makeBox( -0.5f, -0.5f, 0.2f, 0.5f, 0.5f, 0.8f ) ) );        // 가까운 면 앞(눈과 사이)
        SW_EXPECT_TRUE( overlaps( frustum, makeBox( -0.5f, -0.5f, 99.5f, 0.5f, 0.5f, 100.5f ) ) );      // 먼 면에 걸침
        SW_EXPECT_FALSE( overlaps( frustum, makeBox( -0.5f, -0.5f, 101.0f, 0.5f, 0.5f, 102.0f ) ) );    // 먼 면 너머
        SW_EXPECT_TRUE( overlaps( frustum, makeBox( -200.0f, -200.0f, 5.0f, 200.0f, 200.0f, 6.0f ) ) ); // 절두체를 통째로 품는 상자
        // 구: 왼쪽 면까지의 거리 = ( x + z ) / √2 = -0.354 — 반지름 1 이면 걸치고 0.3 이면 밖이다.
        SW_EXPECT_TRUE( frustum.overlapsSphere( float3{ -10.5f, 0.0f, 10.0f }, 1.0f ) );
        SW_EXPECT_FALSE( frustum.overlapsSphere( float3{ -10.5f, 0.0f, 10.0f }, 0.3f ) );
        SW_EXPECT_TRUE( frustum.overlapsSphere( float3{ 0.0f, 0.0f, -0.5f }, 2.0f ) ); // 눈을 품는 구
    }
} // namespace

/** @brief [FrustumTest] 원근: 평면이 정규화돼 있고(법선 길이 1) 걸친 상자 · 구는 남고 바깥 · 뒤 · 가까운 면 앞 · 먼 면 너머는 빠진다 */
SW_TEST_CASE( FrustumTest, PerspectiveKeepsStraddlingAndCullsOutside )
{
    const Frustum frustum = Frustum::fromViewProjection( makePerspective90() );
    for ( const float4& plane : frustum._arrPlane )
        SW_EXPECT_NEAR( 1.0f, MathUtil::sqrt( plane._x * plane._x + plane._y * plane._y + plane._z * plane._z ), 1.0e-5f );
    expectPerspectiveCases( frustum );
}

/** @brief [FrustumTest] reversed-Z 투영(깊이 1 이 가까운 면)도 같은 여섯 평면(0 ≤ z ≤ w 의 두 면이 자리만 바뀐다)이라 같은 답을 낸다 */
SW_TEST_CASE( FrustumTest, ReversedZGivesTheSameAnswers )
{
    expectPerspectiveCases( Frustum::fromViewProjection( makePerspective90() * makeReverseZ() ) );
}

/** @brief [FrustumTest] 직교: x 는 ±10, 깊이는 1 ~ 100 — 가장자리 안쪽 · 걸침은 남고 옆 · 가까운 면 앞은 빠진다. 거리와 무관하게 폭이 같다 */
SW_TEST_CASE( FrustumTest, OrthographicBoundsAreParallel )
{
    const Frustum frustum = Frustum::fromViewProjection( float4x4::makeOrthographic( 20.0f, 20.0f, 1.0f, 100.0f ) );
    SW_EXPECT_TRUE( overlaps( frustum, makeBox( 9.5f, -0.5f, 50.0f, 10.5f, 0.5f, 51.0f ) ) );
    SW_EXPECT_TRUE( overlaps( frustum, makeBox( 9.5f, -0.5f, 98.0f, 10.5f, 0.5f, 99.0f ) ) ); // 멀어도 폭이 같다
    SW_EXPECT_FALSE( overlaps( frustum, makeBox( 10.1f, -0.5f, 50.0f, 11.0f, 0.5f, 51.0f ) ) );
    SW_EXPECT_FALSE( overlaps( frustum, makeBox( -0.5f, -0.5f, 0.0f, 0.5f, 0.5f, 0.5f ) ) );
    SW_EXPECT_TRUE( frustum.overlapsSphere( float3{ 9.999f, 0.0f, 50.0f }, 0.0f ) );
    SW_EXPECT_FALSE( frustum.overlapsSphere( float3{ 10.01f, 0.0f, 50.0f }, 0.0f ) );
}

/** @brief [FrustumTest] 시야각이 180° 에 가까워도(투영이 0.001 rad 안으로 자른다) 옆으로 크게 벗어난 앞쪽 점이 남고 뒤쪽 점은 빠진다 */
SW_TEST_CASE( FrustumTest, VeryWideFieldOfViewKeepsSidePoints )
{
    const Frustum frustum = Frustum::fromViewProjection( float4x4::makePerspectiveFieldOfView( MathUtil::kPi - 0.01f, 1.0f, 0.01f, 100.0f ) );
    SW_EXPECT_TRUE( frustum.overlapsSphere( float3{ 10.0f, 0.0f, 1.0f }, 0.0f ) ); // 축에서 84°
    SW_EXPECT_FALSE( frustum.overlapsSphere( float3{ 0.0f, 0.0f, -1.0f }, 0.0f ) );
}

/** @brief [FrustumTest] 퇴화 행렬(0)은 법선 길이 0 이라 평면을 0 으로 둔다 — 아무것도 거르지 않는다(가짜 컬링보다 안전한 쪽) */
SW_TEST_CASE( FrustumTest, DegenerateMatrixCullsNothing )
{
    const float4x4 zero{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    const Frustum  frustum = Frustum::fromViewProjection( zero );
    SW_EXPECT_TRUE( overlaps( frustum, makeBox( 1000.0f, 1000.0f, -1000.0f, 1001.0f, 1001.0f, -999.0f ) ) );
    SW_EXPECT_TRUE( frustum.overlapsSphere( float3{ -1000.0f, 0.0f, -1000.0f }, 0.0f ) );
}

/** @brief [FrustumTest] 회전한 상자를 다시 감싼 월드 상자(`AABB::transformedBy`)는 옮긴 모서리 여덟 개를 모두 품고, 모서리만 들어온 상자도 절두체가 남긴다 */
SW_TEST_CASE( FrustumTest, TransformedBoxCoversRotatedCorners )
{
    const AABB     local = makeBox( -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f );
    const float4x4 world = float4x4::makeRotationY( MathUtil::kPi * 0.25f ) * float4x4::makeTranslation( float3{ -11.3f, 0.0f, 10.0f } );
    const AABB     box   = local.transformedBy( world );
    for ( uint32 corner = 0; corner < 8; ++corner )
    {
        const float3 point = float3::transform( float3{ ( corner & 1u ) != 0u ? 1.0f : -1.0f, ( corner & 2u ) != 0u ? 1.0f : -1.0f, ( corner & 4u ) != 0u ? 1.0f : -1.0f }, world );
        SW_EXPECT_TRUE( point._x >= box._min._x - 1.0e-4f && point._x <= box._max._x + 1.0e-4f );
        SW_EXPECT_TRUE( point._z >= box._min._z - 1.0e-4f && point._z <= box._max._z + 1.0e-4f );
    }
    // 45° 로 돈 모서리가 x = -11.3 + √2 ≈ -9.89 까지 나와 z = 10 의 왼쪽 면(-10) 안으로 들어온다.
    SW_EXPECT_NEAR( -11.3f + MathUtil::sqrt( 2.0f ), box._max._x, 1.0e-4f );
    SW_EXPECT_TRUE( overlaps( Frustum::fromViewProjection( makePerspective90() ), box ) );
}
