#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Catalog/ActionPlatformerCatalog.h"
#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Rule/ActionEnemyBrain.h"

// ActionEnemyBrainTest — 적 패턴 상태 기계: 들어선 프레임은 넘어가지 않음, 시간 · 가까움 · 맞음 전이와 그 경계, 한 번만 쏘기, 시작 상태 없음 거절(폴백 결함), 상태 왕복과 깨진 바이트. 키트 GF_ActionPlatformer(nogpu).

namespace
{
    constexpr const utf8* kBrainXML = R"(<ActionPlatformer>
  <Grading><Grade id="S" min="90"/></Grading>
  <Pattern id="gunner" start="patrol">
    <State id="patrol" frames="3" next="shoot" onNear="aim" near="5" moveX="1" onHit="stagger"/>
    <State id="aim" frames="2" next="shoot"/>
    <State id="shoot" frames="1" next="patrol" fire="true" fireSpeed="9" attack="true"/>
    <State id="stagger" frames="2" next="patrol"/>
  </Pattern>
  <Pattern id="guard" start="watch">
    <State id="watch" frames="2" next="rest" onNear="watch" near="1"/>
    <State id="rest" frames="1"/>
  </Pattern>
</ActionPlatformer>)";

    const sw::hashed_string kPatrol{ "patrol" };
    const sw::hashed_string kAim{ "aim" };
    const sw::hashed_string kShoot{ "shoot" };
    const sw::hashed_string kStagger{ "stagger" };
    const sw::hashed_string kWatch{ "watch" };
    const sw::hashed_string kRest{ "rest" };

    constexpr float32 kBrainFar       = 100.0f;
    constexpr float32 kBrainTolerance = 1.0e-5f;

    struct ActionEnemyBrainTestUtil
    {
        static void writeBrainBytes( sw::Archive& outArchive, const sw::hashed_string& patternID, const sw::hashed_string& stateID, int32 frame, int32 facing, uint8 bEntered )
        {
            sw::StateIO::writeName( outArchive, patternID );
            sw::StateIO::writeName( outArchive, stateID );
            outArchive << frame;
            outArchive << facing;
            outArchive << bEntered;
        }
    };
} // namespace

/**
 * @brief [ActionEnemyBrainTest] 순찰은 들어선 프레임을 포함해 `frames` 만큼 보인 뒤 사격으로 가고, 쏘는 것은 들어선 한 프레임뿐이며, 들어선 프레임에는 바로 넘어가지 않는다
 */
SW_TEST_CASE( ActionEnemyBrainTest, TimeAdvancesAndFiresOnEnteringOnly )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kBrainXML, "ActionEnemyBrainTest" ) );
    sw::ActionEnemyBrain brain;
    SW_REQUIRE_TRUE( brain.initialize( catalog.findPattern( sw::hashed_string( "gunner" ) ) ) );
    SW_EXPECT_EQUAL( kPatrol, brain.getStateID() );

    sw::ActionEnemyAction action = brain.advanceFrame( kBrainFar ); // 들어선 프레임
    SW_EXPECT_EQUAL( kPatrol, brain.getStateID() );
    SW_EXPECT_EQUAL( 0, brain.getStateFrame() );
    SW_EXPECT_TRUE( action._bStateChanged == SW_TRUE );
    SW_EXPECT_NEAR( 1.0f, action._moveX, kBrainTolerance );

    action = brain.advanceFrame( kBrainFar );
    SW_EXPECT_TRUE( action._bStateChanged == SW_FALSE );
    SW_EXPECT_EQUAL( 1, brain.getStateFrame() );
    action = brain.advanceFrame( kBrainFar );
    SW_EXPECT_EQUAL( 2, brain.getStateFrame() );
    SW_EXPECT_EQUAL( kPatrol, brain.getStateID() );

    action = brain.advanceFrame( kBrainFar ); // 사격에 들어선 프레임
    SW_EXPECT_EQUAL( kShoot, brain.getStateID() );
    SW_EXPECT_TRUE( action._bStateChanged == SW_TRUE );
    SW_EXPECT_TRUE( action._bFire == SW_TRUE );
    SW_EXPECT_TRUE( action._bAttack == SW_TRUE );
    SW_EXPECT_NEAR( 9.0f, action._fireSpeed, kBrainTolerance );
    SW_EXPECT_NEAR( 0.0f, action._moveX, kBrainTolerance );

    action = brain.advanceFrame( kBrainFar ); // frames=1 이라 바로 돌아간다
    SW_EXPECT_EQUAL( kPatrol, brain.getStateID() );
    SW_EXPECT_TRUE( action._bFire == SW_FALSE );
    SW_EXPECT_TRUE( action._bAttack == SW_FALSE );
}

