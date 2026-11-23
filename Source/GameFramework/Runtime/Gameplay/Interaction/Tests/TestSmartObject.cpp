// SmartObjectTest — 스마트 오브젝트 슬롯(차지 · 비우기 · 태그 찾기)과 컴포넌트(사라진 쪽의 자리 · 슬롯 자리), 일정 예약 어댑터(시간 창 · 결정성 · 점유 · 저장)를 본다(nogpu).
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

#include "GameFramework/Runtime/Gameplay/Interaction/InteractionCatalog.h"
#include "GameFramework/Runtime/Gameplay/Interaction/SmartObjectComponent.h"
#include "GameFramework/Runtime/Gameplay/Interaction/SmartObjectScheduleLocator.h"

using namespace sw;

namespace
{
    struct SmartObjectTestUtil
    {
        /** @brief 앉는 자리 둘(x −0.5 · +0.5)과 만드는 자리 하나(z +1)를 가진 정의입니다. */
        static SmartObjectDef makeDefinition( const utf8* pID )
        {
            SmartObjectDef     def;
            SmartObjectSlotDef left;
            left._id     = hashed_string( "Left" );
            left._offset = float3{ -0.5f, 0.0f, 0.0f };
            left._tags.addTag( TagID::request( "Activity.Sit" ) );
            SmartObjectSlotDef right = left;
            right._id                = hashed_string( "Right" );
            right._offset            = float3{ 0.5f, 0.0f, 0.0f };
            SmartObjectSlotDef craft;
            craft._id     = hashed_string( "Craft" );
            craft._offset = float3{ 0.0f, 0.0f, 1.0f };
            craft._tags.addTag( TagID::request( "Activity.Craft" ) );
            def._id = hashed_string( pID );
            def._listSlot.push_back( left );
            def._listSlot.push_back( right );
            def._listSlot.push_back( craft );
            return def;
        }

        static TagContainer makeTags( const utf8* pTag )
        {
            TagContainer tags;
            tags.addTag( TagID::request( pTag ) );
            return tags;
        }

        static SmartObjectComponent* makeObject( GameObjectManager& manager, const utf8* pName, const float3& position )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
            pObject->addComponent<SceneComponent>()->setLocalPosition( position );
            SmartObjectComponent* pSmart = pObject->addComponent<SmartObjectComponent>();
            pSmart->setDefinition( makeDefinition( "Bench" ) );
            return pSmart;
        }

        static ScheduleReserveRequest makeRequest( const utf8* pNPC, int32 startMinute, int32 endMinute, const float3& nearPosition )
        {
            ScheduleReserveRequest request;
            request._npc         = hashed_string( pNPC );
            request._objectKind  = hashed_string( "Bench" );
            request._near        = nearPosition;
            request._startMinute = startMinute;
            request._endMinute   = endMinute;
            return request;
        }
    };
    using Util = SmartObjectTestUtil;
} // namespace

/** @brief [SmartObjectTest] 한 쪽은 슬롯 하나만 차지하고, 남이 차지한 슬롯은 거절하며, 같은 슬롯을 다시 차지하면 그대로다. 범위 밖 · 빈 id 는 거절한다 */
SW_TEST_CASE( SmartObjectTest, SlotHoldsOneClaimant )
{
    SmartObjectSlots slots;
    slots.initialize( Util::makeDefinition( "Bench" ) );
    SW_EXPECT_EQUAL( 3, slots.countFree() );
    SW_EXPECT_TRUE( slots.claim( 0, 11 ) );
    SW_EXPECT_TRUE( slots.claim( 0, 11 ) );  // 멱등
    SW_EXPECT_FALSE( slots.claim( 0, 22 ) ); // 남의 자리
    SW_EXPECT_FALSE( slots.claim( 1, 11 ) ); // 이미 하나를 차지했다
    SW_EXPECT_FALSE( slots.claim( 3, 22 ) );
    SW_EXPECT_FALSE( slots.claim( -1, 22 ) );
    SW_EXPECT_FALSE( slots.claim( 1, 0 ) ); // 0 은 빈 자리 표시다
    SW_EXPECT_EQUAL( 2, slots.countFree() );
    SW_EXPECT_TRUE( slots.release( 11 ) );
    SW_EXPECT_FALSE( slots.release( 11 ) );
    SW_EXPECT_TRUE( slots.claim( 0, 22 ) );
}

