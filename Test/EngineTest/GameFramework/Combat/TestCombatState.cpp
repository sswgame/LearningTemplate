#include "pch.h"

#include "GameFramework/Base/Combat/ElementChart.h"
#include "GameFramework/Base/Combat/FrameData.h"
#include "GameFramework/Base/Combat/ResourceGauge.h"
#include "GameFramework/Base/Combat/Vitality.h"
#include "GameFramework/Base/Utility/GameRandom.h"

#include "TestFramework/TestFramework.h"

// 장르 공통 전투 상태 — 체력(실드 · 기절 · 부활 · 경직), 게이지(탈진 · 과열), 프레임 데이터(위상 · 캔슬 · 히트스톱 · 이득 · 가드), 속성 상성.

using namespace sw;

namespace
{
    constexpr const utf8* kCombatStateMoveXml = R"(
<MoveCatalog>
  <Move id="jab" startup="10" active="2" recovery="15" damage="12" chip="2" hitstun="20" blockstun="12" hitstop="3" height="High">
    <Hitbox from="10" to="11" x="0.8" y="1.5" w="0.6" h="0.3"/>
    <Hitbox from="11" to="11" x="1.2" y="1.5" w="0.4" h="0.3" shape="Capsule"/>
    <Cancel from="12" to="20" moves="jab2, launcher" onHit="true"/>
    <Cancel from="18" to="26" moves="backdash"/>
  </Move>
  <Move id="sweep" startup="16" active="3" recovery="30" damage="18" hitstun="40" blockstun="8" height="Low" knockdown="true"/>
  <Move/>
</MoveCatalog>
)";

    constexpr const utf8* kCombatStateElementXml = R"(
<ElementChart>
  <Element id="Fire"/><Element id="Water"/><Element id="Grass"/><Element id="Ground"/><Element id="Flying"/><Element id="Electric"/>
  <Rule attack="Water" defend="Fire" multiplier="2"/>
  <Rule attack="Water" defend="Grass" multiplier="0.5"/>
  <Rule attack="Water" defend="Ground" multiplier="2"/>
  <Rule attack="Electric" defend="Ground" multiplier="0"/>
  <Rule attack="Electric" defend="Flying" multiplier="2"/>
  <Rule attack="Fire" defend="Ice" multiplier="2"/>
  <Status element="Fire" status="Burn" chance="0.1"/>
  <Status element="Electric" status="Paralysis" chance="0.3"/>
  <Status element="Electric" status="Confusion" chance="1"/>
</ElementChart>
)";

    bool hasVitalityEvent( const vector<VitalityEvent>& listEvent, VitalityEventType type )
    {
        for ( const VitalityEvent& event : listEvent )
        {
            if ( event._type == type )
                return true;
        }
        return false;
    }
} // namespace

