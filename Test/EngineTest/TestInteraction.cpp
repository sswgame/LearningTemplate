// 상호작용 — 대상 고르기(거리 · 시야각 · 시야), 누름 · 누르고 있기 · 연타 · 여러 단계, 태그 조건 · 쿨다운 · 강조, 스마트 오브젝트 자리, 집기 폴백,
// 상호작용 완료가 기믹 회로를 움직인다(2D 와 3D 같은 코드).
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourceUtil.h"

#include "GameFramework/Gimmick/GimmickCircuitComponent.h"
#include "GameFramework/Gimmick/GimmickSensorComponent.h"
#include "GameFramework/Interaction/GrabberComponent.h"
#include "GameFramework/Interaction/InteractableComponent.h"
#include "GameFramework/Interaction/InteractionCatalog.h"
#include "GameFramework/Interaction/InteractionSelector.h"
#include "GameFramework/Interaction/InteractionSession.h"
#include "GameFramework/Interaction/InteractorComponent.h"
#include "GameFramework/Interaction/SmartObjectComponent.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 정해 둔 오브젝트 id 를 가리는 벽이 있다고 답하는 가짜 시야입니다. */
    class BlockedLineOfSight final : public ILineOfSightQuery
    {
    public:
        explicit BlockedLineOfSight( uint64 blockedObjectId )
            : _blockedObjectId{ blockedObjectId }
        {
        }

        bool hasLineOfSight( const float3& from, const float3& to, uint64 viewerObjectId, uint64 targetObjectId ) const override
        {
            (void)from;
            (void)to;
            (void)viewerObjectId;
            return targetObjectId != _blockedObjectId;
        }

    private:
        uint64 _blockedObjectId;
    };

    struct InteractionTestInternal
    {
        static InteractionCandidate makeCandidate( uint64 objectId, const float3& position )
        {
            InteractionCandidate candidate;
            candidate._objectId    = objectId;
            candidate._position    = position;
            candidate._maxDistance = 5.0f;
            candidate._maxAngle    = 60.0f * MathUtil::DegreeToRadian;
            return candidate;
        }

        static InteractionDef makeDef( const utf8* pId, InteractionInputMode mode )
        {
            InteractionDef     def;
            InteractionStepDef step;
            step._mode     = mode;
            step._duration = 1.0f;
            step._presses  = 4;
            step._decay    = 1.0f;
            def._id        = hashed_string( pId );
            def._listStep.push_back( step );
            def._maxDistance  = 3.0f;
            def._bLineOfSight = SW_FALSE;
            return def;
        }

        static void tickFrames( GameObjectManager& manager, int32 frameCount, InteractorComponent* pInteractor, bool bHeld )
        {
            for ( int32 frame = 0; frame < frameCount; ++frame )
            {
                pInteractor->setInput( bHeld );
                manager.tick( 1.0f / 60.0f );
            }
        }
    };
} // namespace

/**
 * @brief [InteractionTest] 고르기 — 닿는 것 중 가장 가까운 것, 시야각 밖 · 거리 밖은 빼고, 가려진 것은 건너 다음, 우선도가 거리를 이기고, 2D 는 Z 를 보지 않는다
 */
SW_TEST_CASE( InteractionTest, SelectionPicksNearestInViewWithLineOfSight )
{
    InteractionViewer viewer;
    viewer._position                           = float3{ 0.0f, 0.0f, 0.0f };
    viewer._forward                            = float3{ 0.0f, 0.0f, 1.0f };
    viewer._objectId                           = 99;
    vector<InteractionCandidate> listCandidate = {
        InteractionTestInternal::makeCandidate( 1, float3{ 0.0f, 0.0f, 3.0f } ),  // 앞 3 m
        InteractionTestInternal::makeCandidate( 2, float3{ 0.5f, 0.0f, 2.0f } ),  // 앞 2.06 m — 가장 가깝다
        InteractionTestInternal::makeCandidate( 3, float3{ 0.0f, 0.0f, -1.0f } ), // 뒤 1 m — 시야각 밖
        InteractionTestInternal::makeCandidate( 4, float3{ 0.0f, 0.0f, 9.0f } ),  // 거리 밖
    };
    SW_EXPECT_EQUAL( 1, InteractionSelector::selectBest( viewer, listCandidate, nullptr ) );

    const BlockedLineOfSight wall{ 2 };
    SW_EXPECT_EQUAL( 0, InteractionSelector::selectBest( viewer, listCandidate, &wall ) ); // 2 는 가려져 다음 가까운 1
    listCandidate[1]._bRequiresLineOfSight = SW_FALSE;
    SW_EXPECT_EQUAL( 1, InteractionSelector::selectBest( viewer, listCandidate, &wall ) ); // 시야가 필요 없는 것은 가려져도 된다

    listCandidate[0]._priority = 1;
    SW_EXPECT_EQUAL( 0, InteractionSelector::selectBest( viewer, listCandidate, nullptr ) ); // 우선도가 거리를 이긴다

    // 2D — Z(그리기 순서)가 멀어도 XY 거리로 본다. 시선은 +X.
    InteractionViewer side;
    side._space                           = InteractionSpace::Space2D;
    side._forward                         = float3{ 1.0f, 0.0f, 0.0f };
    vector<InteractionCandidate> listFlat = {
        InteractionTestInternal::makeCandidate( 7, float3{ 2.0f, 0.0f, 50.0f } ),
        InteractionTestInternal::makeCandidate( 8, float3{ -1.0f, 0.0f, 0.0f } ), // 등 뒤
    };
    SW_EXPECT_EQUAL( 0, InteractionSelector::selectBest( side, listFlat, nullptr ) );
}

