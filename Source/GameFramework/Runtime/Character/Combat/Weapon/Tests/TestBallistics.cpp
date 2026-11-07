#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Physics/PhysicsSystem.h"

#include "GameFramework/Runtime/Character/Combat/Projectile/ResolvedShot.h"
#include "GameFramework/Runtime/Character/Combat/Weapon/Ballistics.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Genre/ShooterGimmicks.h"
#include "GameFramework/Runtime/World/Query/WorldQuery.h"

// BallisticsTest — 앞 겨누기 · 포물선 발사 방향 · 낙차와 포탑이 그 셋을 묶어 쓰는 길(`TurretComponent::computeAim`). 물리 씬 없이 설정된 중력만 읽는다(nogpu).

using namespace sw;

namespace
{
    /** @brief z = `_wallZ` 의 벽 하나입니다. `_bWall` 이 거짓이면 아무것도 막지 않습니다(쏜 뒤에 벽이 생기는 것을 흉내 낸다). */
    class WallWorldQuery final : public IWorldQuery
    {
    public:
        uint64  _wallID;
        float32 _wallZ;
        uint8   _bWall : 1;

        WallWorldQuery()
            : _wallID{ 99 }
            , _wallZ{ 10.0f }
            , _bWall{ SW_FALSE }
        {
        }

        bool raycast( const float3& from, const float3& to, uint64 /*ignoreObjectID*/, WorldRayHit& outHit ) const override
        {
            if ( _bWall == SW_FALSE || from._z >= _wallZ || to._z < _wallZ )
                return false;
            outHit._fraction = ( _wallZ - from._z ) / ( to._z - from._z );
            outHit._point    = from + ( to - from ) * outHit._fraction;
            outHit._objectID = _wallID;
            return true;
        }
    };

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

/** @brief [BallisticsTest] 포탑의 조준: 히트스캔은 대상 그 자리, 날아가는 탄은 앞, 못 따라잡으면 쏘지 않는다 */
SW_TEST_CASE( BallisticsTest, TurretAimLeadsOnlyFlyingShots )
{
    const float3 origin{ 0.0f, 0.0f, 0.0f };
    const float3 target{ 0.0f, 0.0f, 20.0f };
    const float3 velocity{ 6.0f, 0.0f, 0.0f };
    float3       aimPoint{};
    float3       direction{};
    SW_REQUIRE_TRUE( TurretComponent::computeAim( origin, target, velocity, 0.0f, 0.0f, aimPoint, direction ) );
    SW_EXPECT_EQUAL( 0.0f, aimPoint._x );

    SW_REQUIRE_TRUE( TurretComponent::computeAim( origin, target, velocity, 25.0f, 0.0f, aimPoint, direction ) );
    SW_EXPECT_TRUE( aimPoint._x > 0.0f );
    SW_EXPECT_TRUE( direction._x > 0.0f );

    SW_REQUIRE_TRUE( TurretComponent::computeAim( origin, target, velocity, 25.0f, 1.0f, aimPoint, direction ) );
    SW_EXPECT_TRUE( direction._y > 0.0f ); // 중력이 있으면 위로 쏜다

    SW_EXPECT_FALSE( TurretComponent::computeAim( origin, target, float3{ 0.0f, 0.0f, 30.0f }, 25.0f, 0.0f, aimPoint, direction ) );
}

/** @brief [BallisticsTest] 포탑 탄은 쏜 뒤 사이에 생긴 벽에 맞아 끝난다(발사 순간 시야만 보지 않는다). 벽이 없으면 대상을 맞히고, 대상의 몸은 막지 않는다 */
SW_TEST_CASE( BallisticsTest, TurretShotStopsAtAWallThatAppearsInFlight )
{
    vector<TurretShotTarget> listTarget{
        TurretShotTarget{ float3{ 0.0f, 0.0f, 20.0f }, 7u }
    };
    ResolvedShot shot;
    shot._damage = 10.0f;
    shot._speed  = 25.0f;
    shot._range  = 40.0f;

    // 벽 없이: 0.8 초 비행 뒤 대상에 맞는다.
    WallWorldQuery       open;
    TurretShotWorld      openWorld{ listTarget, 0.6f, open, 1u };
    ProjectileSimulation clear;
    (void)clear.spawn( ProjectileState::launch( shot, float3{}, float3{ 0.0f, 0.0f, 1.0f }, 0.0f ) );
    int32 targetHits = 0;
    for ( int32 step = 0; step < 120 && clear.getProjectiles().empty() == false; ++step )
    {
        for ( const ProjectileEvent& event : clear.step( 1.0f / 60.0f, openWorld ) )
        {
            targetHits += event._kind == ProjectileEvent::Kind::Hit && event._hit._kind == ProjectileHitKind::Target ? 1 : 0;
        }
    }
    SW_EXPECT_EQUAL( 1, targetHits );

    // 쏜 뒤(몇 걸음 날아간 다음) 벽이 생긴다: 탄은 벽에서 끝나고 대상을 맞히지 않는다.
    WallWorldQuery       wall;
    TurretShotWorld      wallWorld{ listTarget, 0.6f, wall, 1u };
    ProjectileSimulation blocked;
    (void)blocked.spawn( ProjectileState::launch( shot, float3{}, float3{ 0.0f, 0.0f, 1.0f }, 0.0f ) );
    int32  blockedTargetHits = 0;
    int32  surfaceHits       = 0;
    float3 surfacePoint{};
    for ( int32 step = 0; step < 120 && blocked.getProjectiles().empty() == false; ++step )
    {
        wall._bWall = step >= 5 ? SW_TRUE : SW_FALSE; // 탄이 약 2 m 나간 뒤
        for ( const ProjectileEvent& event : blocked.step( 1.0f / 60.0f, wallWorld ) )
        {
            const bool bHit = event._kind == ProjectileEvent::Kind::Hit;
            blockedTargetHits += bHit && event._hit._kind == ProjectileHitKind::Target ? 1 : 0;
            if ( bHit && event._hit._kind == ProjectileHitKind::Surface )
            {
                ++surfaceHits;
                surfacePoint = event._hit._position;
            }
        }
    }
    SW_EXPECT_EQUAL( 0, blockedTargetHits );
    SW_EXPECT_EQUAL( 1, surfaceHits );
    SW_EXPECT_NEAR( 10.0f, surfacePoint._z, 1.0e-3f );

    // 대상 자신의 몸이 먼저 맞아도 막지 않는다(대상은 구로 맞힌다).
    WallWorldQuery body;
    body._bWall  = SW_TRUE;
    body._wallZ  = 19.0f; // 구(반지름 0.6, 19.4 m 에서 맞는다)보다 앞
    body._wallID = 7u;
    TurretShotWorld      bodyWorld{ listTarget, 0.6f, body, 1u };
    ProjectileSimulation toBody;
    (void)toBody.spawn( ProjectileState::launch( shot, float3{}, float3{ 0.0f, 0.0f, 1.0f }, 0.0f ) );
    int32 bodyHits = 0;
    for ( int32 step = 0; step < 120 && toBody.getProjectiles().empty() == false; ++step )
    {
        for ( const ProjectileEvent& event : toBody.step( 1.0f / 60.0f, bodyWorld ) )
        {
            bodyHits += event._kind == ProjectileEvent::Kind::Hit && event._hit._kind == ProjectileHitKind::Target ? 1 : 0;
        }
    }
    SW_EXPECT_EQUAL( 1, bodyHits );
}
