// 상호작용 — 대상 고르기(거리 · 시야각 · 시야), 누름 · 누르고 있기 · 연타 · 여러 단계, 태그 조건 · 쿨다운 · 강조, 스마트 오브젝트 자리, 집기 폴백,
// 상호작용 완료가 기믹 회로를 움직인다(2D 와 3D 같은 코드).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Animation/MotionWarpingComponent.h"
#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/Cache/IAssetCache.h"
#include "Engine/Resource/ResourceUtil.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuitComponent.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickSensorComponent.h"
#include "GameFramework/Base/Gameplay/Interaction/GrabberComponent.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractableComponent.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractionCatalog.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractionSelector.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractionSession.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractorComponent.h"
#include "GameFramework/Base/Gameplay/Interaction/SmartObjectComponent.h"

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
            candidate._maxAngle    = 60.0f * MathUtil::kDegreeToRadian;
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
    {
        session.update( 1.0f / 60.0f, true, false );
    }
    SW_EXPECT_NEAR_EQUAL( 0.5f, session.getStepProgress(), 0.02f );
    session.update( 1.0f / 60.0f, false, false ); // 뗐다
    SW_EXPECT_TRUE( session.getState() == InteractionSessionState::Cancelled );
    SW_EXPECT_EQUAL( 0.0f, session.getStepProgress() );
    SW_ASSERT_TRUE( session.begin( &hold, 1 ) );
    SW_EXPECT_EQUAL( 0.0f, session.getStepProgress() ); // 다시 시작하면 처음부터
    for ( int32 frame = 0; frame < 61; ++frame )
    {
        session.update( 1.0f / 60.0f, true, false );
    }
    SW_EXPECT_TRUE( session.getState() == InteractionSessionState::Completed );

    const InteractionDef mash = InteractionTestInternal::makeDef( "Mash", InteractionInputMode::Mash );
    SW_ASSERT_TRUE( session.begin( &mash, 1 ) );
    session.update( 0.1f, true, true );
    session.update( 0.1f, true, true );
    SW_EXPECT_NEAR_EQUAL( 0.5f, session.getStepProgress(), 1.0e-4f );
    session.update( 0.25f, false, false ); // 쉬면 줄어든다
    SW_EXPECT_NEAR_EQUAL( 0.25f, session.getStepProgress(), 1.0e-4f );
    for ( int32 press = 0; press < 3; ++press )
    {
        session.update( 0.01f, true, true );
    }
    SW_EXPECT_TRUE( session.getState() == InteractionSessionState::Completed );

    InteractionDef steps = InteractionTestInternal::makeDef( "Craft", InteractionInputMode::Press );
    steps._listStep.push_back( hold._listStep.front() );
    steps._listStep.push_back( mash._listStep.front() );
    SW_ASSERT_TRUE( session.begin( &steps, 1 ) ); // 첫 단계(누름)는 시작한 누름으로 끝난다
    SW_EXPECT_EQUAL( 1, session.getStepIndex() );
    for ( int32 frame = 0; frame < 61; ++frame )
    {
        session.update( 1.0f / 60.0f, true, false );
    }
    SW_EXPECT_EQUAL( 2, session.getStepIndex() );
    for ( int32 press = 0; press < 4; ++press )
    {
        session.update( 0.01f, true, true );
    }
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
    SW_EXPECT_NEAR_EQUAL( 70.0f * MathUtil::kDegreeToRadian, pUnlock->_maxAngle, 1.0e-4f );
    const SmartObjectDef* pBench = catalog.findSmartObject( "Bench" );
    SW_ASSERT_NOT_NULL( pBench );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), pBench->_listSlot.size() );

    InteractionCatalog broken;
    SW_TEST_DEFENSIVE_SCOPE( "interaction table with an unknown mode and attribute" );
    SW_EXPECT_FALSE( broken.loadFromXMLText( R"(<Interactions><Interaction id="X" mode="Tap"/></Interactions>)", "mode.interactions.xml" ) );
    SW_EXPECT_FALSE( broken.loadFromXMLText( R"(<Interactions><Interaction id="Y" range="3"/></Interactions>)", "attribute.interactions.xml" ) );
}

/**
 * @brief [InteractionTest] 상호작용을 시작하면 하는 쪽의 모션 워핑에 대상의 맞춤 지점이 정의의 마커 이름으로 워프 목표가 된다
 */