/**
 * @brief [InteractionTest] 씬에서 시야 — 물리 월드의 막는 상자(2D 콜라이더)가 사이에 있으면 가까운 대상 대신 보이는 대상을 고른다
 */
SW_TEST_CASE( InteractionTest, WorldLineOfSightSkipsBlockedTarget )
{
    GameObjectManager manager;
    GameObject*       pPlayer = manager.createGameObject( hashed_string( "Player" ) );
    pPlayer->addComponent<SceneComponent>();
    InteractorComponent* pInteractor = pPlayer->addComponent<InteractorComponent>();
    pInteractor->setSpace( InteractionSpace::Space2D );
    pInteractor->setEyeOffset( float3{} );

    InteractionDef def = InteractionTestInternal::makeDef( "Use", InteractionInputMode::Press );
    def._maxDistance   = 10.0f;
    def._bLineOfSight  = SW_TRUE;
    GameObject* pNear  = manager.createGameObject( hashed_string( "Near" ) );
    pNear->addComponent<SceneComponent>()->setLocalPosition( float3{ 3.0f, 0.0f, 0.0f } );
    pNear->addComponent<InteractableComponent>()->setDefinition( def );
    GameObject* pFar = manager.createGameObject( hashed_string( "Far" ) );
    pFar->addComponent<SceneComponent>()->setLocalPosition( float3{ -5.0f, 0.0f, 0.0f } );
    pFar->addComponent<InteractableComponent>()->setDefinition( def );
    pInteractor->setFacing2D( float3{ 0.0f, 0.0f, 0.0f } ); // 각은 보지 않는다
    GameObject*             pWall = manager.createGameObject( hashed_string( "Wall" ) );
    BoxCollider2DComponent* pBox  = pWall->addComponent<BoxCollider2DComponent>();
    pBox->setOffsetScale( float2{ 0.5f, 4.0f } );
    pBox->setLocalPosition( float3{ 1.5f, 0.0f, 0.0f } );

    manager.beginPlay();
    InteractionTestInternal::tickFrames( manager, 2, pInteractor, false );
    SW_EXPECT_TRUE( pInteractor->getFocus() == pFar->getHandle() );
    SW_EXPECT_TRUE( pFar->getComponent<InteractableComponent>()->getHighlightRequest() == InteractionHighlight::Outline );
    SW_EXPECT_TRUE( pNear->getComponent<InteractableComponent>()->getHighlightRequest() == InteractionHighlight::None );

    pWall->getPrimarySceneComponent()->setLocalPosition( float3{ 0.0f, 20.0f, 0.0f } ); // 벽을 치운다
    InteractionTestInternal::tickFrames( manager, 2, pInteractor, false );
    SW_EXPECT_TRUE( pInteractor->getFocus() == pNear->getHandle() );
    SW_EXPECT_TRUE( pFar->getComponent<InteractableComponent>()->getHighlightRequest() == InteractionHighlight::None ); // 강조가 옮겨 갔다
    manager.endPlay();
}

/**
 * @brief [InteractionTest] 누르고 있기 — 떼면 취소되고 진행은 처음부터, 끝까지 누르면 완료. 연타는 누를 때마다 오르고 쉬면 줄어든다. 여러 단계는 차례로
 */