/**
 * @brief [ActionEnemyBrainTest] 가까움은 거리가 정확히 범위와 같을 때 켜지고 시간보다 먼저 보며, 들어선 프레임에는 가까워도 넘어가지 않는다
 */
SW_TEST_CASE( ActionEnemyBrainTest, NearRangeIsInclusiveAndBeatsTime )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kBrainXML, "ActionEnemyBrainTest" ) );
    sw::ActionEnemyBrain brain;
    SW_REQUIRE_TRUE( brain.initialize( catalog.findPattern( sw::hashed_string( "gunner" ) ) ) );

    (void)brain.advanceFrame( 1.0f ); // 들어선 프레임
    SW_EXPECT_EQUAL( kPatrol, brain.getStateID() );
    (void)brain.advanceFrame( 5.01f );
    SW_EXPECT_EQUAL( kPatrol, brain.getStateID() );
    (void)brain.advanceFrame( kBrainFar );
    SW_EXPECT_EQUAL( 2, brain.getStateFrame() );
    const sw::ActionEnemyAction action = brain.advanceFrame( 5.0f ); // 시간도 다 된 프레임 — 가까움이 이긴다
    SW_EXPECT_EQUAL( kAim, brain.getStateID() );
    SW_EXPECT_TRUE( action._bStateChanged == SW_TRUE );
}

/**
 * @brief [ActionEnemyBrainTest] 자기 자신으로 가는 가까움은 다시 들어서지 않고 시간만 다시 세고, 멀어지면 그때부터 센다. 다음 상태가 없으면 시간이 다 돼도 머문다
 */
SW_TEST_CASE( ActionEnemyBrainTest, SelfTransitionRestartsTheClockOnly )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kBrainXML, "ActionEnemyBrainTest" ) );
    sw::ActionEnemyBrain brain;
    SW_REQUIRE_TRUE( brain.initialize( catalog.findPattern( sw::hashed_string( "guard" ) ) ) );
    (void)brain.advanceFrame( kBrainFar );
    for ( int32 frame = 0; frame < 5; ++frame )
    {
        const sw::ActionEnemyAction action = brain.advanceFrame( 1.0f );
        SW_EXPECT_EQUAL( kWatch, brain.getStateID() );
        SW_EXPECT_EQUAL( 0, brain.getStateFrame() );
        SW_EXPECT_TRUE( action._bStateChanged == SW_FALSE );
    }
    (void)brain.advanceFrame( kBrainFar );
    SW_EXPECT_EQUAL( 1, brain.getStateFrame() );
    (void)brain.advanceFrame( kBrainFar );
    SW_EXPECT_EQUAL( kRest, brain.getStateID() );
    for ( int32 frame = 0; frame < 5; ++frame )
    {
        (void)brain.advanceFrame( 0.0f );
        SW_EXPECT_EQUAL( kRest, brain.getStateID() );
    }
    SW_EXPECT_EQUAL( 5, brain.getStateFrame() ); // 다음 상태가 없어도 머무는 동안 프레임을 센다
}

/**
 * @brief [ActionEnemyBrainTest] 맞으면 `onHit` 이 있는 상태에서만 바로 경직으로 들어서고, 다음 프레임은 그 들어선 프레임이다
 */