SW_TEST_CASE( CombatStateTest, ShieldTakesDamageFirstAndPoiseBreaksThenRecovers )
{
    VitalitySettings settings;
    settings._maxHealth          = 100.0f;
    settings._maxShield          = 50.0f;
    settings._shieldRegenDelay   = 2.0f;
    settings._shieldRegenRate    = 10.0f;
    settings._poiseMax           = 30.0f;
    settings._poiseRegenDelay    = 1.0f;
    settings._poiseRegenRate     = 10.0f;
    settings._poiseBreakDuration = 0.5f;
    Vitality vitality( settings );

    // 실드가 먼저 — 30 은 모두 실드, 다음 40 은 실드 20 + 체력 20.
    VitalityDamageResult result = vitality.applyDamage( 30.0f, 10.0f, 7 );
    SW_EXPECT_NEAR_EQUAL( 30.0f, result._shieldAbsorbed, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, result._healthDamage, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 100.0f, vitality.getHealth(), 1.0e-4f );
    result = vitality.applyDamage( 40.0f, 10.0f, 7 );
    SW_EXPECT_NEAR_EQUAL( 20.0f, result._shieldAbsorbed, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, result._healthDamage, 1.0e-4f );
    SW_EXPECT_TRUE( result._bShieldBroken == SW_TRUE );
    SW_EXPECT_TRUE( result._bPoiseBroken == SW_FALSE );
    SW_EXPECT_NEAR_EQUAL( 80.0f, vitality.getHealth(), 1.0e-4f );

    // 재생 지연 — 2 초 전에는 실드가 차지 않는다.
    vitality.update( 1.5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, vitality.getShield(), 1.0e-4f );
    vitality.update( 1.0f ); // 지난 시간 2.5 — 지연이 끝난 뒤 0.5 초만큼만 찬다
    SW_EXPECT_NEAR_EQUAL( 5.0f, vitality.getShield(), 1.0e-4f );

    // 경직 — 30 에서 10 까지 깎였다가 지연 1 초 뒤 1.5 초 회복으로 25, 25 로 붕괴. 붕괴 중의 경직 피해는 쌓이지 않는다.
    SW_EXPECT_NEAR_EQUAL( 25.0f, vitality.getPoise(), 1.0e-4f );
    result = vitality.applyDamage( 0.0f, 25.0f, 9 );
    SW_EXPECT_TRUE( result._bPoiseBroken == SW_TRUE );
    SW_EXPECT_TRUE( vitality.isPoiseBroken() );
    result = vitality.applyDamage( 0.0f, 25.0f, 9 );
    SW_EXPECT_TRUE( result._bPoiseBroken == SW_FALSE );
    vitality.update( 0.6f );
    SW_EXPECT_TRUE( vitality.isPoiseBroken() == false );
    SW_EXPECT_NEAR_EQUAL( 30.0f, vitality.getPoise(), 1.0e-4f );

    vector<VitalityEvent> listEvent;
    vitality.drainEvents( listEvent );
    SW_EXPECT_TRUE( hasVitalityEvent( listEvent, VitalityEventType::ShieldBroken ) );
    SW_EXPECT_TRUE( hasVitalityEvent( listEvent, VitalityEventType::PoiseBroken ) );
    SW_EXPECT_TRUE( hasVitalityEvent( listEvent, VitalityEventType::PoiseRecovered ) );
    listEvent.clear();
    vitality.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 0u, listEvent.size() );

    // 쓰지 않는 쪽은 꺼내지 않고 버린다 — 다음 꺼냄에 남지 않는다. 버릴 알림이 정말 있는지는 사본으로 먼저 본다.
    (void)vitality.applyDamage( 0.0f, 30.0f, 9 ); // 경직 30 을 다 깎는다 — PoiseBroken
    Vitality probe = vitality;
    probe.drainEvents( listEvent );
    SW_ASSERT_TRUE( hasVitalityEvent( listEvent, VitalityEventType::PoiseBroken ) );
    listEvent.clear();
    vitality.discardEvents();
    vitality.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 0u, listEvent.size() );

    // 무적 중의 피해는 무시된다.
    vitality.setInvulnerable( 1.0f );
    result = vitality.applyDamage( 50.0f, 0.0f, 1 );
    SW_EXPECT_TRUE( result._bIgnored == SW_TRUE );
    SW_EXPECT_NEAR_EQUAL( 80.0f, vitality.getHealth(), 1.0e-4f );
}

