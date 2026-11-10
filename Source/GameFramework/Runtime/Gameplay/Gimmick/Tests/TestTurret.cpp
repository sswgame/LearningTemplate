#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Character/Combat/Projectile/ResolvedShot.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Genre/ShooterGimmicks.h"
#include "GameFramework/Runtime/World/Query/WorldQuery.h"

// TurretTest — 포탑이 탄도 계산(`Ballistics`)을 묶어 쓰는 길(`TurretComponent::computeAim`)과 쏜 탄의 세계 질의. 물리 씬 없이 설정된 중력만 읽는다(nogpu).
// 탄도 계산 자체는 Character 층의 `BallisticsTest` 가 본다 — 포탑(Gameplay 층)은 아래층 테스트가 include 하지 못한다(Config/Rules/Layers/GameFrameworkLayers.toml).

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
} // namespace

/** @brief [TurretTest] 포탑의 조준: 히트스캔은 대상 그 자리, 날아가는 탄은 앞, 못 따라잡으면 쏘지 않는다 */
SW_TEST_CASE( TurretTest, TurretAimLeadsOnlyFlyingShots )
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

/** @brief [TurretTest] 포탑 탄은 쏜 뒤 사이에 생긴 벽에 맞아 끝난다(발사 순간 시야만 보지 않는다). 벽이 없으면 대상을 맞히고, 대상의 몸은 막지 않는다 */
SW_TEST_CASE( TurretTest, TurretShotStopsAtAWallThatAppearsInFlight )
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