SW_TEST_CASE( InteractionTest, HoldCancelsOnReleaseMashAndSteps )
{
    const InteractionDef hold = InteractionTestInternal::makeDef( "Hold", InteractionInputMode::Hold );
    InteractionSession   session;
    SW_ASSERT_TRUE( session.begin( &hold, 1 ) );
    for ( int32 frame = 0; frame < 30; ++frame )
        session.update( 1.0f / 60.0f, true, false );
    SW_EXPECT_NEAR_EQUAL( 0.5f, session.getStepProgress(), 0.02f );
    session.update( 1.0f / 60.0f, false, false ); // 뗐다
    SW_EXPECT_TRUE( session.getState() == InteractionSessionState::Cancelled );
    SW_EXPECT_EQUAL( 0.0f, session.getStepProgress() );
    SW_ASSERT_TRUE( session.begin( &hold, 1 ) );
    SW_EXPECT_EQUAL( 0.0f, session.getStepProgress() ); // 다시 시작하면 처음부터
    for ( int32 frame = 0; frame < 61; ++frame )
        session.update( 1.0f / 60.0f, true, false );
    SW_EXPECT_TRUE( session.getState() == InteractionSessionState::Completed );

    const InteractionDef mash = InteractionTestInternal::makeDef( "Mash", InteractionInputMode::Mash );
    SW_ASSERT_TRUE( session.begin( &mash, 1 ) );
    session.update( 0.1f, true, true );
    session.update( 0.1f, true, true );
    SW_EXPECT_NEAR_EQUAL( 0.5f, session.getStepProgress(), 1.0e-4f );
    session.update( 0.25f, false, false ); // 쉬면 줄어든다
    SW_EXPECT_NEAR_EQUAL( 0.25f, session.getStepProgress(), 1.0e-4f );
    for ( int32 press = 0; press < 3; ++press )
        session.update( 0.01f, true, true );
    SW_EXPECT_TRUE( session.getState() == InteractionSessionState::Completed );

    InteractionDef steps = InteractionTestInternal::makeDef( "Craft", InteractionInputMode::Press );
    steps._listStep.push_back( hold._listStep.front() );
    steps._listStep.push_back( mash._listStep.front() );
    SW_ASSERT_TRUE( session.begin( &steps, 1 ) ); // 첫 단계(누름)는 시작한 누름으로 끝난다
    SW_EXPECT_EQUAL( 1, session.getStepIndex() );
    for ( int32 frame = 0; frame < 61; ++frame )
        session.update( 1.0f / 60.0f, true, false );
    SW_EXPECT_EQUAL( 2, session.getStepIndex() );
    for ( int32 press = 0; press < 4; ++press )
        session.update( 0.01f, true, true );
    vector<InteractionSessionEvent> listEvent;
    session.drainEvents( listEvent );
    SW_EXPECT_TRUE( session.getState() == InteractionSessionState::Completed );
    SW_EXPECT_TRUE( listEvent.size() >= 3 && listEvent.back() == InteractionSessionEvent::Completed );
}

/**
 * @brief [InteractionTest] 씬 — 열쇠 태그가 있어야 고를 수 있고, 누르고 있기를 끝내면 대상의 기믹 센서로 회로의 문이 열리며, 쿨다운 동안은 다시 못 고른다
 */