SW_TEST_CASE( CombatStateTest, DownedBleedsOutReviveInterruptsAndDownLimitKills )
{
    VitalitySettings settings;
    settings._maxHealth               = 100.0f;
    settings._bDownedEnabled          = SW_TRUE;
    settings._downedHealth            = 20.0f;
    settings._bleedoutRate            = 10.0f;
    settings._reviveTime              = 4.0f;
    settings._reviveHealthRatio       = 0.25f;
    settings._invulnerableAfterRevive = 1.0f;
    settings._maxDownCount            = 2;
    Vitality vitality( settings );

    // 치명타 → 기절(넘친 피해는 출혈 체력으로 넘어가지 않는다) → 2 초 출혈사.
    VitalityDamageResult result = vitality.applyDamage( 500.0f, 0.0f, 3 );
    SW_EXPECT_TRUE( result._bDowned == SW_TRUE );
    SW_EXPECT_TRUE( vitality.isDowned() );
    SW_EXPECT_NEAR_EQUAL( 20.0f, vitality.getDownedHealth(), 1.0e-4f );
    SW_EXPECT_TRUE( vitality.heal( 50.0f ) == 0.0f ); // 기절 중 치료는 듣지 않는다
    vitality.update( 1.0f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, vitality.getDownedHealth(), 1.0e-4f );
    vitality.update( 1.0f );
    SW_EXPECT_TRUE( vitality.isDead() );
    vector<VitalityEvent> listEvent;
    vitality.drainEvents( listEvent );
    SW_ASSERT_TRUE( listEvent.empty() == false );
    SW_EXPECT_TRUE( listEvent.back()._type == VitalityEventType::Died );
    SW_EXPECT_EQUAL( 3, listEvent.back()._instigatorId ); // 출혈사는 기절시킨 쪽에 귀속
    SW_EXPECT_TRUE( vitality.startRevive( 1 ) == false ); // 죽은 개체는 살리지 못한다

    // 리스폰(기절 횟수 1 은 남는다) — 두 번째 기절, 부활 중 맞으면 끊기고 진행은 처음부터.
    vitality.respawn();
    SW_EXPECT_EQUAL( 1, vitality.getDownCount() );
    (void)vitality.applyDamage( 100.0f );
    SW_ASSERT_TRUE( vitality.isDowned() );
    SW_EXPECT_TRUE( vitality.startRevive( 5, 2.0f ) ); // 둘이 살린다 — 2 배
    vitality.update( 1.0f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, vitality.getReviveProgress(), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, vitality.getDownedHealth(), 1.0e-4f ); // 살리는 동안 출혈이 멈춘다
    (void)vitality.applyDamage( 5.0f, 0.0f, 3 );
    SW_EXPECT_TRUE( vitality.isReviving() == false );
    SW_EXPECT_NEAR_EQUAL( 0.0f, vitality.getReviveProgress(), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 15.0f, vitality.getDownedHealth(), 1.0e-4f );
    SW_EXPECT_TRUE( vitality.startRevive( 5, 1.0f ) );
    vitality.update( 4.0f );
    SW_ASSERT_TRUE( vitality.isAlive() );
    SW_EXPECT_NEAR_EQUAL( 25.0f, vitality.getHealth(), 1.0e-4f );
    SW_EXPECT_TRUE( vitality.isInvulnerable() );
    listEvent.clear();
    vitality.drainEvents( listEvent );
    SW_EXPECT_TRUE( hasVitalityEvent( listEvent, VitalityEventType::ReviveInterrupted ) );
    SW_EXPECT_TRUE( hasVitalityEvent( listEvent, VitalityEventType::Revived ) );

    // 기절 횟수 2 를 다 썼다 — 세 번째 치명타는 바로 죽음.
    vitality.update( 1.5f );
    result = vitality.applyDamage( 30.0f, 0.0f, 8 );
    SW_EXPECT_TRUE( result._bDied == SW_TRUE );
    SW_EXPECT_TRUE( result._bDowned == SW_FALSE );
    SW_EXPECT_TRUE( vitality.isDead() );

    // 끊기지 않는 설정 · 진행을 남기는 설정(DBD 치료).
    settings._bDamageInterruptsRevive = SW_FALSE;
    settings._bKeepReviveProgress     = SW_TRUE;
    settings._maxDownCount            = 0;
    vitality.initialize( settings );
    (void)vitality.applyDamage( 100.0f );
    SW_EXPECT_TRUE( vitality.startRevive( 2 ) );
    vitality.update( 1.0f );
    (void)vitality.applyDamage( 5.0f );
    SW_EXPECT_TRUE( vitality.isReviving() );
    vitality.stopRevive();
    SW_EXPECT_NEAR_EQUAL( 0.25f, vitality.getReviveProgress(), 1.0e-4f );
}

