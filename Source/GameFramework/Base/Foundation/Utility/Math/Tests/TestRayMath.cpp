#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Base/Foundation/Utility/Math/OrientationUtil.h"
#include "GameFramework/Base/Foundation/Utility/Math/RayMath.h"

// RayMathTest — 광선 판정(구 · 상자 · 캡슐 · 수평면: 앞쪽 가장 가까운 거리, 시작점이 안이면 0, 뒤 · 최대 거리 밖은 놓침), 시선 방향, 원뿔 시야(3D · 평면),
// 앞 · 위 방향 → 오일러(요 · 피치 · 롤, 수직 앞의 요), 짧은 쪽으로 돌기(±π 를 넘는 쪽). nogpu.

namespace
{
    constexpr float64 kTolerance = 1.0e-4;

    sw::GameRay makeRay( const sw::float3& origin, const sw::float3& direction )
    {
        sw::GameRay ray;
        ray._origin    = origin;
        ray._direction = direction;
        return ray;
    }
} // namespace

/**
 * @brief [RayMathTest] 구: 앞쪽 표면까지 거리, 시작점이 안이면 0, 뒤에 있거나 비켜 가거나 최대 거리보다 멀면 놓친다
 */
SW_TEST_CASE( RayMathTest, SphereNearestFrontHit )
{
    const sw::GameRay ray      = makeRay( sw::float3{ 0.0f, 0.0f, 0.0f }, sw::float3{ 0.0f, 0.0f, 1.0f } );
    float32           distance = -1.0f;
    SW_REQUIRE_TRUE( sw::RayMath::intersectSphere( ray, sw::float3{ 0.0f, 0.0f, 10.0f }, 2.0f, 100.0f, distance ) );
    SW_EXPECT_NEAR( 8.0, distance, kTolerance );
    SW_REQUIRE_TRUE( sw::RayMath::intersectSphere( ray, sw::float3{ 0.0f, 0.0f, 1.0f }, 2.0f, 100.0f, distance ) );
    SW_EXPECT_NEAR( 0.0, distance, kTolerance ); // 안에서 쏘았다

    distance = -1.0f;
    SW_EXPECT_FALSE( sw::RayMath::intersectSphere( ray, sw::float3{ 0.0f, 0.0f, -10.0f }, 2.0f, 100.0f, distance ) ); // 뒤
    SW_EXPECT_FALSE( sw::RayMath::intersectSphere( ray, sw::float3{ 3.0f, 0.0f, 10.0f }, 2.0f, 100.0f, distance ) );  // 비켜 간다
    SW_EXPECT_FALSE( sw::RayMath::intersectSphere( ray, sw::float3{ 0.0f, 0.0f, 10.0f }, 2.0f, 7.9f, distance ) );    // 최대 거리 밖
    SW_EXPECT_EQUAL( -1.0f, distance );                                                                               // 놓치면 거리를 쓰지 않는다
    SW_REQUIRE_TRUE( sw::RayMath::intersectSphere( ray, sw::float3{ 2.0f, 0.0f, 10.0f }, 2.0f, 100.0f, distance ) );  // 스친다
    SW_EXPECT_NEAR( 10.0, distance, 1.0e-3 );
}

/**
 * @brief [RayMathTest] 상자(슬랩): 들어가는 면까지 거리, 안이면 0, 한 축과 나란한 광선은 그 슬랩 안일 때만, 뒤 · 최대 거리 밖은 놓친다
 */
SW_TEST_CASE( RayMathTest, AABBSlabHit )
{
    const sw::float3 boxMin{ -1.0f, -1.0f, 4.0f };
    const sw::float3 boxMax{ 1.0f, 1.0f, 6.0f };
    float32          distance = -1.0f;
    SW_REQUIRE_TRUE( sw::RayMath::intersectAABB( makeRay( sw::float3{ 0.0f, 0.0f, 0.0f }, sw::float3{ 0.0f, 0.0f, 1.0f } ), boxMin, boxMax, 100.0f, distance ) );
    SW_EXPECT_NEAR( 4.0, distance, kTolerance );
    SW_REQUIRE_TRUE( sw::RayMath::intersectAABB( makeRay( sw::float3{ 0.0f, 0.0f, 5.0f }, sw::float3{ 1.0f, 0.0f, 0.0f } ), boxMin, boxMax, 100.0f, distance ) );
    SW_EXPECT_NEAR( 0.0, distance, kTolerance );

    // z 와 나란하지만 x 슬랩 밖
    SW_EXPECT_FALSE( sw::RayMath::intersectAABB( makeRay( sw::float3{ 2.0f, 0.0f, 0.0f }, sw::float3{ 0.0f, 0.0f, 1.0f } ), boxMin, boxMax, 100.0f, distance ) );
    SW_EXPECT_FALSE( sw::RayMath::intersectAABB( makeRay( sw::float3{ 0.0f, 0.0f, 10.0f }, sw::float3{ 0.0f, 0.0f, 1.0f } ), boxMin, boxMax, 100.0f, distance ) );
    SW_EXPECT_FALSE( sw::RayMath::intersectAABB( makeRay( sw::float3{ 0.0f, 0.0f, 0.0f }, sw::float3{ 0.0f, 0.0f, 1.0f } ), boxMin, boxMax, 3.5f, distance ) );

    // 대각선으로 x 면(-1)에 들어간다 — 그때 z 는 5 라 상자 안이다
    const float32 inverseRoot2 = 1.0f / sw::MathUtil::sqrt( 2.0f );
    SW_REQUIRE_TRUE( sw::RayMath::intersectAABB( makeRay( sw::float3{ -4.0f, 0.0f, 2.0f }, sw::float3{ inverseRoot2, 0.0f, inverseRoot2 } ), boxMin, boxMax, 100.0f,
                                                 distance ) );
    SW_EXPECT_NEAR( 3.0 * 1.41421356, distance, 1.0e-3 );
}

