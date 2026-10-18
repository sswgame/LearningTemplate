#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Actor/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleSaveState.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleSkeletalAnimator.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleSystem.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractionCatalog.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractionInterruptionSystem.h"
#include "GameFramework/Base/Gameplay/Interaction/SmartObjectComponent.h"
#include "GameFramework/Base/Gameplay/Interaction/SmartObjectScheduleLocator.h"

#include "TestFramework/TestFramework.h"

// NPC 일정과 스마트 오브젝트 · 상호작용 · 애니메이터의 연결 — 씬의 스마트 오브젝트 슬롯을 일정의 자리로 예약하고, 말 걸기가 일정에 끼어들며, 활동이 애니메이터 상태를 고른다.

using namespace sw;

namespace
{
    struct ScheduleIntegrationTestInternal
    {
        static constexpr const utf8* kXML = R"(
<Schedules>
  <Calendar days="Mon,Tue,Wed,Thu,Fri,Sat,Sun" seasons="Spring" weathers="sunny"/>
  <Place id="home" area="town" position="0 0 0"/>
  <Place id="square" area="town" position="10 0 0"/>
  <Interrupt id="Talk" priority="10" timeout="30"/>
  <Archetype id="villager"><Routine id="night"><Block start="0:00" end="8:00" activity="Sleep"/></Routine></Archetype>
  <Npc id="abigail" archetype="villager" home="home" speed="2">
    <Routine id="daily"><Block start="18:00" end="20:00" activity="UseObject" objectKind="Bench" place="square" animation="Sit"/></Routine>
  </Npc>
  <Npc id="emily" archetype="villager" home="home" speed="1">
    <Routine id="daily"><Block start="18:00" end="20:00" activity="UseObject" objectKind="Bench" place="square"/></Routine>
  </Npc>
</Schedules>
)";

        static int32 makeMinute( int32 hour, int32 minute = 0 ) { return hour * 60 + minute; }

        static ScheduleSystemSettings makeSettings()
        {
            ScheduleSystemSettings settings;
            settings._clock._listSeason    = { hashed_string( "Spring" ) };
            settings._clock._daysPerSeason = 28;
            settings._seed                 = 3u;
            return settings;
        }

        /** @brief 정의 id 가 @p pKindName 이고 슬롯이 둘(좌우 0.5 m)인 벤치 오브젝트를 @p x 에 둡니다. */
        static GameObject* createBench( GameObjectManager& manager, const utf8* pKindName, float32 x )
        {
            SmartObjectDef bench;
            bench._id = hashed_string( pKindName );
            for ( int32 slot = 0; slot < 2; ++slot )
            {
                SmartObjectSlotDef slotDef;
                slotDef._id     = hashed_string( slot == 0 ? "Left" : "Right" );
                slotDef._offset = float3{ slot == 0 ? -0.5f : 0.5f, 0.0f, 0.0f };
                bench._listSlot.push_back( slotDef );
            }
            GameObject* pBench = manager.createGameObject( hashed_string( "Bench" ) );
            pBench->addComponent<SceneComponent>()->setLocalPosition( float3{ x, 0.0f, 0.0f } );
            pBench->addComponent<SmartObjectComponent>()->setDefinition( bench );
            return pBench;
        }

        static ScheduleReserveRequest makeRequest( const utf8* pNpc, int32 startMinute, int32 endMinute, const float3& nearPosition )
        {
            ScheduleReserveRequest request;
            request._npc         = hashed_string( pNpc );
            request._objectKind  = hashed_string( "Bench" );
            request._day         = 0;
            request._startMinute = startMinute;
            request._endMinute   = endMinute;
            request._near        = nearPosition;
            return request;
        }
    };
} // namespace

/**
 * @brief [ScheduleIntegrationTest] 스마트 오브젝트 제공자 — 가까운 빈 슬롯을 예약하고(NPC 마다 다른 슬롯), 같은 요청은 같은 예약이며, 겹치지 않는 시간 창은 같은 슬롯을 다시 쓰고, 반납하면 비운다
 */