SW_TEST_CASE( ActionEnemyBrainTest, HitGoesToStaggerOnlyWhenTheStateHasOne )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kBrainXML, "ActionEnemyBrainTest" ) );
    sw::ActionEnemyBrain brain;
    SW_EXPECT_FALSE( brain.notifyHit() ); // 패턴 없음
    SW_REQUIRE_TRUE( brain.initialize( catalog.findPattern( sw::hashed_string( "gunner" ) ) ) );
    (void)brain.advanceFrame( kBrainFar );

    SW_EXPECT_TRUE( brain.notifyHit() );
    SW_EXPECT_EQUAL( kStagger, brain.getStateID() );
    SW_EXPECT_EQUAL( 0, brain.getStateFrame() );
    const sw::ActionEnemyAction action = brain.advanceFrame( 0.0f );
    SW_EXPECT_TRUE( action._bStateChanged == SW_TRUE );
    SW_EXPECT_FALSE( brain.notifyHit() ); // 경직에는 onHit 이 없다
    SW_EXPECT_EQUAL( kStagger, brain.getStateID() );
}

/**
 * @brief [ActionEnemyBrainTest] 바라보는 쪽은 부호만 받아 이동에 곱한다(0 은 오른쪽)
 */
SW_TEST_CASE( ActionEnemyBrainTest, FacingKeepsOnlyTheSign )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kBrainXML, "ActionEnemyBrainTest" ) );
    sw::ActionEnemyBrain brain;
    SW_REQUIRE_TRUE( brain.initialize( catalog.findPattern( sw::hashed_string( "gunner" ) ), -7 ) );
    SW_EXPECT_EQUAL( -1, brain.getFacing() );
    SW_EXPECT_NEAR( -1.0f, brain.advanceFrame( kBrainFar )._moveX, kBrainTolerance );
    brain.setFacing( 0 );
    SW_EXPECT_EQUAL( 1, brain.getFacing() );
    SW_EXPECT_NEAR( 1.0f, brain.advanceFrame( kBrainFar )._moveX, kBrainTolerance );
}

/**
 * @brief [ActionEnemyBrainTest] 결함: 시작 상태가 목록에 없으면 첫 상태로 대신 시작했다. 패턴 없음 · 빈 상태 목록과 함께 거절하고, 앞 패턴의 진행도 남기지 않으며, 그 뒤 호출은 아무 일도 하지 않는다
 */
SW_TEST_CASE( ActionEnemyBrainTest, InitializeRejectsMissingStartWithoutFallback )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kBrainXML, "ActionEnemyBrainTest" ) );
    sw::ActionEnemyBrain brain;
    SW_REQUIRE_TRUE( brain.initialize( catalog.findPattern( sw::hashed_string( "gunner" ) ) ) );
    (void)brain.advanceFrame( kBrainFar );
    (void)brain.advanceFrame( kBrainFar );

    sw::ActionPatternDef wrongStart;
    wrongStart._id    = sw::hashed_string( "wrong" );
    wrongStart._start = sw::hashed_string( "ghost" );
    sw::ActionPatternStateDef state;
    state._id = sw::hashed_string( "only" );
    wrongStart._listState.push_back( state );

    SW_EXPECT_FALSE( brain.initialize( &wrongStart ) );
    SW_EXPECT_TRUE( brain.getStateID().empty() );
    SW_EXPECT_EQUAL( 0, brain.getStateFrame() );

    sw::ActionPatternDef noState;
    noState._start = sw::hashed_string( "only" );
    SW_EXPECT_FALSE( brain.initialize( &noState ) );
    SW_EXPECT_FALSE( brain.initialize( nullptr ) );

    const sw::ActionEnemyAction action = brain.advanceFrame( 0.0f );
    SW_EXPECT_TRUE( action._bFire == SW_FALSE );
    SW_EXPECT_TRUE( action._bStateChanged == SW_FALSE );
    SW_EXPECT_NEAR( 0.0f, action._moveX, kBrainTolerance );
    SW_EXPECT_FALSE( brain.notifyHit() );
}

/**
 * @brief [ActionEnemyBrainTest] 들어선 프레임 표시까지 왕복해, 읽은 두뇌가 원본과 같은 행동을 낸다(한 번만 쏘기를 다시 하거나 건너뛰지 않는다)
 */