/**
 * @brief [RayMathTest] 캡슐: 옆면(선분 안 높이) · 끝 반구 중 가까운 것, 안이면 0, 길이 0 은 구와 같고, 옆면을 지나도 선분 밖 높이면 반구가 받는다
 */
SW_TEST_CASE( RayMathTest, CapsuleSideAndCaps )
{
    const sw::float3 bottom{ 0.0f, 0.0f, 10.0f };
    const sw::float3 top{ 0.0f, 2.0f, 10.0f };
    float32          distance = -1.0f;

    SW_REQUIRE_TRUE( sw::RayMath::intersectCapsule( makeRay( sw::float3{ 0.0f, 1.0f, 0.0f }, sw::float3{ 0.0f, 0.0f, 1.0f } ), bottom, top, 0.5f, 100.0f, distance ) );
    SW_EXPECT_NEAR( 9.5, distance, kTolerance ); // 옆면

    SW_REQUIRE_TRUE( sw::RayMath::intersectCapsule( makeRay( sw::float3{ 0.0f, 2.3f, 0.0f }, sw::float3{ 0.0f, 0.0f, 1.0f } ), bottom, top, 0.5f, 100.0f, distance ) );
    SW_EXPECT_NEAR( 10.0 - 0.4, distance, 1.0e-3 ); // 위 반구(높이 0.3 위 — 반지름 0.5 원의 반폭 0.4)

    SW_EXPECT_FALSE( sw::RayMath::intersectCapsule( makeRay( sw::float3{ 0.0f, 2.6f, 0.0f }, sw::float3{ 0.0f, 0.0f, 1.0f } ), bottom, top, 0.5f, 100.0f, distance ) );
    SW_EXPECT_FALSE( sw::RayMath::intersectCapsule( makeRay( sw::float3{ 0.0f, 1.0f, 0.0f }, sw::float3{ 0.0f, 0.0f, 1.0f } ), bottom, top, 0.5f, 9.0f, distance ) );
    SW_EXPECT_FALSE( sw::RayMath::intersectCapsule( makeRay( sw::float3{ 0.0f, 1.0f, 20.0f }, sw::float3{ 0.0f, 0.0f, 1.0f } ), bottom, top, 0.5f, 100.0f, distance ) );

    SW_REQUIRE_TRUE( sw::RayMath::intersectCapsule( makeRay( sw::float3{ 0.2f, 1.5f, 10.0f }, sw::float3{ 0.0f, 0.0f, 1.0f } ), bottom, top, 0.5f, 100.0f, distance ) );
    SW_EXPECT_NEAR( 0.0, distance, kTolerance ); // 안

    // 축과 나란한 광선(옆면 식이 풀리지 않는다)은 반구가 받는다
    SW_REQUIRE_TRUE( sw::RayMath::intersectCapsule( makeRay( sw::float3{ 0.0f, -5.0f, 10.0f }, sw::float3{ 0.0f, 1.0f, 0.0f } ), bottom, top, 0.5f, 100.0f, distance ) );
    SW_EXPECT_NEAR( 4.5, distance, kTolerance );

    float32 sphereDistance = -1.0f;
    SW_REQUIRE_TRUE( sw::RayMath::intersectCapsule( makeRay( sw::float3{ 0.0f, 0.0f, 0.0f }, sw::float3{ 0.0f, 0.0f, 1.0f } ), bottom, bottom, 0.5f, 100.0f, distance ) );
    SW_REQUIRE_TRUE( sw::RayMath::intersectSphere( makeRay( sw::float3{ 0.0f, 0.0f, 0.0f }, sw::float3{ 0.0f, 0.0f, 1.0f } ), bottom, 0.5f, 100.0f, sphereDistance ) );
    SW_EXPECT_NEAR( sphereDistance, distance, kTolerance );
}