SW_TEST_CASE( CombatStateTest, GaugeExhaustionAndOverheatLockUntilRecovered )
{
    // 젤다 스태미나 — 0 까지 쓰면 탈진, 30 까지 차야 다시 쓴다.
    ResourceGaugeSettings stamina;
    stamina._max              = 100.0f;
    stamina._regenRate        = 20.0f;
    stamina._regenDelay       = 1.0f;
    stamina._exhaustThreshold = 30.0f;
    ResourceGauge gauge( stamina );
    SW_EXPECT_TRUE( gauge.trySpend( 40.0f ) );
    SW_EXPECT_TRUE( gauge.trySpend( 70.0f ) == false ); // 모자라면 쓰지 않는다
    SW_EXPECT_NEAR_EQUAL( 60.0f, gauge.getValue(), 1.0e-4f );
    SW_EXPECT_TRUE( gauge.drain( 50.0f, 1.0f ) );
    SW_EXPECT_TRUE( gauge.drain( 50.0f, 1.0f ) == false ); // 이번 프레임에 바닥
    SW_EXPECT_TRUE( gauge.isExhausted() );
    SW_EXPECT_NEAR_EQUAL( 0.0f, gauge.getValue(), 1.0e-4f );
    gauge.update( 0.5f ); // 지연 1 초 전 — 그대로
    SW_EXPECT_NEAR_EQUAL( 0.0f, gauge.getValue(), 1.0e-4f );
    gauge.update( 1.0f ); // 지연이 끝난 뒤 0.5 초 → 10
    SW_EXPECT_NEAR_EQUAL( 10.0f, gauge.getValue(), 1.0e-4f );
    gauge.update( 0.5f ); // 20
    SW_EXPECT_TRUE( gauge.isExhausted() );
    SW_EXPECT_TRUE( gauge.trySpend( 5.0f ) == false ); // 남은 양이 있어도 탈진 중
    gauge.update( 0.5f );                              // 30 — 풀린다
    SW_EXPECT_TRUE( gauge.isExhausted() == false );
    SW_EXPECT_TRUE( gauge.trySpend( 5.0f ) );

    // 스태미나 그릇 — 최대치가 늘면 그만큼 함께 찬다.
    gauge.refill();
    gauge.setMaxBonus( 20.0f );
    SW_EXPECT_NEAR_EQUAL( 120.0f, gauge.getValue(), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, gauge.getRatio(), 1.0e-4f );

    // 탈진 문턱이 없으면 바닥나도 잠기지 않는다(조금 차면 바로 쓸 수 있다).
    stamina._exhaustThreshold = 0.0f;
    gauge.initialize( stamina );
    SW_EXPECT_TRUE( gauge.trySpend( 100.0f ) );
    SW_EXPECT_TRUE( gauge.isExhausted() == false );
    gauge.update( 1.25f );
    SW_EXPECT_TRUE( gauge.trySpend( 5.0f ) );

    // 과열 — 쓰면 오르고 100 에 닿으면 과열, 벌칙 0.5 초 뒤 식어 20 이하가 되어야 풀린다.
    ResourceGaugeSettings heat;
    heat._max                  = 100.0f;
    heat._regenRate            = 40.0f;
    heat._regenDelay           = 0.0f;
    heat._bOverheatMode        = SW_TRUE;
    heat._overheatCooldown     = 0.5f;
    heat._overheatRecoverLevel = 20.0f;
    ResourceGauge boost( heat );
    SW_EXPECT_NEAR_EQUAL( 0.0f, boost.getValue(), 1.0e-4f );
    SW_EXPECT_TRUE( boost.trySpend( 60.0f ) );
    SW_EXPECT_TRUE( boost.trySpend( 50.0f ) == false ); // 최대를 넘는 한 번 쓰기는 실패(과열시키지도 않는다)
    SW_EXPECT_TRUE( boost.isOverheated() == false );
    SW_EXPECT_TRUE( boost.drain( 40.0f, 1.0f ) == false ); // 100 에 닿음 — 과열
    SW_EXPECT_TRUE( boost.isOverheated() );
    boost.update( 0.5f ); // 벌칙 — 식지 않는다
    SW_EXPECT_NEAR_EQUAL( 100.0f, boost.getValue(), 1.0e-4f );
    boost.update( 1.5f ); // 40
    SW_EXPECT_NEAR_EQUAL( 40.0f, boost.getValue(), 1.0e-4f );
    SW_EXPECT_TRUE( boost.isOverheated() );
    SW_EXPECT_TRUE( boost.trySpend( 1.0f ) == false );
    boost.update( 0.5f ); // 20 — 풀린다
    SW_EXPECT_TRUE( boost.isOverheated() == false );
    SW_EXPECT_TRUE( boost.trySpend( 10.0f ) );
}

