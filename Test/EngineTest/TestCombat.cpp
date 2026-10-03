#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Combat/Ballistics.h"
#include "GameFramework/Combat/DamageMath.h"
#include "GameFramework/Combat/LockOnSelector.h"
#include "GameFramework/Combat/TurnOrder.h"
#include "GameFramework/Combat/Weapon.h"

#include "TestFramework/TestFramework.h"

// 장르 공통 전투 계산 — 무기 정의의 거리 감쇠 · 머리 배율 · 탄속 · 탄 아이템, 방어 공식, 탄도(낙차 · 발사각 · 앞 겨누기).

using namespace sw;

namespace
{
    constexpr const utf8* kCombatTestXml = R"(
<WeaponCatalog>
  <Weapon id="smg" damage="20" falloffStart="10" falloffEnd="30" falloffMinScale="0.5" headshotMultiplier="1.5" ammo="ammo_9mm"/>
  <Weapon id="bow" damage="60" projectileSpeed="50" projectileGravity="1" automatic="false"/>
</WeaponCatalog>
)";
} // namespace

SW_TEST_CASE( CombatTest, WeaponDamageFallsOffWithDistanceAndScalesOnHeadshots )
{
    WeaponCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kCombatTestXml, "CombatTest" ) );
    const WeaponDef* pSmg = catalog.findWeapon( hashed_string( "smg" ) );
    SW_ASSERT_NOT_NULL( pSmg );
    SW_EXPECT_TRUE( pSmg->_ammoId == hashed_string( "ammo_9mm" ) );
    SW_EXPECT_NEAR_EQUAL( 20.0f, DamageMath::computeWeaponDamage( *pSmg, 5.0f, false ), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 15.0f, DamageMath::computeWeaponDamage( *pSmg, 20.0f, false ), 1.0e-4f ); // 감쇠 중간
    SW_EXPECT_NEAR_EQUAL( 10.0f, DamageMath::computeWeaponDamage( *pSmg, 99.0f, false ), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 30.0f, DamageMath::computeWeaponDamage( *pSmg, 5.0f, true ), 1.0e-4f );

    const WeaponDef* pBow = catalog.findWeapon( hashed_string( "bow" ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, DamageMath::computeFalloffScale( *pBow, 500.0f ), 1.0e-4f ); // 감쇠 없음
    SW_EXPECT_NEAR_EQUAL( 50.0f, pBow->_projectileSpeed, 1.0e-4f );
    SW_EXPECT_TRUE( pBow->_bAutomatic == SW_FALSE );

    // 방어 — (피해 − 고정) × (1 − 비율), 최소 피해는 지킨다.
    SW_EXPECT_NEAR_EQUAL( 40.0f, DamageMath::applyArmor( 60.0f, 10.0f, 0.2f, 1.0f ), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, DamageMath::applyArmor( 5.0f, 50.0f, 0.0f, 1.0f ), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, DamageMath::applyArmor( 0.5f, 50.0f, 0.0f, 1.0f ), 1.0e-4f ); // 최소가 원래 피해보다 클 수는 없다
    SW_EXPECT_NEAR_EQUAL( 0.0f, DamageMath::applyArmor( 0.0f, 0.0f, 0.0f, 1.0f ), 1.0e-4f );
}

SW_TEST_CASE( CombatTest, BallisticsDropsAimsArcsAndLeadsMovingTargets )
{
    // 낙차 — 100 m 를 100 m/s 로 1 초: ½ g.
    SW_EXPECT_NEAR_EQUAL( 0.5f * Ballistics::kGravity, Ballistics::computeDrop( 100.0f, 100.0f, 1.0f ), 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, Ballistics::computeDrop( 100.0f, 100.0f, 0.0f ), 1.0e-6f );

    // 발사각으로 쏜 탄을 걸음으로 흘리면 목표 근처를 지난다(낮은 · 높은 탄도 모두).
    const float3 from{ 0.0f, 1.0f, 0.0f };
    const float3 to{ 40.0f, 3.0f, 0.0f };
    for ( const bool bHighArc : { false, true } )
    {
        float3 direction{};
        SW_ASSERT_TRUE( Ballistics::computeLaunchDirection( from, to, 30.0f, 1.0f, bHighArc, direction ) );
        Projectile projectile = Ballistics::launch( from, direction, 30.0f, 1.0f, 10.0f );
        float32    bestGap    = MathUtil::MaxFloat;
        while ( projectile.isExpired() == false && projectile._position._y > -5.0f )
        {
            Ballistics::step( projectile, 0.001f );
            bestGap = MathUtil::min( bestGap, ( projectile._position - to ).getLength() );
        }
        SW_EXPECT_TRUE( bestGap < 0.2f );
    }
    float3 unused{};
    SW_EXPECT_FALSE( Ballistics::computeLaunchDirection( from, float3{ 500.0f, 0.0f, 0.0f }, 30.0f, 1.0f, false, unused ) ); // 닿지 않는다

    // 앞 겨누기 — 옆으로 달리는 목표.
    const float3 shooter{ 0.0f, 0.0f, 0.0f };
    const float3 target{ 0.0f, 0.0f, 30.0f };
    const float3 velocity{ 10.0f, 0.0f, 0.0f };
    float3       intercept{};
    SW_ASSERT_TRUE( Ballistics::computeInterceptPoint( shooter, target, velocity, 50.0f, intercept ) );
    const float32 bulletTime = intercept.getLength() / 50.0f;
    const float3  targetThen = target + velocity * bulletTime;
    SW_EXPECT_TRUE( ( targetThen - intercept ).getLength() < 1.0e-2f );
    SW_EXPECT_FALSE( Ballistics::computeInterceptPoint( shooter, target, float3{ 0.0f, 0.0f, 80.0f }, 50.0f, intercept ) ); // 달아나는 쪽이 빠르다
}