SW_TEST_CASE( InteractionTest, CompletedInteractionDrivesGimmickAndCooldown )
{
    GameObjectManager manager;
    GameObject*       pPlayer = manager.createGameObject( hashed_string( "Player" ) );
    pPlayer->addComponent<SceneComponent>();
    InteractorComponent* pInteractor = pPlayer->addComponent<InteractorComponent>();
    pInteractor->setEyeOffset( float3{} );

    InteractionDef def = InteractionTestInternal::makeDef( "Unlock", InteractionInputMode::Hold );
    def._cooldown      = 1.0f;
    def._requiredTags.addTag( TagID::request( "Item.Key" ) );
    GameObject* pLock = manager.createGameObject( hashed_string( "Lock" ) );
    pLock->addComponent<SceneComponent>()->setLocalPosition( float3{ 0.0f, 0.0f, 1.0f } );
    pLock->addComponent<InteractableComponent>()->setDefinition( def );
    pLock->addComponent<GimmickSensorComponent>();
    GameObject* pDoor = manager.createGameObject( hashed_string( "Door" ) );
    pDoor->addComponent<SceneComponent>();
    GimmickCircuitComponent* pCircuit = pLock->addComponent<GimmickCircuitComponent>();
    pCircuit->addNode( "use", "Interaction", "" );
    pCircuit->addNode( "door", "Door", "openTime=0.1; openOffset=0 2 0", pDoor->getHandle() );
    pCircuit->addWire( "use.Toggled", "door.Open" );
    SW_ASSERT_TRUE( pCircuit->rebuild() );

    manager.beginPlay();
    InteractionTestInternal::tickFrames( manager, 2, pInteractor, false );
    SW_EXPECT_FALSE( pInteractor->getPrompt()._bVisible == SW_TRUE ); // 열쇠가 없다
    pPlayer->addTag( TagID::request( "Item.Key" ) );
    InteractionTestInternal::tickFrames( manager, 2, pInteractor, false );
    SW_EXPECT_TRUE( pInteractor->getPrompt()._bVisible == SW_TRUE );
    SW_EXPECT_TRUE( pInteractor->getPrompt()._mode == InteractionInputMode::Hold );

    InteractionTestInternal::tickFrames( manager, 30, pInteractor, true ); // 반만 누르고
    SW_EXPECT_TRUE( pInteractor->isInteracting() );
    InteractionTestInternal::tickFrames( manager, 2, pInteractor, false ); // 뗐다 — 취소
    SW_EXPECT_FALSE( pInteractor->isInteracting() );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pDoor->getPrimarySceneComponent()->getWorldPosition()._y, 1.0e-4f );

    InteractionTestInternal::tickFrames( manager, 70, pInteractor, true ); // 끝까지
    InteractionTestInternal::tickFrames( manager, 12, pInteractor, false );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pDoor->getPrimarySceneComponent()->getWorldPosition()._y, 1.0e-4f ); // 회로의 문이 열렸다
    SW_EXPECT_TRUE( pLock->getComponent<InteractableComponent>()->getCooldownRemaining() > 0.0f );
    SW_EXPECT_FALSE( pInteractor->getPrompt()._bVisible == SW_TRUE ); // 쿨다운 동안은 고르지 않는다
    InteractionTestInternal::tickFrames( manager, 60, pInteractor, false );
    SW_EXPECT_TRUE( pInteractor->getPrompt()._bVisible == SW_TRUE );
    manager.endPlay();
}

/**
 * @brief [InteractionTest] 스마트 오브젝트 — 플레이어와 AI 가 같은 함수로 자리를 차지 · 비우고, 남의 자리는 못 가지며, 한 이는 한 자리, 태그로 빈자리 찾기
 */
