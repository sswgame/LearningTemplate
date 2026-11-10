#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Physics/PhysicsSystem.h"

#include "GameFramework/Runtime/Character/Combat/Weapon/Ballistics.h"

// BallisticsTest — 앞 겨누기 · 포물선 발사 방향 · 낙차. 물리 씬 없이 설정된 중력만 읽는다(nogpu). 포탑이 셋을 묶어 쓰는 길은 Gameplay/Gimmick/Tests/TestTurret.cpp.

using namespace sw;

namespace
{
    struct BallisticsTestUtil
    {
        /** @brief 방향과 속력으로 쏜 탄이 수평 거리 @p horizontal 에 닿는 순간의 높이입니다(진공 포물선). */
        static float32 computeHeightAt( const float3& direction, float32 speed, float32 gravity, float32 horizontal )
        {
            const float32 horizontalSpeed = MathUtil::sqrt( direction._x * direction._x + direction._z * direction._z ) * speed;
            const float32 time            = horizontal / horizontalSpeed;
            return direction._y * speed * time - 0.5f * gravity * time * time;
        }
    };
} // namespace

/** @brief [BallisticsTest] 앞을 겨눈 자리에 탄과 대상이 같은 시간에 닿는다 */
SW_TEST_CASE( BallisticsTest, InterceptPointMeetsTheTargetAtTheSameTime )
{
    const float3 shooter{ 0.0f, 0.0f, 0.0f };
    const float3 target{ 0.0f, 0.0f, 20.0f };
    const float3 velocity{ 5.0f, 0.0f, 0.0f };
    float3       point{};
    SW_REQUIRE_TRUE( Ballistics::computeInterceptPoint( shooter, target, velocity, 25.0f, point ) );
    const float32 shotTime   = float3::getDistance( shooter, point ) / 25.0f;
    const float32 targetTime = float3::getDistance( target, point ) / 5.0f;
    SW_EXPECT_TRUE( MathUtil::abs( shotTime - targetTime ) < 1.0e-3f );
    SW_EXPECT_TRUE( point._x > 0.0f ); // 대상이 가는 쪽 앞이다
}

/** @brief [BallisticsTest] 달아나는 대상보다 느린 탄은 따라잡지 못한다 */
SW_TEST_CASE( BallisticsTest, SlowerShotCannotCatchAFleeingTarget )
{
    float3 point{};
    SW_EXPECT_FALSE( Ballistics::computeInterceptPoint( float3{}, float3{ 0.0f, 0.0f, 10.0f }, float3{ 0.0f, 0.0f, 30.0f }, 20.0f, point ) );
}

/** @brief [BallisticsTest] 포물선 해는 둘 다 목표 높이에 닿고, 힘이 모자라면 해가 없다 */
SW_TEST_CASE( BallisticsTest, LaunchDirectionReachesTheTargetOnBothArcs )
{
    const float32 gravity = PhysicsSystem::getConfiguredGravityMagnitude();
    SW_REQUIRE_TRUE( gravity > 0.0f );
    const float3  from{ 0.0f, 0.0f, 0.0f };
    const float3  to{ 0.0f, 2.0f, 30.0f };
    const float32 speed = 40.0f;
    float3        low{};
    float3        high{};
    SW_REQUIRE_TRUE( Ballistics::computeLaunchDirection( from, to, speed, 1.0f, false, low ) );
    SW_REQUIRE_TRUE( Ballistics::computeLaunchDirection( from, to, speed, 1.0f, true, high ) );
    SW_EXPECT_TRUE( high._y > low._y );
    SW_EXPECT_TRUE( MathUtil::abs( BallisticsTestUtil::computeHeightAt( low, speed, gravity, 30.0f ) - 2.0f ) < 0.01f );
    SW_EXPECT_TRUE( MathUtil::abs( BallisticsTestUtil::computeHeightAt( high, speed, gravity, 30.0f ) - 2.0f ) < 0.01f );

    float3 unreachable{};
    SW_EXPECT_FALSE( Ballistics::computeLaunchDirection( from, float3{ 0.0f, 0.0f, 1000.0f }, 10.0f, 1.0f, false, unreachable ) );
}

/** @brief [BallisticsTest] 낙차는 비행 시간의 제곱에 비례하고 중력 배율 0 이면 없다 */
SW_TEST_CASE( BallisticsTest, DropGrowsWithTheSquareOfFlightTime )
{
    const float32 shortDrop = Ballistics::computeDrop( 10.0f, 100.0f, 1.0f );
    const float32 longDrop  = Ballistics::computeDrop( 20.0f, 100.0f, 1.0f );
    SW_EXPECT_TRUE( MathUtil::abs( longDrop - shortDrop * 4.0f ) < 1.0e-4f );
    SW_EXPECT_EQUAL( 0.0f, Ballistics::computeDrop( 20.0f, 100.0f, 0.0f ) );
}