SW_TEST_CASE( CombatStateTest, MoveTimelinePhasesCancelWindowsHitstopAndFrameAdvantage )
{
    MoveCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kCombatStateMoveXml, "CombatStateTest" ) );
    SW_EXPECT_EQUAL( 2u, catalog.getMoves().size() ); // id 없는 Move 는 버린다
    const MoveFrameData* pJab = catalog.findMove( hashed_string( "jab" ) );
    SW_ASSERT_NOT_NULL( pJab );
    SW_EXPECT_EQUAL( 26, pJab->getTotalFrames() ); // 9 + 2 + 15
    SW_EXPECT_TRUE( pJab->_height == AttackHeight::High );
    SW_EXPECT_EQUAL( 2u, pJab->_listHitbox.size() );
    SW_EXPECT_TRUE( pJab->_listHitbox[1]._shape == HitboxShape::Capsule );
    // 표의 숫자 — 첫 지속 프레임(10)에 닿았을 때: 막히면 12 − (1 + 15) = −4, 맞으면 20 − 16 = +4.
    SW_EXPECT_EQUAL( -4, pJab->computeFrameAdvantage( true ) );
    SW_EXPECT_EQUAL( 4, pJab->computeFrameAdvantage( false ) );

    MoveTimeline timeline;
    SW_EXPECT_TRUE( timeline.getPhase() == MovePhase::Idle );
    timeline.start( *pJab );
    vector<MoveHitbox> listHitbox;
    SW_EXPECT_TRUE( timeline.getPhase() == MovePhase::Startup );
    SW_EXPECT_EQUAL( 0u, timeline.collectActiveHitboxes( listHitbox ) );
    while ( timeline.getFrame() < 10 )
        (void)timeline.advanceFrame();
    SW_EXPECT_TRUE( timeline.getPhase() == MovePhase::Active );
    SW_EXPECT_EQUAL( 1u, timeline.collectActiveHitboxes( listHitbox ) );
    // 닿은 프레임이 10 이면 표의 숫자와 같다.
    SW_EXPECT_EQUAL( -4, timeline.computeFrameAdvantage( true ) );

    (void)timeline.advanceFrame(); // 11 — 박스 둘
    SW_EXPECT_EQUAL( 2u, timeline.collectActiveHitboxes( listHitbox ) );
    // 늦게(11 프레임에) 닿으면 남은 지속이 없어 1 프레임 이득이 는다.
    SW_EXPECT_EQUAL( -3, timeline.computeFrameAdvantage( true ) );
    timeline.registerContact( true );
    SW_EXPECT_TRUE( timeline.hasContact() && timeline.wasBlocked() );

    // 히트스톱 3 — 프레임이 멈춘다.
    for ( int32 index = 0; index < 3; ++index )
        SW_EXPECT_TRUE( timeline.advanceFrame() == false );
    SW_EXPECT_EQUAL( 11, timeline.getFrame() );
    SW_EXPECT_TRUE( timeline.advanceFrame() );
    SW_EXPECT_EQUAL( 12, timeline.getFrame() );
    SW_EXPECT_TRUE( timeline.getPhase() == MovePhase::Recovery );
    SW_EXPECT_EQUAL( 0u, timeline.collectActiveHitboxes( listHitbox ) );

    // 캔슬 창 — 12..20 은 닿았을 때만 jab2/launcher, 18..26 은 backdash.
    SW_EXPECT_TRUE( timeline.canCancelInto( hashed_string( "launcher" ) ) );
    SW_EXPECT_TRUE( timeline.canCancelInto( hashed_string( "backdash" ) ) == false );
    SW_EXPECT_TRUE( timeline.canCancelInto( hashed_string( "sweep" ) ) == false );
    while ( timeline.getFrame() < 21 )
        (void)timeline.advanceFrame();
    SW_EXPECT_TRUE( timeline.canCancelInto( hashed_string( "jab2" ) ) == false );
    SW_EXPECT_TRUE( timeline.canCancelInto( hashed_string( "backdash" ) ) );

    // 헛치면 onHit 창은 열리지 않는다.
    timeline.start( *pJab );
    while ( timeline.getFrame() < 12 )
        (void)timeline.advanceFrame();
    SW_EXPECT_TRUE( timeline.canCancelInto( hashed_string( "jab2" ) ) == false );

    // 끝 — 26 프레임째가 마지막 후딜, 그다음은 Finished.
    while ( timeline.getFrame() < 26 )
        (void)timeline.advanceFrame();
    SW_EXPECT_TRUE( timeline.getPhase() == MovePhase::Recovery );
    SW_EXPECT_TRUE( timeline.advanceFrame() );
    SW_EXPECT_TRUE( timeline.getPhase() == MovePhase::Finished );
    SW_EXPECT_TRUE( timeline.isPlaying() == false );
    SW_EXPECT_TRUE( timeline.advanceFrame() == false );

    // 높이 대 가드(철권 규칙).
    SW_EXPECT_TRUE( MoveTimeline::isBlocked( AttackHeight::High, GuardStance::Standing ) );
    SW_EXPECT_TRUE( MoveTimeline::isBlocked( AttackHeight::Mid, GuardStance::Standing ) );
    SW_EXPECT_TRUE( MoveTimeline::isBlocked( AttackHeight::Low, GuardStance::Standing ) == false );
    SW_EXPECT_TRUE( MoveTimeline::isBlocked( AttackHeight::Low, GuardStance::Crouching ) );
    SW_EXPECT_TRUE( MoveTimeline::isBlocked( AttackHeight::Throw, GuardStance::Standing ) == false );
    SW_EXPECT_TRUE( MoveTimeline::computeGuardOutcome( AttackHeight::High, GuardStance::Crouching ) == GuardOutcome::Evaded );
    SW_EXPECT_TRUE( MoveTimeline::computeGuardOutcome( AttackHeight::Throw, GuardStance::Crouching ) == GuardOutcome::Evaded );
    SW_EXPECT_TRUE( MoveTimeline::computeGuardOutcome( AttackHeight::Mid, GuardStance::Crouching ) == GuardOutcome::Hit );
    SW_EXPECT_TRUE( MoveTimeline::computeGuardOutcome( AttackHeight::Mid, GuardStance::None ) == GuardOutcome::Hit );
    SW_EXPECT_TRUE( MoveTimeline::computeGuardOutcome( AttackHeight::Mid, GuardStance::Standing, true ) == GuardOutcome::Hit );
    SW_EXPECT_TRUE( MoveTimeline::computeGuardOutcome( AttackHeight::High, GuardStance::Crouching, true ) == GuardOutcome::Evaded );
}