SW_TEST_CASE( ScheduleIntegrationTest, SmartObjectLocatorReservesSlotsIdempotently )
{
    using Internal = ScheduleIntegrationTestInternal;
    GameObjectManager manager;
    (void)Internal::createBench( manager, "Bench", 10.0f ); // 오브젝트만 필요하다(포인터는 안 쓴다). 슬롯 9.5 · 10.5
    (void)Internal::createBench( manager, "Bench", 30.0f ); // 오브젝트만 필요하다(포인터는 안 쓴다). 슬롯 29.5 · 30.5
    manager.flushSceneTransforms();

    SmartObjectScheduleLocator locator;
    locator.initialize( &manager );
    locator.setDefaultArea( hashed_string( "town" ) );

    ScheduleLocation location{};
    uint32           idA{ 0 };
    uint32           idB{ 0 };
    uint32           idC{ 0 };
    uint32           idD{ 0 };
    const float3     nearBench{ 11.0f, 0.0f, 0.0f };
    SW_ASSERT_TRUE( locator.reserve( Internal::makeRequest( "abigail", 1080, 1200, nearBench ), location, idA ) );
    SW_EXPECT_NEAR_EQUAL( 10.5f, location._position._x, 1.0e-3f ); // 가장 가까운 슬롯
    SW_EXPECT_TRUE( location._area == hashed_string( "town" ) );
    SW_EXPECT_TRUE( idA != 0 );

    uint32 idAgain{ 0 };
    SW_ASSERT_TRUE( locator.reserve( Internal::makeRequest( "abigail", 1100, 1150, nearBench ), location, idAgain ) ); // 겹치는 창 — 같은 예약
    SW_EXPECT_EQUAL( idA, idAgain );
    SW_EXPECT_EQUAL( size_t( 1 ), locator.getReservationCount() );

    SW_ASSERT_TRUE( locator.reserve( Internal::makeRequest( "emily", 1080, 1200, nearBench ), location, idB ) );
    SW_EXPECT_NEAR_EQUAL( 9.5f, location._position._x, 1.0e-3f ); // 같은 벤치의 남은 슬롯
    SW_ASSERT_TRUE( locator.reserve( Internal::makeRequest( "pierre", 1080, 1200, nearBench ), location, idC ) );
    SW_EXPECT_NEAR_EQUAL( 29.5f, location._position._x, 1.0e-3f ); // 첫 벤치가 찼으니 다음 벤치
    SW_ASSERT_TRUE( locator.reserve( Internal::makeRequest( "sam", 1080, 1200, nearBench ), location, idD ) );
    SW_EXPECT_NEAR_EQUAL( 30.5f, location._position._x, 1.0e-3f );
    uint32 idFull{ 0 };
    SW_EXPECT_FALSE( locator.reserve( Internal::makeRequest( "leah", 1080, 1200, nearBench ), location, idFull ) ); // 슬롯이 모두 찼다

    // 겹치지 않는 시간 창은 같은 슬롯을 다시 쓴다.
    uint32 idLater{ 0 };
    SW_ASSERT_TRUE( locator.reserve( Internal::makeRequest( "leah", 1260, 1320, nearBench ), location, idLater ) );
    SW_EXPECT_NEAR_EQUAL( 10.5f, location._position._x, 1.0e-3f );

    // 반납하면 그 슬롯이 비어 다음 요청이 가진다.
    locator.release( idA );
    uint32 idReused{ 0 };
    SW_ASSERT_TRUE( locator.reserve( Internal::makeRequest( "leah", 1080, 1200, nearBench ), location, idReused ) );
    SW_EXPECT_NEAR_EQUAL( 10.5f, location._position._x, 1.0e-3f );

    // 모르는 종류는 자리가 없다.
    ScheduleReserveRequest unknownKind = Internal::makeRequest( "abigail", 1080, 1200, nearBench );
    unknownKind._objectKind            = hashed_string( "Anvil" );
    SW_EXPECT_FALSE( locator.reserve( unknownKind, location, idFull ) );
}

/**
 * @brief [ScheduleIntegrationTest] 예약한 슬롯을 블록이 시작될 때 실제로 점유하고 끝나면 비운다 — 플레이어가 먼저 앉았으면 점유가 실패한다
 */