SW_TEST_CASE( InteractionTest, BeginningInteractionSetsAlignmentWarpTarget )
{
    GameObjectManager manager;
    GameObject*       pPlayer = manager.createGameObject( hashed_string( "Player" ) );
    pPlayer->addComponent<SceneComponent>();
    InteractorComponent* pInteractor = pPlayer->addComponent<InteractorComponent>();
    pInteractor->setEyeOffset( float3{} );
    MotionWarpingComponent* pWarping = pPlayer->addComponent<MotionWarpingComponent>();
    SW_ASSERT_NOT_NULL( pWarping );

    InteractionDef def   = InteractionTestInternal::makeDef( "Open", InteractionInputMode::Hold );
    def._alignmentMarker = hashed_string( "Handle" );
    GameObject* pDoor    = manager.createGameObject( hashed_string( "Door" ) );
    pDoor->addComponent<SceneComponent>()->setLocalPosition( float3{ 0.5f, 0.0f, 1.0f } );
    pDoor->addComponent<InteractableComponent>()->setDefinition( def );

    manager.beginPlay();
    InteractionTestInternal::tickFrames( manager, 2, pInteractor, false );
    SW_EXPECT_TRUE( pWarping->findWarpTarget( hashed_string( "Handle" ) ) == nullptr );
    InteractionTestInternal::tickFrames( manager, 3, pInteractor, true );
    SW_EXPECT_TRUE( pInteractor->isInteracting() );
    const MotionWarpTarget* pTarget = pWarping->findWarpTarget( hashed_string( "Handle" ) );
    SW_ASSERT_NOT_NULL( pTarget );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pTarget->_position._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pTarget->_position._z, 1.0e-4f );
    manager.endPlay();
}

/**
 * @brief [InteractionTest] 상호작용 표 파일을 고치면 핫 리로드가 등록부의 캐시로 새 표를 읽고, 컴포넌트가 다음 틱에 새 정의를 쓴다
 * @details 표 캐시가 모듈 정적 맵이라 등록부 밖에 있으면 에디터 핫 리로드가 찾을 이름이 없어, 고친 파일이 다시 시작할 때까지 반영되지 않는다.
 *          게임 서비스가 묶이면(`bindGameService` — `AssetManager` 포함) 캐시가 등록부에 오르고, 다시 읽은 뒤에도 옛 정의를 든 쪽은 죽은 메모리를 읽지 않는다.
 */
SW_TEST_CASE( InteractionTest, EditedCatalogReachesComponentsThroughTheAssetCacheRegistry )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const string path = FileUtil::joinPath( test::makeTempPath( "catalogreload" ), "edited.interactions.xml" );
    FileUtil::ensureParentDirectoryExists( path );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( path, R"(<Interactions><Interaction id="Open" mode="Press" maxDistance="2"/></Interactions>)" ) );

    AssetManager  resources;
    ModuleService service{};
    service.arrServices[internal::toRawServiceId( internal::ModuleServiceId::AssetManager )] = &resources;
    game::bindGameService( service );

    GameObjectManager      manager;
    GameObject*            pDoor         = manager.createGameObject( hashed_string( "Door" ) );
    InteractableComponent* pInteractable = pDoor != nullptr ? pDoor->addComponent<InteractableComponent>() : nullptr;
    if ( pInteractable != nullptr )
    {
        pInteractable->setCatalogPath( path );
        pInteractable->setInteractionId( "Open" );
    }
    IAssetCache*          pCache  = resources.findAssetCache( "InteractionCatalog" ); // 표 캐시는 처음 읽을 때 생겨 묶인 등록부에 오른다
    const InteractionDef* pBefore = pInteractable != nullptr ? pInteractable->getDefinition() : nullptr;

    SW_ASSERT_TRUE( FileUtil::writeTextFile( path, R"(<Interactions><Interaction id="Open" mode="Press" maxDistance="5"/></Interactions>)" ) );
    const bool bCached = pCache != nullptr && pCache->isCached( path );
    if ( pCache != nullptr )
        pCache->reload( path, nullptr );
    if ( pInteractable != nullptr )
        pInteractable->onTick( 0.0f );
    const InteractionDef* pAfter = pInteractable != nullptr ? pInteractable->getDefinition() : nullptr;
    game::unbindGameService();

    SW_EXPECT_NULL( resources.findAssetCache( "InteractionCatalog" ) ); // 서비스가 풀리면 등록부에서 내려간다
    SW_ASSERT_NOT_NULL( pInteractable );
    SW_ASSERT_TRUE_MSG( pCache != nullptr, "상호작용 표 캐시가 에셋 캐시 등록부에 없다 — 에디터 핫 리로드가 고친 표를 다시 읽을 길이 없다" );
    SW_EXPECT_TRUE( bCached );
    SW_ASSERT_NOT_NULL( pBefore );
    SW_ASSERT_NOT_NULL( pAfter );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pBefore->_maxDistance, 1e-4f ); // 옛 정의는 아직 산다
    SW_EXPECT_NEAR_EQUAL( 5.0f, pAfter->_maxDistance, 1e-4f );
}