SW_TEST_CASE( CombatStateTest, ElementChartMultipliesDualTypesHandlesImmunityAndRollsDeterministically )
{
    ElementChart chart;
    SW_ASSERT_TRUE( chart.loadFromXmlText( kCombatStateElementXml, "CombatStateTest" ) );
    SW_EXPECT_EQUAL( 6u, chart.getElements().size() );
    const hashed_string water( "Water" );
    const hashed_string electric( "Electric" );
    SW_EXPECT_NEAR_EQUAL( 2.0f, chart.getMultiplier( water, hashed_string( "Fire" ) ), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, chart.getMultiplier( water, water ), 1.0e-4f );                                    // 규칙 없음 = 1
    SW_EXPECT_NEAR_EQUAL( 1.0f, chart.getMultiplier( hashed_string( "Fire" ), hashed_string( "Ice" ) ), 1.0e-4f ); // 선언 안 된 속성의 규칙은 버렸다

    // 복합 타입은 곱 — 물 → 땅/풀 = 2 × 0.5 = 1, 물 → 불/땅 = 4.
    SW_EXPECT_NEAR_EQUAL( 1.0f, chart.computeMultiplier( water, { hashed_string( "Ground" ), hashed_string( "Grass" ) } ), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, chart.computeMultiplier( water, { hashed_string( "Fire" ), hashed_string( "Ground" ) } ), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, chart.computeMultiplier( water, {} ), 1.0e-4f );
    // 면역은 다른 타입의 약점보다 강하다 — 전기 → 비행/땅 = 2 × 0 = 0.
    SW_EXPECT_TRUE( chart.isImmune( electric, { hashed_string( "Flying" ), hashed_string( "Ground" ) } ) );
    SW_EXPECT_TRUE( chart.isImmune( electric, { hashed_string( "Flying" ) } ) == false );

    // 상태이상 — 씨앗이 같으면 같은 결과, 확률은 적은 순서대로 굴린다.
    GameRandom firstRandom( 1234u );
    GameRandom secondRandom( 1234u );
    int32      burnCount = 0;
    for ( int32 index = 0; index < 1000; ++index )
    {
        const hashed_string first  = chart.rollStatus( hashed_string( "Fire" ), firstRandom );
        const hashed_string second = chart.rollStatus( hashed_string( "Fire" ), secondRandom );
        SW_EXPECT_TRUE( first == second );
        if ( first == hashed_string( "Burn" ) )
            ++burnCount;
    }
    SW_EXPECT_TRUE( burnCount > 50 && burnCount < 150 ); // 10 % 근처
    SW_EXPECT_TRUE( chart.rollStatus( water, firstRandom ).empty() );
    // 전기 — 마비 30 %, 아니면 혼란 100 %: 늘 둘 중 하나.
    int32 paralysisCount = 0;
    for ( int32 index = 0; index < 1000; ++index )
    {
        const hashed_string status = chart.rollStatus( electric, firstRandom );
        SW_EXPECT_TRUE( status == hashed_string( "Paralysis" ) || status == hashed_string( "Confusion" ) );
        if ( status == hashed_string( "Paralysis" ) )
            ++paralysisCount;
    }
    SW_EXPECT_TRUE( paralysisCount > 230 && paralysisCount < 370 );

    // 선언된 속성이 없으면 실패.
    ElementChart emptyChart;
    SW_EXPECT_TRUE( emptyChart.loadFromXmlText( "<ElementChart><Rule attack=\"A\" defend=\"B\" multiplier=\"2\"/></ElementChart>", "CombatStateTest" ) == false );
}