/** @brief [SmartObjectTest] 찾기는 요구한 태그를 모두 가진 빈 슬롯 가운데 앞의 것이고, 다 차면 비어 있다 */
SW_TEST_CASE( SmartObjectTest, FreeSlotSearchFollowsTags )
{
    SmartObjectSlots slots;
    slots.initialize( Util::makeDefinition( "Bench" ) );
    SW_EXPECT_EQUAL( 2, slots.findFreeSlot( Util::makeTags( "Activity.Craft" ) ).value() );
    SW_EXPECT_EQUAL( 0, slots.findFreeSlot( Util::makeTags( "Activity.Sit" ) ).value() );
    SW_EXPECT_TRUE( slots.claim( 0, 11 ) );
    SW_EXPECT_EQUAL( 1, slots.findFreeSlot( Util::makeTags( "Activity.Sit" ) ).value() );
    SW_EXPECT_TRUE( slots.claim( 1, 22 ) );
    SW_EXPECT_FALSE( slots.findFreeSlot( Util::makeTags( "Activity.Sit" ) ).has_value() ); // 만드는 자리는 남았지만 태그가 다르다
    SW_EXPECT_EQUAL( 2, slots.findFreeSlot( TagContainer{} ).value() );
    SW_EXPECT_EQUAL( 1, slots.findSlotOf( 22 ).value() );
}

/** @brief [SmartObjectTest] 컴포넌트: 차지한 오브젝트가 사라지면 다음에 찾는 이가 그 자리를 얻고, 슬롯 자리는 오브젝트의 월드 자세를 따른다 */
SW_TEST_CASE( SmartObjectTest, DestroyedClaimantFreesItsSlot )
{
    GameObjectManager     manager;
    SmartObjectComponent* pSmart  = Util::makeObject( manager, "Bench", float3{ 10.0f, 0.0f, 0.0f } );
    GameObject*           pFirst  = manager.createGameObject( hashed_string( "Anna" ) );
    GameObject*           pSecond = manager.createGameObject( hashed_string( "Ben" ) );
    SW_EXPECT_EQUAL( 0, pSmart->claimFreeSlot( *pFirst, Util::makeTags( "Activity.Sit" ) ).value() );
    SW_EXPECT_EQUAL( 0, pSmart->claimFreeSlot( *pFirst, Util::makeTags( "Activity.Sit" ) ).value() ); // 이미 앉은 자리
    SW_EXPECT_EQUAL( 1, pSmart->claimFreeSlot( *pSecond, Util::makeTags( "Activity.Sit" ) ).value() );
    SW_EXPECT_EQUAL( 1, pSmart->countFreeSlots() );

    float3  position{};
    float32 yaw = 0.0f;
    SW_REQUIRE_TRUE( pSmart->computeSlotTransform( 1, position, yaw ) );
    SW_EXPECT_NEAR( 10.5f, position._x, 1.0e-4f );
    SW_EXPECT_FALSE( pSmart->computeSlotTransform( 3, position, yaw ) );

    pFirst->destroy();
    GameObject* pThird = manager.createGameObject( hashed_string( "Carl" ) );
    SW_EXPECT_EQUAL( 0, pSmart->claimFreeSlot( *pThird, Util::makeTags( "Activity.Sit" ) ).value() );
    SW_EXPECT_TRUE( pSmart->releaseSlot( *pSecond ) );
    SW_EXPECT_FALSE( pSmart->releaseSlot( *pSecond ) );
}