/**
 * @brief [InteractionTest] 상호작용 표를 고쳐 다시 읽으면 스마트 오브젝트가 다음 틱에 새 자리 정의를 쓰고, 이미 차지한 자리는 이어진다
 * @details 정의를 onPostLoad 에서만 찾으면 표를 고쳐도 다시 시작할 때까지 옛 자리 수로 돈다.
 */
SW_TEST_CASE( InteractionTest, EditedCatalogReachesSmartObjectSlots )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const string path = FileUtil::joinPath( test::makeTempPath( "smartreload" ), "bench.interactions.xml" );
    FileUtil::ensureParentDirectoryExists( path );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( path, R"(<Interactions><SmartObject id="Bench"><Slot id="Left" offset="-0.5,0,0" tags="Activity.Sit"/></SmartObject></Interactions>)" ) );

    AssetManager  resources;
    ModuleService service{};
    service.arrServices[internal::toRawServiceId( internal::ModuleServiceId::AssetManager )] = &resources;
    game::bindGameService( service );

    GameObjectManager     manager;
    GameObject*           pBench  = manager.createGameObject( hashed_string( "Bench" ) );
    GameObject*           pSitter = manager.createGameObject( hashed_string( "Sitter" ) );
    SmartObjectComponent* pSmart  = pBench != nullptr ? pBench->addComponent<SmartObjectComponent>() : nullptr;
    int32                 before  = -1;
    int32                 after   = -1;
    bool                  bKept   = false;
    if ( pSmart != nullptr && pSitter != nullptr )
    {
        pSmart->setCatalogPath( path );
        pSmart->setSmartObjectId( hashed_string( "Bench" ) );
        before = pSmart->getSlotCount();
        (void)pSmart->claimSlot( *pSitter, 0 );
        SW_EXPECT_TRUE( FileUtil::writeTextFile(
            path, R"(<Interactions><SmartObject id="Bench"><Slot id="Left" offset="-0.5,0,0" tags="Activity.Sit"/><Slot id="Right" offset="0.5,0,0" tags="Activity.Sit"/></SmartObject></Interactions>)" ) );
        IAssetCache* pCache = resources.findAssetCache( "InteractionCatalog" );
        if ( pCache != nullptr )
            pCache->reload( path, nullptr );
        pSmart->onTick( 0.0f );
        after = pSmart->getSlotCount();
        bKept = pSmart->findSlotOf( *pSitter ) == 0;
    }
    game::unbindGameService();

    SW_ASSERT_NOT_NULL( pSmart );
    SW_EXPECT_EQUAL( 1, before );
    SW_EXPECT_EQUAL( 2, after );
    SW_EXPECT_TRUE( bKept );
}

/**
 * @brief [InteractionTest] 하는 쪽은 등록된 대상(`ComponentRegistry`)에서 고른다 — 씬 전수 훑기로 고른 것과 자리마다 같고, 모듈을 내렸다 다시 붙여도 목록이 맞다
 * @details 후보를 등록부에서 모으고 닿지 않는 것을 미리 거르므로, 등록이 빠지거나(대상이 사라진다) 죽은 것이 남거나(해제된 메모리) 거르기가 고르기와
 *          다르면 여기서 갈린다. 동률은 오브젝트 id 로 가르므로 모은 순서와 무관하다.
 */