SW_TEST_CASE( CombatTest, TurnOrderSortsRoundsByPriorityAndRunsTimelinesBySpeed )
{
    // 라운드제 — 우선도 → 속도. 미리 보기는 실제 순서와 같다(동속은 씨앗 난수).
    TurnOrder rounds;
    rounds.initialize( TurnOrderMode::Rounds, 9u );
    rounds.addActor( 1, 50.0f );
    rounds.addActor( 2, 90.0f );
    rounds.addActor( 3, 70.0f );
    rounds.addActor( 4, 70.0f );
    rounds.setPriority( 1, 1 ); // 선제 기술
    vector<int32> listPreview;
    rounds.previewOrder( 8, listPreview );
    SW_EXPECT_EQUAL( 1, listPreview[0] );
    SW_EXPECT_EQUAL( 2, listPreview[1] );
    for ( const int32 expected : listPreview )
        SW_EXPECT_EQUAL( expected, rounds.next() );
    SW_EXPECT_EQUAL( 2, rounds.getRound() );
    rounds.removeActor( 2 );
    rounds.restartRound();
    SW_EXPECT_EQUAL( 1, rounds.next() );
    SW_EXPECT_TRUE( rounds.next() != 2 );

    // 타임라인제 — 두 배 빠르면 두 번. 늦추면 밀린다.
    TurnOrder timeline;
    timeline.initialize( TurnOrderMode::Timeline, 1u );
    timeline.addActor( 10, 20.0f );
    timeline.addActor( 20, 10.0f );
    timeline.previewOrder( 6, listPreview );
    int32 fastCount = 0;
    for ( const int32 expected : listPreview )
    {
        const int32 actor = timeline.next();
        SW_EXPECT_EQUAL( expected, actor );
        fastCount += actor == 10 ? 1 : 0;
    }
    SW_EXPECT_EQUAL( 4, fastCount );
    timeline.delayActor( 10, 1.5f ); // 한 차례 반 밀린다
    timeline.previewOrder( 2, listPreview );
    SW_EXPECT_EQUAL( 20, listPreview[0] );
    timeline.setSpeed( 20, 1000.0f );
    SW_EXPECT_EQUAL( 20, timeline.next() );
}

SW_TEST_CASE( CombatTest, LockOnPicksCentredTargetsCyclesSidewaysAndBreaksWhenLost )
{
    const float3            eye{ 0.0f, 0.0f, 0.0f };
    const float3            forward{ 0.0f, 0.0f, 1.0f };
    vector<LockOnCandidate> listCandidate;
    listCandidate.push_back( LockOnCandidate{
        float3{ 0.5f, 0.0f, 10.0f },
        1, 0.0f, SW_TRUE
    } ); // 정면
    listCandidate.push_back( LockOnCandidate{
        float3{ 6.0f, 0.0f, 6.0f },
        2, 0.0f, SW_TRUE
    } ); // 오른쪽 45°
    listCandidate.push_back( LockOnCandidate{
        float3{ -4.0f, 0.0f, 8.0f },
        3, 0.0f, SW_TRUE
    } ); // 왼쪽
    listCandidate.push_back( LockOnCandidate{
        float3{ 0.0f, 0.0f, -5.0f },
        4, 0.0f, SW_TRUE
    } ); // 뒤 — 새로 잡지 않는다
    listCandidate.push_back( LockOnCandidate{
        float3{ 0.0f, 0.0f, 40.0f },
        5, 0.0f, SW_TRUE
    } ); // 너무 멀다
    LockOnSelector selector;
    SW_EXPECT_EQUAL( 1, static_cast<int32>( selector.pickBest( eye, forward, listCandidate ) ) );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( selector.cycle( eye, forward, listCandidate, 1 ) ) );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( selector.cycle( eye, forward, listCandidate, 1 ) ) ); // 더 오른쪽은 뒤(4)뿐 — 시야 밖은 넘기지 않는다
    SW_EXPECT_EQUAL( 1, static_cast<int32>( selector.cycle( eye, forward, listCandidate, -1 ) ) );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( selector.cycle( eye, forward, listCandidate, -1 ) ) );

    // 우선도 — 보스는 조금 비껴 있어도 먼저.
    listCandidate[2]._priority = 2.0f;
    SW_EXPECT_EQUAL( 3, static_cast<int32>( selector.pickBest( eye, forward, listCandidate ) ) );

    // 가려지면 잠깐은 유지, 오래면 풀린다. 멀어져도 풀린다.
    listCandidate[2]._bVisible = SW_FALSE;
    SW_EXPECT_TRUE( selector.update( eye, listCandidate, 0.5f ) );
    SW_EXPECT_FALSE( selector.update( eye, listCandidate, 0.6f ) );
    SW_EXPECT_FALSE( selector.hasTarget() );
    (void)selector.pickBest( eye, forward, listCandidate );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( selector.getTarget() ) );
    listCandidate[0]._position = float3{ 0.0f, 0.0f, 33.0f };
    SW_EXPECT_FALSE( selector.update( eye, listCandidate, 0.1f ) );
}