/** @brief [SmartObjectTest] 예약 어댑터: 겹치는 시간 창은 다른 슬롯을, 겹치지 않으면 같은 슬롯을 주고, 같은 요청은 같은 예약이다(멱등). 슬롯이 모자라면 거절한다 */
SW_TEST_CASE( SmartObjectTest, LocatorReservesTimeWindows )
{
    Scene scene( "SmartObjectWindows" );
    (void)Util::makeObject( *scene.getObjectManager(), "Bench", float3{} );
    SmartObjectScheduleLocator locator;
    locator.initialize( &scene );

    ScheduleLocation firstSpot;
    ScheduleLocation secondSpot;
    uint32           firstID  = 0;
    uint32           secondID = 0;
    SW_REQUIRE_TRUE( locator.reserve( Util::makeRequest( "anna", 600, 720, float3{ -4.0f, 0.0f, 0.0f } ), firstSpot, firstID ) );
    SW_REQUIRE_TRUE( locator.reserve( Util::makeRequest( "ben", 660, 780, float3{ -4.0f, 0.0f, 0.0f } ), secondSpot, secondID ) );
    SW_EXPECT_TRUE( firstID != 0 && secondID != 0 && firstID != secondID );
    SW_EXPECT_NEAR( -0.5f, firstSpot._position._x, 1.0e-4f ); // 가장 가까운 자리
    SW_EXPECT_NEAR( 0.0f, secondSpot._position._x, 1.0e-4f ); // 왼쪽이 찼으니 그다음 가까운 자리(가운데 만드는 자리)

    ScheduleLocation again;
    uint32           againID = 0;
    SW_REQUIRE_TRUE( locator.reserve( Util::makeRequest( "anna", 630, 700, float3{ 4.0f, 0.0f, 0.0f } ), again, againID ) );
    SW_EXPECT_EQUAL( firstID, againID ); // 같은 NPC · 종류 · 날의 겹치는 창

    ScheduleLocation laterSpot;
    uint32           laterID = 0;
    SW_REQUIRE_TRUE( locator.reserve( Util::makeRequest( "carl", 720, 800, float3{ -4.0f, 0.0f, 0.0f } ), laterSpot, laterID ) );
    SW_EXPECT_NEAR( -0.5f, laterSpot._position._x, 1.0e-4f ); // anna 의 창이 끝난 뒤라 같은 자리

    uint32 thirdID = 0;
    SW_REQUIRE_TRUE( locator.reserve( Util::makeRequest( "dora", 660, 700, float3{} ), laterSpot, thirdID ) );
    uint32 fourthID = 0;
    SW_EXPECT_FALSE( locator.reserve( Util::makeRequest( "eve", 660, 700, float3{} ), laterSpot, fourthID ) ); // 세 자리가 모두 찼다
    locator.release( thirdID );
    SW_EXPECT_TRUE( locator.reserve( Util::makeRequest( "eve", 660, 700, float3{} ), laterSpot, fourthID ) );
}

/** @brief [SmartObjectTest] 예약한 자리를 플레이어가 먼저 차지했으면 점유를 거절하고, 놓은 뒤에는 점유한다. 저장한 예약은 같은 자리로 돌아오고 사라진 오브젝트의 예약은 버린다 */
SW_TEST_CASE( SmartObjectTest, LocatorClaimsAndRestores )
{
    Scene scene( "SmartObjectReservation" );
    scene.setSourcePath( "game/test/maps/reservation.scene.xml" );
    GameObjectManager&         manager = *scene.getObjectManager();
    SmartObjectComponent*      pSmart  = Util::makeObject( manager, "Bench", float3{} );
    GameObject*                pPlayer = manager.createGameObject( hashed_string( "Player" ) );
    GameObject*                pFirst  = manager.createGameObject( hashed_string( "anna" ) );
    SmartObjectScheduleLocator locator;
    locator.initialize( &scene );
    ScheduleLocation spot;
    uint32           reservationID = 0;
    SW_REQUIRE_TRUE( locator.reserve( Util::makeRequest( "anna", 600, 720, float3{ -4.0f, 0.0f, 0.0f } ), spot, reservationID ) );

    SW_EXPECT_TRUE( pSmart->claimSlot( *pPlayer, 0 ) );
    SW_EXPECT_FALSE( locator.claimReserved( reservationID, *pFirst ) );
    SW_EXPECT_TRUE( pSmart->releaseSlot( *pPlayer ) );
    SW_EXPECT_TRUE( locator.claimReserved( reservationID, *pFirst ) );
    SW_EXPECT_EQUAL( 0, pSmart->findSlotOf( *pFirst ).value() );
    SW_EXPECT_TRUE( locator.releaseClaim( reservationID, *pFirst ) );
    SW_EXPECT_FALSE( locator.releaseClaim( reservationID, *pFirst ) );
    SW_EXPECT_FALSE( locator.claimReserved( reservationID + 1, *pFirst ) ); // 모르는 예약

    vector<ScheduleReservationSaveState> listSaved;
    locator.fillState( listSaved );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listSaved.size() );
    SW_EXPECT_EQUAL( scene.findFileID( pSmart->getOwner()->getObjectID() ), listSaved[0]._object._fileID ); // 런타임 id 가 아니라 파일 id 다
    SmartObjectScheduleLocator restored;
    restored.initialize( &scene );
    restored.restoreState( listSaved );
    SW_EXPECT_EQUAL( size_t{ 1 }, restored.getReservationCount() );
    ScheduleLocation restoredSpot;
    uint32           restoredID = 0;
    SW_REQUIRE_TRUE( restored.reserve( Util::makeRequest( "anna", 600, 720, float3{ 4.0f, 0.0f, 0.0f } ), restoredSpot, restoredID ) );
    SW_EXPECT_EQUAL( reservationID, restoredID );

    pSmart->getOwner()->destroy();
    SmartObjectScheduleLocator orphaned;
    orphaned.initialize( &scene );
    orphaned.restoreState( listSaved );
    SW_EXPECT_EQUAL( size_t{ 0 }, orphaned.getReservationCount() );
}