SW_TEST_CASE( ScheduleIntegrationTest, ClaimReservedOccupiesTheSmartObjectSlot )
{
    using Internal = ScheduleIntegrationTestInternal;
    GameObjectManager manager;
    GameObject*       pBench = Internal::createBench( manager, "Bench", 10.0f );
    manager.flushSceneTransforms();
    GameObject* pAbigail = manager.createGameObject( hashed_string( "abigail" ) );
    GameObject* pPlayer  = manager.createGameObject( hashed_string( "Player" ) );

    SmartObjectScheduleLocator locator;
    locator.initialize( &manager );
    ScheduleLocation location{};
    uint32           reservation{ 0 };
    SW_ASSERT_TRUE( locator.reserve( Internal::makeRequest( "abigail", 1080, 1200, float3{ 11.0f, 0.0f, 0.0f } ), location, reservation ) );

    SmartObjectComponent* pSmart = pBench->getComponent<SmartObjectComponent>();
    SW_ASSERT_NOT_NULL( pSmart );
    SW_EXPECT_EQUAL( 2, pSmart->countFreeSlots() ); // 예약만으로는 점유하지 않는다
    SW_EXPECT_TRUE( locator.claimReserved( reservation, *pAbigail ) );
    SW_EXPECT_EQUAL( 1, pSmart->countFreeSlots() );
    SW_EXPECT_EQUAL( 1, pSmart->findSlotOf( *pAbigail ) );             // 가까운 오른쪽 슬롯
    SW_EXPECT_TRUE( locator.claimReserved( reservation, *pAbigail ) ); // 멱등
    SW_EXPECT_TRUE( locator.releaseClaim( reservation, *pAbigail ) );
    SW_EXPECT_EQUAL( 2, pSmart->countFreeSlots() );
    SW_EXPECT_FALSE( locator.releaseClaim( reservation, *pAbigail ) );

    // 플레이어가 그 슬롯에 먼저 앉으면 NPC 의 점유는 실패한다 — 게임은 물러서는 활동을 고른다.
    SW_EXPECT_TRUE( pSmart->claimSlot( *pPlayer, 1 ) );
    SW_EXPECT_FALSE( locator.claimReserved( reservation, *pAbigail ) );
    SW_EXPECT_FALSE( locator.claimReserved( 999u, *pAbigail ) ); // 모르는 예약
}

/**
 * @brief [ScheduleIntegrationTest] 예약은 저장했다 되살려도 같은 슬롯이고, 사라진 오브젝트의 예약은 버린다
 */
SW_TEST_CASE( ScheduleIntegrationTest, LocatorStateRoundTripsAndDropsGoneObjects )
{
    using Internal = ScheduleIntegrationTestInternal;
    GameObjectManager manager;
    GameObject*       pFirst = Internal::createBench( manager, "Bench", 10.0f );
    (void)Internal::createBench( manager, "Bench", 30.0f ); // 오브젝트만 필요하다(포인터는 안 쓴다)
    manager.flushSceneTransforms();

    SmartObjectScheduleLocator locator;
    locator.initialize( &manager );
    ScheduleLocation location{};
    uint32           idA{ 0 };
    uint32           idB{ 0 };
    uint32           idC{ 0 };
    const float3     nearBench{ 11.0f, 0.0f, 0.0f };
    SW_ASSERT_TRUE( locator.reserve( Internal::makeRequest( "abigail", 1080, 1200, nearBench ), location, idA ) );
    SW_ASSERT_TRUE( locator.reserve( Internal::makeRequest( "emily", 1080, 1200, nearBench ), location, idB ) );
    SW_ASSERT_TRUE( locator.reserve( Internal::makeRequest( "pierre", 1080, 1200, nearBench ), location, idC ) );

    vector<ScheduleReservationSaveState> listState;
    locator.fillState( listState );
    SW_ASSERT_EQUAL( size_t( 3 ), listState.size() );

    SmartObjectScheduleLocator restored;
    restored.initialize( &manager );
    restored.restoreState( listState );
    SW_EXPECT_EQUAL( size_t( 3 ), restored.getReservationCount() );
    uint32 idAgain{ 0 };
    SW_ASSERT_TRUE( restored.reserve( Internal::makeRequest( "emily", 1080, 1200, nearBench ), location, idAgain ) );
    SW_EXPECT_EQUAL( idB, idAgain );
    SW_EXPECT_NEAR_EQUAL( 9.5f, location._position._x, 1.0e-3f );

    // 첫 벤치가 사라지면 그 예약 둘은 버려진다.
    manager.destroyObject( pFirst );
    manager.update( 1.0f / 60.0f );
    SmartObjectScheduleLocator afterDestroy;
    afterDestroy.initialize( &manager );
    afterDestroy.restoreState( listState );
    SW_EXPECT_EQUAL( size_t( 1 ), afterDestroy.getReservationCount() );
}

/**
 * @brief [ScheduleIntegrationTest] 일정 시스템이 스마트 오브젝트 제공자로 UseObject 자리를 받는다 — NPC 마다 다른 슬롯으로 가고, 활동 시작 신호의 예약 id 로 점유한다
 */