SW_TEST_CASE( InteractionTest, SmartObjectSlotClaimAndRelease )
{
    SmartObjectDef bench;
    bench._id = hashed_string( "Bench" );
    for ( int32 slot = 0; slot < 2; ++slot )
    {
        SmartObjectSlotDef slotDef;
        slotDef._id     = hashed_string( slot == 0 ? "Left" : "Right" );
        slotDef._offset = float3{ slot == 0 ? -0.5f : 0.5f, 0.0f, 0.0f };
        slotDef._tags.addTag( TagID::request( "Activity.Sit" ) );
        bench._listSlot.push_back( slotDef );
    }
    GameObjectManager manager;
    GameObject*       pBench = manager.createGameObject( hashed_string( "Bench" ) );
    pBench->addComponent<SceneComponent>()->setLocalPosition( float3{ 10.0f, 0.0f, 0.0f } );
    SmartObjectComponent* pSmart = pBench->addComponent<SmartObjectComponent>();
    pSmart->setDefinition( bench );
    GameObject* pPlayer = manager.createGameObject( hashed_string( "Player" ) );
    GameObject* pAiA    = manager.createGameObject( hashed_string( "VillagerA" ) );
    GameObject* pAiB    = manager.createGameObject( hashed_string( "VillagerB" ) );

    TagContainer sit;
    sit.addTag( TagID::request( "Activity.Sit" ) );
    SW_EXPECT_TRUE( pSmart->claimSlot( *pPlayer, 1 ) );
    SW_EXPECT_FALSE( pSmart->claimSlot( *pAiA, 1 ) ); // 남의 자리
    SW_EXPECT_EQUAL( 0, pSmart->claimFreeSlot( *pAiA, sit ) );
    SW_EXPECT_EQUAL( -1, pSmart->claimFreeSlot( *pAiB, sit ) ); // 꽉 찼다
    SW_EXPECT_FALSE( pSmart->claimSlot( *pPlayer, 0 ) );        // 한 이는 한 자리
    SW_EXPECT_EQUAL( 0, pSmart->countFreeSlots() );
    SW_EXPECT_TRUE( pSmart->releaseSlot( *pPlayer ) );
    SW_EXPECT_FALSE( pSmart->releaseSlot( *pPlayer ) );
    SW_EXPECT_EQUAL( 1, pSmart->claimFreeSlot( *pAiB, sit ) );
    TagContainer cover;
    cover.addTag( TagID::request( "Cover.Low" ) );
    SW_EXPECT_EQUAL( -1, pSmart->claimFreeSlot( *pPlayer, cover ) ); // 태그가 맞는 자리가 없다

    manager.flushSceneTransforms();
    float3  position{};
    float32 yaw{ 0.0f };
    SW_ASSERT_TRUE( pSmart->computeSlotTransform( 1, position, yaw ) );
    SW_EXPECT_NEAR_EQUAL( 10.5f, position._x, 1.0e-4f );

    // 차지한 이가 사라지면 자리가 빈다.
    manager.destroyObject( pAiA );
    manager.tick( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( 0, pSmart->claimFreeSlot( *pPlayer, sit ) );
}

/**
 * @brief [InteractionTest] 집기 폴백 — 집으면 손 자리에 붙어 따라오고, 던지면 떨어져 그 자리에 남는다(수직 속도는 중력 컴포넌트로)
 */
SW_TEST_CASE( InteractionTest, GrabFallbackAttachesAndReleases )
{
    GameObjectManager manager;
    GameObject*       pHolder = manager.createGameObject( hashed_string( "Holder" ) );
    pHolder->addComponent<SceneComponent>();
    GrabberComponent* pGrabber = pHolder->addComponent<GrabberComponent>();
    pGrabber->setHoldOffset( float3{ 0.0f, 1.0f, 0.5f } );
    GameObject* pCrate = manager.createGameObject( hashed_string( "Crate" ) );
    pCrate->addComponent<SceneComponent>()->setLocalPosition( float3{ 3.0f, 0.0f, 0.0f } );

    SW_ASSERT_TRUE( pGrabber->grab( *pCrate ) );
    SW_EXPECT_FALSE( pGrabber->grab( *pCrate ) ); // 이미 들고 있다
    pHolder->getPrimarySceneComponent()->setLocalPosition( float3{ 5.0f, 0.0f, 0.0f } );
    manager.flushSceneTransforms();
    SW_EXPECT_NEAR_EQUAL( 5.0f, pCrate->getPrimarySceneComponent()->getWorldPosition()._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pCrate->getPrimarySceneComponent()->getWorldPosition()._y, 1.0e-4f );

    SW_EXPECT_TRUE( pGrabber->throwHeld( float3{ 0.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_FALSE( pGrabber->isHolding() );
    pHolder->getPrimarySceneComponent()->setLocalPosition( float3{ -5.0f, 0.0f, 0.0f } );
    manager.flushSceneTransforms();
    SW_EXPECT_NEAR_EQUAL( 5.0f, pCrate->getPrimarySceneComponent()->getWorldPosition()._x, 1.0e-4f ); // 놓은 자리에 남았다
}

/**
 * @brief [InteractionTest] 공용 상호작용 표는 모르는 이름 없이 읽히고, 여러 단계 · 태그 · 각(도 → 라디안) · 스마트 오브젝트 자리를 담는다. 모르는 모드는 읽기 오류
 */
SW_TEST_CASE( InteractionTest, CatalogReadsStepsTagsAndSlots )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    InteractionCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromResource( InteractionCatalog::kDefaultPath ) );
    const InteractionDef* pCraft = catalog.findInteraction( "Craft" );
    SW_ASSERT_NOT_NULL( pCraft );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), pCraft->_listStep.size() );
    SW_EXPECT_TRUE( pCraft->_listStep[1]._mode == InteractionInputMode::Mash );
    const InteractionDef* pUnlock = catalog.findInteraction( "Unlock" );
    SW_ASSERT_NOT_NULL( pUnlock );
    SW_EXPECT_TRUE( pUnlock->_requiredTags.hasTag( TagID::request( "Item.Key" ) ) );
    SW_EXPECT_TRUE( pUnlock->_authority == InteractionAuthority::Server );
    SW_EXPECT_NEAR_EQUAL( 70.0f * MathUtil::DegreeToRadian, pUnlock->_maxAngle, 1.0e-4f );
    const SmartObjectDef* pBench = catalog.findSmartObject( "Bench" );
    SW_ASSERT_NOT_NULL( pBench );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), pBench->_listSlot.size() );

    InteractionCatalog broken;
    SW_TEST_DEFENSIVE_SCOPE( "interaction table with an unknown mode and attribute" );
    SW_EXPECT_FALSE( broken.loadFromXmlText( R"(<Interactions><Interaction id="X" mode="Tap"/></Interactions>)", "mode.interactions.xml" ) );
    SW_EXPECT_FALSE( broken.loadFromXmlText( R"(<Interactions><Interaction id="Y" range="3"/></Interactions>)", "attribute.interactions.xml" ) );
}