/** @brief [SmartObjectTest] 다른 실행에서 런타임 id 가 달라져도 파일 id 로 같은 오브젝트의 예약이 돌아온다. 저장한 적 없는 씬의 예약은 오류를 남기고 저장하지 않는다 */
SW_TEST_CASE( SmartObjectTest, LocatorReservationSurvivesNewRuntimeIDs )
{
    Scene first( "SmartObjectFirstRun" );
    first.setSourcePath( "game/test/maps/reservation.scene.xml" );
    SmartObjectComponent*      pFirstBench = Util::makeObject( *first.getObjectManager(), "Bench", float3{} );
    SmartObjectScheduleLocator locator;
    locator.initialize( &first );
    ScheduleLocation spot;
    uint32           reservationID = 0;
    SW_REQUIRE_TRUE( locator.reserve( Util::makeRequest( "anna", 600, 720, float3{} ), spot, reservationID ) );
    vector<ScheduleReservationSaveState> listSaved;
    locator.fillState( listSaved );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listSaved.size() );

    // 다른 실행 — 저장할 때의 런타임 id 는 다른 벤치가 받고, 예약한 벤치는 새 id 를 받는다.
    Scene second( "SmartObjectSecondRun" );
    second.setSourcePath( "game/test/maps/reservation.scene.xml" );
    GameObjectManager& manager = *second.getObjectManager();
    GameObject*        pOther  = manager.createGameObjectWithID( hashed_string( "Other" ), pFirstBench->getOwner()->getObjectID() );
    pOther->addComponent<SceneComponent>()->setLocalPosition( float3{ 50.0f, 0.0f, 0.0f } );
    pOther->addComponent<SmartObjectComponent>()->setDefinition( Util::makeDefinition( "Bench" ) );
    SmartObjectComponent* pSecondBench = Util::makeObject( manager, "Bench", float3{ 10.0f, 0.0f, 0.0f } );
    second.rebindRestoredFileIDs( {
        Scene::SavedFileID{ pSecondBench->getOwner()->getObjectID(), listSaved[0]._object._fileID }
    } );

    SmartObjectScheduleLocator restored;
    restored.initialize( &second );
    restored.restoreState( listSaved );
    SW_REQUIRE_EQUAL( size_t{ 1 }, restored.getReservationCount() );
    ScheduleLocation restoredSpot;
    uint32           restoredID = 0;
    SW_REQUIRE_TRUE( restored.reserve( Util::makeRequest( "anna", 600, 720, float3{} ), restoredSpot, restoredID ) );
    SW_EXPECT_EQUAL( reservationID, restoredID );
    SW_EXPECT_NEAR( spot._position._x + 10.0f, restoredSpot._position._x, 1.0e-4f );

    second.setSourcePath( "" );
    SW_EXPECT_LOG( "SmartObject.ReservationHasNoIdentity" );
    restored.fillState( listSaved );
    SW_EXPECT_TRUE( listSaved.empty() );
}