SW_TEST_CASE( ScheduleIntegrationTest, ScheduleSystemUsesTheSmartObjectLocator )
{
    using Internal = ScheduleIntegrationTestInternal;
    GameObjectManager manager;
    GameObject*       pBench = Internal::createBench( manager, "Bench", 10.0f );
    manager.flushSceneTransforms();
    SmartObjectScheduleLocator locator;
    locator.initialize( &manager );
    locator.setDefaultArea( hashed_string( "town" ) );

    ScheduleCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXMLText( Internal::kXML, "town" ) );
    ScheduleSystem system;
    system.setActivityLocator( &locator );
    SW_ASSERT_TRUE( system.initialize( &catalog, Internal::makeSettings(), Internal::makeMinute( 6 ) ) );
    const int32 abigail = system.findNpcIndex( "abigail" );
    const int32 emily   = system.findNpcIndex( "emily" );
    system.advanceTo( Internal::makeMinute( 19 ) );

    const float32 abigailX = system.getNpcView( abigail )._target._position._x;
    const float32 emilyX   = system.getNpcView( emily )._target._position._x;
    SW_EXPECT_TRUE( MathUtil::abs( abigailX - emilyX ) > 0.5f );
    const auto isBenchSlot = []( float32 x )
    { return MathUtil::abs( x - 9.5f ) < 1.0e-3f || MathUtil::abs( x - 10.5f ) < 1.0e-3f; };
    SW_EXPECT_TRUE( isBenchSlot( abigailX ) && isBenchSlot( emilyX ) );
    SW_EXPECT_EQUAL( size_t( 2 ), locator.getReservationCount() );
    SW_EXPECT_TRUE( system.getNpcView( abigail )._animation == hashed_string( "Sit" ) );

    // 하루가 지나면 일정이 지난 예약을 반납한다.
    system.advanceTo( Internal::makeMinute( 19 ) + kScheduleMinutesPerDay );
    SW_EXPECT_EQUAL( size_t( 2 ), locator.getReservationCount() );                        // 어제 것을 돌려주고 오늘 것을 새로 잡았다
    SW_EXPECT_EQUAL( 2, pBench->getComponent<SmartObjectComponent>()->countFreeSlots() ); // 점유는 게임이 한다 — 일정은 예약만
}

/**
 * @brief [ScheduleIntegrationTest] 말 걸기가 일정에 끼어든다 — 상호작용 중인 대상 NPC 에 Talk 를 쌓고, 끝나면 빼며, 같은 대상 여럿은 한 번이고, NPC 가 아닌 오브젝트는 무시한다
 */
SW_TEST_CASE( ScheduleIntegrationTest, InteractionInterruptionSystemPushesAndPopsTalk )
{
    using Internal = ScheduleIntegrationTestInternal;
    ScheduleCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXMLText( Internal::kXML, "town" ) );
    ScheduleSystem system;
    SW_ASSERT_TRUE( system.initialize( &catalog, Internal::makeSettings(), Internal::makeMinute( 6 ) ) );
    const int32 abigail = system.findNpcIndex( "abigail" );
    system.advanceTo( Internal::makeMinute( 18, 30 ) );

    GameObjectManager manager;
    GameObject*       pAbigail = manager.createGameObject( hashed_string( "abigail" ) );
    GameObject*       pCrate   = manager.createGameObject( hashed_string( "Crate" ) ); // NPC 가 아니다

    InteractionInterruptionSystem interruption;
    interruption.initialize( &system );

    interruption.applyTargets( { pAbigail, pCrate, pAbigail } );
    SW_EXPECT_EQUAL( size_t( 1 ), interruption.getActiveCount() );
    SW_EXPECT_TRUE( system.getNpcView( abigail )._phase == ScheduleNpcPhase::Interrupted );
    SW_EXPECT_TRUE( system.getNpcView( abigail )._interruption == hashed_string( "Talk" ) );

    // 말이 timeout(30 분)보다 길어도 매 프레임 다시 쌓아 만료가 밀리므로 끼어든 채로 있다.
    for ( int32 minute = 0; minute < 6; ++minute )
    {
        system.advanceTo( Internal::makeMinute( 18, 30 + minute * 10 ) );
        interruption.applyTargets( { pAbigail } );
    }
    SW_EXPECT_TRUE( system.getNpcView( abigail )._phase == ScheduleNpcPhase::Interrupted );

    // 말이 끝나면 뺀다 — 일정은 지금 시각의 칸으로 돌아간다.
    interruption.applyTargets( {} );
    SW_EXPECT_EQUAL( size_t( 0 ), interruption.getActiveCount() );
    SW_EXPECT_TRUE( system.getNpcView( abigail )._phase != ScheduleNpcPhase::Interrupted );
}