SW_TEST_CASE( CombatStateTest, GaugeRegenScaleReduceAndDrainAtEmptyKeepDelay )
{
    ResourceGaugeSettings settings;
    settings._max            = 100.0f;
    settings._regenRate      = 10.0f;
    settings._regenDelay     = 1.0f;
    settings._drainPerSecond = 50.0f;
    ResourceGauge gauge( settings );

    // 0 에 붙은 채 계속 깎는 중이면 회복 지연이 끝나지 않는다.
    SW_EXPECT_NEAR_EQUAL( gauge.reduce( 100.0f ), 100.0f, 0.001f );
    for ( int32 frame = 0; frame < 30; ++frame )
    {
        SW_EXPECT_FALSE( gauge.drain( 0.1f ) );
        gauge.update( 0.1f );
    }
    SW_EXPECT_NEAR_EQUAL( gauge.getValue(), 0.0f, 0.001f );
    // 멈추면 마지막으로 쓴 뒤 1 초부터 초당 10 — 마지막 걸음에서 이미 0.1 초가 지났으니 2 초 뒤에는 1.1 초 몫.
    gauge.update( 1.0f );
    gauge.update( 1.0f );
    SW_EXPECT_NEAR_EQUAL( gauge.getValue(), 11.0f, 0.01f );

    // 회복 배율은 회복량에만 — 지연은 그대로.
    ResourceGauge scaled( settings );
    (void)scaled.reduce( 50.0f );
    scaled.setRegenScale( 0.5f );
    scaled.update( 0.9f );
    SW_EXPECT_NEAR_EQUAL( scaled.getValue(), 50.0f, 0.001f );
    scaled.update( 1.1f );
    SW_EXPECT_NEAR_EQUAL( scaled.getValue(), 55.0f, 0.01f );
    scaled.initialize( settings );
    SW_EXPECT_NEAR_EQUAL( scaled.getRegenScale(), 1.0f, 0.001f );

    // 과열형의 reduce 는 열을 올리고 최대면 과열.
    ResourceGaugeSettings heat = settings;
    heat._bOverheatMode        = SW_TRUE;
    ResourceGauge boost( heat );
    SW_EXPECT_NEAR_EQUAL( boost.reduce( 60.0f ), 60.0f, 0.001f );
    SW_EXPECT_NEAR_EQUAL( boost.reduce( 60.0f ), 40.0f, 0.001f );
    SW_EXPECT_TRUE( boost.isOverheated() );
}