/**
 * @brief [RayMathTest] 수평면: 앞쪽 거리, 나란하거나 뒤이거나 최대 거리 밖이면 놓친다
 */
SW_TEST_CASE( RayMathTest, HorizontalPlaneHit )
{
    float32 distance = -1.0f;
    SW_REQUIRE_TRUE( sw::RayMath::intersectHorizontalPlane( makeRay( sw::float3{ 0.0f, 10.0f, 0.0f }, sw::float3{ 0.0f, -1.0f, 0.0f } ), 2.0f, 100.0f, distance ) );
    SW_EXPECT_NEAR( 8.0, distance, kTolerance );
    SW_EXPECT_FALSE( sw::RayMath::intersectHorizontalPlane( makeRay( sw::float3{ 0.0f, 10.0f, 0.0f }, sw::float3{ 1.0f, 0.0f, 0.0f } ), 2.0f, 100.0f, distance ) );
    SW_EXPECT_FALSE( sw::RayMath::intersectHorizontalPlane( makeRay( sw::float3{ 0.0f, 10.0f, 0.0f }, sw::float3{ 0.0f, 1.0f, 0.0f } ), 2.0f, 100.0f, distance ) );
    SW_EXPECT_FALSE( sw::RayMath::intersectHorizontalPlane( makeRay( sw::float3{ 0.0f, 10.0f, 0.0f }, sw::float3{ 0.0f, -1.0f, 0.0f } ), 2.0f, 7.0f, distance ) );
}

/**
 * @brief [RayMathTest] 시선 방향: 요 0 · 피치 0 은 +Z, 요 +π/2 는 +X, 피치 +π/2 는 +Y, 언제나 단위 벡터다
 */
SW_TEST_CASE( RayMathTest, LookDirectionFollowsYawPitch )
{
    const sw::float3 forward = sw::RayMath::computeLookDirection( 0.0f, 0.0f );
    SW_EXPECT_NEAR( 1.0, forward._z, kTolerance );
    const sw::float3 right = sw::RayMath::computeLookDirection( sw::MathUtil::kHalfPi, 0.0f );
    SW_EXPECT_NEAR( 1.0, right._x, kTolerance );
    SW_EXPECT_NEAR( 0.0, right._z, kTolerance );
    const sw::float3 upward = sw::RayMath::computeLookDirection( 1.0f, sw::MathUtil::kHalfPi );
    SW_EXPECT_NEAR( 1.0, upward._y, kTolerance );
    const sw::float3 any = sw::RayMath::computeLookDirection( 2.3f, -0.7f );
    SW_EXPECT_NEAR( 1.0, any.getLength(), kTolerance );
    SW_EXPECT_TRUE( any._y < 0.0f ); // 피치 음수는 아래
}

/**
 * @brief [RayMathTest] 원뿔 시야: 반각 · 거리 안만, 꼭짓점은 안, 앞 방향은 단위가 아니어도 된다. 평면 원뿔은 높이를 무시한다
 */
SW_TEST_CASE( RayMathTest, ConeChecksAngleAndRange )
{
    const sw::float3 origin{ 0.0f, 0.0f, 0.0f };
    const sw::float3 forward{ 0.0f, 0.0f, 5.0f };
    SW_EXPECT_TRUE( sw::RayMath::isInCone( origin, forward, 30.0f, 10.0f, sw::float3{ 0.0f, 0.0f, 9.0f } ) );
    SW_EXPECT_TRUE( sw::RayMath::isInCone( origin, forward, 30.0f, 10.0f, sw::float3{ 2.0f, 0.0f, 5.0f } ) );  // 약 21.8 도
    SW_EXPECT_FALSE( sw::RayMath::isInCone( origin, forward, 30.0f, 10.0f, sw::float3{ 4.0f, 0.0f, 5.0f } ) ); // 약 38.7 도
    SW_EXPECT_FALSE( sw::RayMath::isInCone( origin, forward, 30.0f, 10.0f, sw::float3{ 0.0f, 0.0f, 10.5f } ) );
    SW_EXPECT_FALSE( sw::RayMath::isInCone( origin, forward, 30.0f, 10.0f, sw::float3{ 0.0f, 0.0f, -3.0f } ) );
    SW_EXPECT_TRUE( sw::RayMath::isInCone( origin, forward, 30.0f, 10.0f, origin ) );
    SW_EXPECT_TRUE( sw::RayMath::isInCone( origin, forward, 170.0f, 10.0f, sw::float3{ 1.0f, 0.0f, -3.0f } ) ); // 약 161.6 도 — 넓은 반각은 뒤쪽도 받는다

    // 높이 차이가 커서 3D 로는 밖이지만 평면으로는 안
    const sw::float3 high{ 0.0f, 8.0f, 4.0f };
    SW_EXPECT_FALSE( sw::RayMath::isInCone( origin, forward, 30.0f, 10.0f, high ) );
    SW_EXPECT_TRUE( sw::RayMath::isInFlatCone( origin, forward, 30.0f, 10.0f, high ) );
}