SW_TEST_CASE( InteractionTest, RegistryCandidatesMatchFullSceneScan )
{
    GameObjectManager manager;
    uint32            state    = 777u;
    const auto        nextUnit = [&state]()
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float32>( ( state >> 8 ) % 10000u ) / 10000.0f;
    };
    vector<InteractableComponent*> listInteractable;
    for ( uint32 index = 0; index < 300; ++index )
    {
        GameObject* pObject = manager.createGameObject( hashed_string( "Prop" ) );
        pObject->addComponent<SceneComponent>()->setLocalPosition( float3{ nextUnit() * 20.0f - 10.0f, 0.0f, nextUnit() * 20.0f - 10.0f } );
        if ( index % 6 != 0 )
            continue;
        InteractionDef def                   = InteractionTestInternal::makeDef( "Use", InteractionInputMode::Press );
        def._maxDistance                     = 1.5f + nextUnit() * 4.0f;
        def._maxAngle                        = nextUnit() < 0.5f ? 0.0f : 1.2f;
        InteractableComponent* pInteractable = pObject->addComponent<InteractableComponent>();
        pInteractable->setDefinition( def );
        pInteractable->setEnabled( nextUnit() > 0.2f );
        listInteractable.push_back( pInteractable );
    }
    GameObject*          pPlayer     = manager.createGameObject( hashed_string( "Player" ) );
    SceneComponent*      pPlayerRoot = pPlayer->addComponent<SceneComponent>();
    InteractorComponent* pInteractor = pPlayer->addComponent<InteractorComponent>();
    pInteractor->setEyeOffset( float3{} );
    SW_ASSERT_EQUAL( listInteractable.size(), manager.getComponentRegistry().getAll<InteractableComponent>().size() );

    /** @brief 전수 훑기로 고른 대상(없으면 무효 핸들)입니다 — 등록부 이전의 고르기 그대로. */
    const auto selectByScan = [&manager, pPlayer]()
    {
        InteractionViewer viewer;
        viewer._space    = InteractionSpace::Space3D;
        viewer._objectId = pPlayer->getObjectId();
        viewer._position = pPlayer->getPrimarySceneComponent()->getWorldPosition();
        viewer._forward  = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, pPlayer->getPrimarySceneComponent()->getWorldMatrix() );
        vector<InteractionCandidate> listCandidate;
        manager.forEachComponentOfType<InteractableComponent>( [&]( InteractableComponent* pInteractable )
        {
            const GameObject* pObject = pInteractable->getOwner();
            if ( pObject == pPlayer || pInteractable->isActive() == false || pInteractable->isAvailableFor( *pPlayer ) == false )
                return;
            InteractionCandidate candidate;
            candidate._position    = pObject->getPrimarySceneComponent()->getWorldPosition();
            candidate._objectId    = pObject->getObjectId();
            candidate._maxDistance = pInteractable->getDefinition()->_maxDistance;
            candidate._maxAngle    = pInteractable->getDefinition()->_maxAngle;
            candidate._priority    = pInteractable->getPriority();
            listCandidate.push_back( candidate );
        } );
        const int32 best = InteractionSelector::selectBest( viewer, listCandidate, nullptr );
        return best >= 0 ? GameObjectHandle::make( listCandidate[static_cast<size_t>( best )]._objectId ) : GameObjectHandle{};
    };

    manager.beginPlay();
    uint32 mismatches = 0;
    uint32 focused    = 0;
    for ( uint32 spot = 0; spot < 60; ++spot )
    {
        pPlayerRoot->setLocalPosition( float3{ nextUnit() * 20.0f - 10.0f, 0.0f, nextUnit() * 20.0f - 10.0f } );
        pPlayerRoot->setLocalRotation( float3{ 0.0f, nextUnit() * 6.28f, 0.0f } );
        InteractionTestInternal::tickFrames( manager, 1, pInteractor, false );
        const GameObjectHandle expected = selectByScan();
        mismatches += pInteractor->getFocus() == expected ? 0u : 1u;
        focused += expected.isValid() ? 1u : 0u;
    }
    SW_EXPECT_EQUAL( 0u, mismatches );
    SW_EXPECT_TRUE( focused > 5u ); // 고를 것이 있는 자리가 충분해야 "같다" 가 뜻이 있다

#if !defined( SW_SHIPPING )
    // 모듈 리로드(Dev 만 — 배포 구성에는 모듈을 내리는 길이 없다): 그 모듈의 컴포넌트를 모두 내리면 목록이 비고, 다시 붙이면 새 것만 든다.
    const TypeInfo* pType = InteractableComponent::StaticType();
    SW_ASSERT_NOT_NULL( pType );
    manager.endPlay();
    (void)manager.destroyComponentsOfModule( pType->_moduleName.c_str() );
    manager.processDeferredDestruction();
    SW_EXPECT_TRUE( manager.getComponentRegistry().getAll<InteractableComponent>().empty() );
    GameObject*            pDoor  = manager.createGameObject( hashed_string( "Door" ) );
    InteractableComponent* pAgain = pDoor->addComponent<InteractableComponent>();
    SW_ASSERT_NOT_NULL( pAgain );
    const ComponentRegistry::View<InteractableComponent> listAfter = manager.getComponentRegistry().getAll<InteractableComponent>();
    SW_ASSERT_EQUAL( size_t( 1 ), listAfter.size() );
    SW_EXPECT_TRUE( listAfter[0] == pAgain );
#endif
}