SW_TEST_CASE( CombatStateTest, MaxHealthChangesAndTimelineRestores )
{
    VitalitySettings settings;
    settings._maxHealth = 12.0f;
    Vitality vitality( settings );
    (void)vitality.applyDamage( 5.0f );
    vector<VitalityEvent> listEvent;
    vitality.drainEvents( listEvent );
    vitality.setMaxHealth( 16.0f, true ); // 하트 그릇 — 가득
    SW_EXPECT_NEAR_EQUAL( vitality.getHealth(), 16.0f, 0.001f );
    vitality.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( listEvent.size() ) ); // 붙이기 — 앞의 Damaged 가 남는다
    SW_EXPECT_TRUE( listEvent[1]._type == VitalityEventType::Healed );
    vitality.setMaxHealth( 10.0f, false );
    SW_EXPECT_NEAR_EQUAL( vitality.getHealth(), 10.0f, 0.001f );
    vitality.setMaxHealth( 20.0f, false );
    SW_EXPECT_NEAR_EQUAL( vitality.getHealth(), 10.0f, 0.001f );

    MoveFrameData move;
    move._id       = hashed_string( "jab" );
    move._startup  = 5;
    move._active   = 2;
    move._recovery = 6;
    MoveTimeline timeline;
    SW_EXPECT_FALSE( timeline.restoreState( move, 0, 0, false, false ) );
    SW_EXPECT_FALSE( timeline.restoreState( move, move.getTotalFrames() + 1, 0, false, false ) );
    SW_EXPECT_FALSE( timeline.isPlaying() );
    SW_ASSERT_TRUE( timeline.restoreState( move, 6, 3, true, true ) );
    SW_EXPECT_TRUE( timeline.isPlaying() && timeline.hasContact() && timeline.wasBlocked() );
    SW_EXPECT_EQUAL( 6, timeline.getFrame() );
    SW_EXPECT_EQUAL( 3, timeline.getHitstopRemaining() );
    SW_EXPECT_TRUE( timeline.getPhase() == MovePhase::Active );

    // 같은 상태까지 걸어간 타임라인과 같다.
    MoveTimeline walked;
    walked.start( move );
    walked.registerContact( true );
    while ( walked.isInHitstop() )
        (void)walked.advanceFrame();
    for ( int32 step = 1; step < 6; ++step )
        (void)walked.advanceFrame();
    walked.applyHitstop( 3 );
    SW_EXPECT_EQUAL( walked.getFrame(), timeline.getFrame() );
    SW_EXPECT_EQUAL( walked.getHitstopRemaining(), timeline.getHitstopRemaining() );
}