SW_TEST_CASE( ActionEnemyBrainTest, StateRoundTripResumesIdentically )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kBrainXML, "ActionEnemyBrainTest" ) );
    sw::ActionEnemyBrain original;
    SW_REQUIRE_TRUE( original.initialize( catalog.findPattern( sw::hashed_string( "gunner" ) ), -1 ) );
    (void)original.advanceFrame( kBrainFar );
    SW_REQUIRE_TRUE( original.notifyHit() ); // 들어선 프레임이 남은 상태로 쓴다

    sw::Archive written;
    original.writeState( written );

    sw::ActionEnemyBrain noCatalog;
    sw::Archive          withoutCatalog( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( noCatalog.readState( withoutCatalog ) );

    sw::ActionEnemyBrain restored;
    restored.bindCatalog( &catalog );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( kStagger, restored.getStateID() );
    SW_EXPECT_EQUAL( -1, restored.getFacing() );

    for ( int32 frame = 0; frame < 10; ++frame )
    {
        const sw::ActionEnemyAction expected = original.advanceFrame( kBrainFar );
        const sw::ActionEnemyAction actual   = restored.advanceFrame( kBrainFar );
        SW_EXPECT_EQUAL( original.getStateID(), restored.getStateID() );
        SW_EXPECT_EQUAL( original.getStateFrame(), restored.getStateFrame() );
        SW_EXPECT_EQUAL( expected._bFire, actual._bFire );
        SW_EXPECT_EQUAL( expected._bStateChanged, actual._bStateChanged );
    }
}

/**
 * @brief [ActionEnemyBrainTest] 모르는 패턴 · 상태, 범위 밖 프레임 · 바라보는 쪽 · 표시, 잘린 바이트를 거절하고 지금 상태를 그대로 둔다
 */
SW_TEST_CASE( ActionEnemyBrainTest, ReadRejectsBadBytesAndKeepsState )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kBrainXML, "ActionEnemyBrainTest" ) );
    sw::ActionEnemyBrain target;
    target.bindCatalog( &catalog );
    SW_REQUIRE_TRUE( target.initialize( catalog.findPattern( sw::hashed_string( "gunner" ) ) ) );

    const sw::hashed_string gunner{ "gunner" };
    const sw::hashed_string ghost{ "ghost" };
    const auto              expectRejected = [&target]( const sw::hashed_string& patternID, const sw::hashed_string& stateID, int32 frame, int32 facing, uint8 bEntered )
    {
        sw::Archive written;
        ActionEnemyBrainTestUtil::writeBrainBytes( written, patternID, stateID, frame, facing, bEntered );
        sw::Archive reading( written.getData(), written.getSize() );
        SW_EXPECT_FALSE( target.readState( reading ) );
        SW_EXPECT_EQUAL( kPatrol, target.getStateID() );
        SW_EXPECT_EQUAL( 1, target.getFacing() );
    };
    expectRejected( ghost, kPatrol, 0, 1, SW_FALSE );
    expectRejected( gunner, ghost, 0, 1, SW_FALSE );
    expectRejected( sw::hashed_string{}, kPatrol, 0, 1, SW_FALSE ); // 패턴 없이 상태만 있다
    expectRejected( gunner, kPatrol, -1, 1, SW_FALSE );
    expectRejected( gunner, kPatrol, 0, 0, SW_FALSE );
    expectRejected( gunner, kPatrol, 0, 2, SW_FALSE );
    expectRejected( gunner, kPatrol, 0, 1, 2 );

    sw::Archive written;
    ActionEnemyBrainTestUtil::writeBrainBytes( written, gunner, kAim, 0, -1, SW_FALSE );
    sw::Archive truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( target.readState( truncated ) );
    SW_EXPECT_EQUAL( kPatrol, target.getStateID() );

    sw::Archive whole( written.getData(), written.getSize() );
    SW_EXPECT_TRUE( target.readState( whole ) );
    SW_EXPECT_EQUAL( kAim, target.getStateID() );
}