/**
 * @brief [RayMathTest] 앞 · 위 → 오일러: +Z 앞 · +Y 위는 0, +X 앞은 요 π/2, 아래를 보면 피치 +π/2 쪽(-asin), 위를 옆으로 눕히면 롤, 앞이 수직이면 요를 위에서 고르고 롤 0
 */
SW_TEST_CASE( RayMathTest, EulerFromForwardUp )
{
    const sw::float3 up{ 0.0f, 1.0f, 0.0f };
    const sw::float3 identity = sw::OrientationUtil::computeEulerFromForwardUp( sw::float3{ 0.0f, 0.0f, 3.0f }, up );
    SW_EXPECT_NEAR( 0.0, identity._x, kTolerance );
    SW_EXPECT_NEAR( 0.0, identity._y, kTolerance );
    SW_EXPECT_NEAR( 0.0, identity._z, kTolerance );

    const sw::float3 turned = sw::OrientationUtil::computeEulerFromForwardUp( sw::float3{ 1.0f, 0.0f, 0.0f }, up );
    SW_EXPECT_NEAR( sw::MathUtil::kHalfPi, turned._y, kTolerance );
    SW_EXPECT_NEAR( 0.0, turned._z, kTolerance );

    const sw::float3 down = sw::OrientationUtil::computeEulerFromForwardUp( sw::float3{ 0.0f, -1.0f, 1.0f }, up );
    SW_EXPECT_NEAR( sw::MathUtil::kPi * 0.25f, down._x, kTolerance );

    const sw::float3 rolled = sw::OrientationUtil::computeEulerFromForwardUp( sw::float3{ 0.0f, 0.0f, 1.0f }, sw::float3{ 1.0f, 0.0f, 0.0f } );
    SW_EXPECT_NEAR( -sw::MathUtil::kHalfPi, rolled._z, kTolerance ); // 위가 +X — 위 = cos r · 롤 없는 위 − sin r · 오른쪽

    const sw::float3 vertical = sw::OrientationUtil::computeEulerFromForwardUp( sw::float3{ 0.0f, -1.0f, 0.0f }, sw::float3{ 0.0f, 0.0f, 1.0f } );
    SW_EXPECT_NEAR( sw::MathUtil::kHalfPi, vertical._x, kTolerance );
    SW_EXPECT_NEAR( 0.0, vertical._y, kTolerance );
    SW_EXPECT_NEAR( 0.0, vertical._z, kTolerance );

    const sw::float3 zero = sw::OrientationUtil::computeEulerFromForwardUp( sw::float3{ 0.0f, 0.0f, 0.0f }, up );
    SW_EXPECT_TRUE( zero._x == 0.0f && zero._y == 0.0f && zero._z == 0.0f );
}

/**
 * @brief [RayMathTest] 짧은 쪽으로 돌기: 최대 걸음까지만, 남은 각이 작으면 목표 그대로, ±π 를 넘는 쪽이 짧으면 그쪽으로 감아 돈다, 음수 걸음은 0
 */
SW_TEST_CASE( RayMathTest, TurnTowardAngleTakesShortWay )
{
    SW_EXPECT_NEAR( 0.1, sw::OrientationUtil::turnTowardAngle( 0.0f, 1.0f, 0.1f ), kTolerance );
    SW_EXPECT_NEAR( 1.0, sw::OrientationUtil::turnTowardAngle( 0.95f, 1.0f, 0.1f ), kTolerance );

    // 3.0 에서 -3.0 은 π 를 넘는 쪽이 짧다(약 0.283) — 각을 키우는 쪽으로 돈다
    const float32 crossed = sw::OrientationUtil::turnTowardAngle( 3.0f, -3.0f, 0.1f );
    SW_EXPECT_NEAR( 3.1, crossed, kTolerance );
    const float32 wrapped = sw::OrientationUtil::turnTowardAngle( 3.1f, -3.1f, 0.2f );
    SW_EXPECT_NEAR( -3.1, wrapped, 1.0e-3 ); // 남은 각 약 0.083 — 목표에 닿고 [-π, π] 안으로 감는다

    SW_EXPECT_NEAR( 0.5, sw::OrientationUtil::turnTowardAngle( 0.5f, 2.0f, -1.0f ), kTolerance );
}