/**
 * @brief [ScheduleIntegrationTest] 사용자 정의 해석 함수로 오브젝트 → NPC 를 이을 수 있고, clear 는 쌓은 끼어들기를 모두 뺀다
 */
SW_TEST_CASE( ScheduleIntegrationTest, InterruptionResolverAndClear )
{
    using Internal = ScheduleIntegrationTestInternal;
    ScheduleCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXMLText( Internal::kXML, "town" ) );
    ScheduleSystem system;
    SW_ASSERT_TRUE( system.initialize( &catalog, Internal::makeSettings(), Internal::makeMinute( 6 ) ) );
    const int32 emily = system.findNpcIndex( "emily" );
    system.advanceTo( Internal::makeMinute( 18, 30 ) );

    GameObjectManager manager;
    GameObject*       pClerk = manager.createGameObject( hashed_string( "ShopClerk" ) ); // 이름이 NPC id 가 아니다

    InteractionInterruptionSystem interruption;
    interruption.initialize( &system );
    interruption.applyTargets( { pClerk } );
    SW_EXPECT_EQUAL( size_t( 0 ), interruption.getActiveCount() ); // 기본 해석(이름 = NPC id)으로는 모른다

    interruption.setNpcResolver( []( const ScheduleSystem& schedule, const GameObject& target ) -> int32
    { return target.getName() == hashed_string( "ShopClerk" ) ? schedule.findNpcIndex( "emily" ) : -1; } );
    interruption.applyTargets( { pClerk } );
    SW_EXPECT_EQUAL( size_t( 1 ), interruption.getActiveCount() );
    SW_EXPECT_TRUE( system.getNpcView( emily )._phase == ScheduleNpcPhase::Interrupted );

    interruption.clear();
    SW_EXPECT_EQUAL( size_t( 0 ), interruption.getActiveCount() );
    SW_EXPECT_TRUE( system.getNpcView( emily )._phase != ScheduleNpcPhase::Interrupted );
}

/**
 * @brief [ScheduleIntegrationTest] 스켈레탈 애니메이터 훅 — 블록의 animation 이 먼저, 없으면 활동 종류의 기본 상태, 둘 다 없으면 건드리지 않고, 애니메이터가 없는 NPC 는 실패로 센다
 */
SW_TEST_CASE( ScheduleIntegrationTest, SkeletalAnimatorChoosesTheStateByCue )
{
    GameObjectManager manager;
    (void)manager.createGameObject( hashed_string( "abigail" ) ); // 애니메이터가 없다

    ScheduleSkeletalAnimator animator;
    animator.initialize( &manager );
    animator.setActivityState( hashed_string( "WorkAt" ), hashed_string( "Work" ) );

    ScheduleActivityCue cue;
    cue._npc      = hashed_string( "abigail" );
    cue._activity = hashed_string( "WorkAt" );
    SW_EXPECT_TRUE( animator.chooseState( cue ) == hashed_string( "Work" ) ); // 활동 종류의 기본
    cue._animation = hashed_string( "Sweep" );
    SW_EXPECT_TRUE( animator.chooseState( cue ) == hashed_string( "Sweep" ) ); // 블록의 animation 이 이긴다
    cue._animation = hashed_string{};
    cue._activity  = hashed_string( "Sleep" );
    SW_EXPECT_TRUE( animator.chooseState( cue ).empty() ); // 정하지 않은 활동

    animator.setActivityState( hashed_string( "WorkAt" ), hashed_string( "Labor" ) ); // 다시 정하면 바꾼다
    cue._activity = hashed_string( "WorkAt" );
    SW_EXPECT_TRUE( animator.chooseState( cue ) == hashed_string( "Labor" ) );

    animator.onActivityStarted( cue );
    SW_EXPECT_EQUAL( 1u, animator.getFailedPlayCount() ); // 애니메이터 컴포넌트가 없는 NPC
    cue._activity = hashed_string( "Sleep" );
    animator.onActivityStarted( cue );
    SW_EXPECT_EQUAL( 1u, animator.getFailedPlayCount() ); // 정한 상태가 없으면 시도하지 않는다
    animator.onActivityEnded( cue );
    SW_EXPECT_EQUAL( 2u, animator.getFailedPlayCount() ); // 쉬는 상태(Idle)로 돌아가려다 애니메이터가 없다
    SW_EXPECT_EQUAL( 0u, animator.getPlayCount() );

    animator.setIdleState( hashed_string{} );
    animator.onActivityEnded( cue );
    SW_EXPECT_EQUAL( 2u, animator.getFailedPlayCount() ); // 쉬는 상태를 끄면 아무것도 하지 않는다
}
