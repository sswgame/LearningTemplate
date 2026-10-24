/**
 * @file TestTickPrerequisite.cpp
 * @brief 서브틱 선행 조건 — 앞 스테이지가 옮긴 트랜스폼을 뒤 스테이지가 같은 프레임에 읽는가, 사슬 밖 오브젝트는 보통 길로 도는가.
 */
#include "pch.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "EngineTest/TestGameObjectMocks.h"

#include "TestFramework/TestFramework.h"

namespace
{
    constexpr uint32 kMove = sw::MockSubTickMoverComponent::kMoveSubTick;

    /** @brief 서브틱 하나를 등록한 무버 오브젝트를 만듭니다. 쓰는 값은 (감시 대상의 로컬 위치) + @p offset 입니다. */
    sw::MockSubTickMoverComponent* createMover( sw::GameObjectManager& manager, const utf8* pName, const sw::float3& offset, sw::SceneComponent* pWatched,
                                                sw::SubTickHandle& outHandle, sw::TickGroup group = sw::TickGroup::DuringPhysics )
    {
        sw::GameObject*                pObj   = manager.createGameObject( sw::hashed_string( pName ) );
        sw::MockSubTickMoverComponent* pMover = pObj->addComponent<sw::MockSubTickMoverComponent>();
        pMover->_offset                       = offset;
        pMover->_pWatched                     = pWatched;
        outHandle                             = pMover->registerSubTick( group, kMove );
        return pMover;
    }

    bool isNear( const sw::float3& left, const sw::float3& right )
    {
        return sw::float3::getDistanceSquared( left, right ) < 1e-8f;
    }
} // namespace

/**
 * @brief [TickPrerequisiteTest] 앞 스테이지(선행 조건)가 옮긴 로컬 · 월드 위치를 뒤 스테이지가 같은 프레임에 읽는다
 * @details 틱 중의 트랜스폼 쓰기는 대기 칸 · 쓰기 큐로 가고 단계 끝에 적용된다. 선행 조건 스테이지 사이에서 적용하지 않으면 뒤 스테이지는
 *          틱 전 값을 읽어 순서를 세운 뜻이 트랜스폼에는 없다. 감시 대상은 부모 아래에 둬 월드 값(플러시)까지 본다.
 */
SW_TEST_CASE( TickPrerequisiteTest, LaterStageReadsEarlierStageTransformInTheSameFrame )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

    sw::GameObject*     pParentObj = manager.createGameObject( sw::hashed_string( "LeaderParent" ) );
    sw::SceneComponent* pParent    = pParentObj->addComponent<sw::SceneComponent>();
    pParent->setLocalPosition( sw::float3{ 10.0f, 0.0f, 0.0f } );

    sw::SubTickHandle              leaderTick{};
    sw::SubTickHandle              followerTick{};
    sw::MockSubTickMoverComponent* pLeader   = createMover( manager, "Leader", sw::float3{ 5.0f, 0.0f, 0.0f }, nullptr, leaderTick );
    sw::MockSubTickMoverComponent* pFollower = createMover( manager, "Follower", sw::float3{ 0.0f, 1.0f, 0.0f }, pLeader, followerTick );
    SW_ASSERT_TRUE( pLeader->attachToComponent( pParent ) );
    SW_ASSERT_TRUE( pFollower->addSubTickPrerequisite( kMove, leaderTick ) );

    manager.update( 0.016f );

    SW_EXPECT_EQUAL( 1u, pLeader->_subTickCount );
    SW_EXPECT_EQUAL( 1u, pFollower->_subTickCount );
    SW_EXPECT_TRUE( isNear( pFollower->_observedLocal, sw::float3{ 5.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( isNear( pFollower->_observedWorld, sw::float3{ 15.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( isNear( pFollower->getLocalPosition(), sw::float3{ 5.0f, 1.0f, 0.0f } ) );

    // 다음 프레임에 리더가 옮기면 그 프레임에 따라온다(한 프레임 늦지 않는다).
    pLeader->_offset = sw::float3{ 7.0f, 0.0f, 0.0f };
    manager.update( 0.016f );
    SW_EXPECT_TRUE( isNear( pFollower->_observedWorld, sw::float3{ 17.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( isNear( pFollower->getLocalPosition(), sw::float3{ 7.0f, 1.0f, 0.0f } ) );
}

/**
 * @brief [TickPrerequisiteTest] 사슬 A→B→C 의 깊이 3 — 스테이지마다 앞의 결과를 보고, 적용은 기다리는 스테이지 앞에서만(마지막 뒤에는 없다)
 */
SW_TEST_CASE( TickPrerequisiteTest, ChainOfThreeSeesEveryEarlierStage )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

    sw::SubTickHandle              tickA{};
    sw::SubTickHandle              tickB{};
    sw::SubTickHandle              tickC{};
    sw::MockSubTickMoverComponent* pA = createMover( manager, "ChainA", sw::float3{ 1.0f, 0.0f, 0.0f }, nullptr, tickA );
    sw::MockSubTickMoverComponent* pB = createMover( manager, "ChainB", sw::float3{ 0.0f, 1.0f, 0.0f }, pA, tickB );
    sw::MockSubTickMoverComponent* pC = createMover( manager, "ChainC", sw::float3{ 0.0f, 0.0f, 1.0f }, pB, tickC );
    // 등록은 거꾸로 — 순서는 선행 조건이 정한다.
    SW_ASSERT_TRUE( pC->addSubTickPrerequisite( kMove, tickB ) );
    SW_ASSERT_TRUE( pB->addSubTickPrerequisite( kMove, tickA ) );

    const uint32 applyBefore = manager.getStageTransformApplyCount();
    manager.update( 0.016f );

    SW_EXPECT_TRUE( isNear( pB->_observedLocal, sw::float3{ 1.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( isNear( pC->_observedLocal, sw::float3{ 1.0f, 1.0f, 0.0f } ) );
    SW_EXPECT_TRUE( isNear( pC->getLocalPosition(), sw::float3{ 1.0f, 1.0f, 1.0f } ) );
    // B 앞 · C 앞의 두 번 — A 앞(기다리는 것이 없다)과 C 뒤(기다리는 스테이지가 없다)에는 적용하지 않는다.
    SW_EXPECT_EQUAL( applyBefore + 2u, manager.getStageTransformApplyCount() );
}

/**
 * @brief [TickPrerequisiteTest] 앞 스테이지가 미룬 계층 변경(KeepWorld 부착)보다 뒤에 부른 쓰기는 그 변경 뒤에 적용된다
 * @details 스테이지 경계의 적용은 트랜스폼만 앞당긴다. 계층 변경이 미뤄진 단계에서 그 뒤 쓰기를 먼저 적용하면, 단계 끝의 `KeepWorld` 부착이
 *          쓴 로컬 값을 붙이기 전 월드에서 다시 구해 덮는다 — 쓴 값(1,2,3)이 아니라 부모만큼 밀린 값이 남는다.
 */
SW_TEST_CASE( TickPrerequisiteTest, WritesAfterADeferredAttachApplyAfterIt )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

    sw::GameObject*     pParentObj = manager.createGameObject( sw::hashed_string( "AttachParent" ) );
    sw::SceneComponent* pParent    = pParentObj->addComponent<sw::SceneComponent>();
    pParent->setLocalPosition( sw::float3{ 10.0f, 0.0f, 0.0f } );

    sw::SubTickHandle              attacherTick{};
    sw::SubTickHandle              childTick{};
    sw::MockSubTickMoverComponent* pAttacher = createMover( manager, "Attacher", sw::float3{}, nullptr, attacherTick );
    sw::MockSubTickMoverComponent* pChild    = createMover( manager, "Child", sw::float3{ 1.0f, 2.0f, 3.0f }, nullptr, childTick );
    pAttacher->_bWriteOnSubTick              = SW_FALSE;
    pAttacher->_pAttachChild                 = pChild;
    pAttacher->_pAttachParent                = pParent;
    SW_ASSERT_TRUE( pChild->addSubTickPrerequisite( kMove, attacherTick ) );
    // 자식의 쓰기 뒤에 기다리는 스테이지가 하나 더 있어야 경계 적용이 그 쓰기를 앞당길 기회가 생긴다.
    sw::SubTickHandle              watcherTick{};
    sw::MockSubTickMoverComponent* pWatcher = createMover( manager, "Watcher", sw::float3{}, pChild, watcherTick );
    pWatcher->_bWriteOnSubTick              = SW_FALSE;
    SW_ASSERT_TRUE( pWatcher->addSubTickPrerequisite( kMove, childTick ) );

    manager.update( 0.016f );

    SW_EXPECT_EQUAL( pParent, pChild->getParent() );
    // 계층 변경이 미뤄진 단계라 감시자 앞에서도 적용하지 않았다 — 감시자는 틱 전 값을 읽는다.
    SW_EXPECT_TRUE( isNear( pWatcher->_observedLocal, sw::float3{} ) );
    SW_EXPECT_TRUE( isNear( pChild->getLocalPosition(), sw::float3{ 1.0f, 2.0f, 3.0f } ) );
    SW_EXPECT_TRUE( isNear( pChild->getWorldPosition(), sw::float3{ 11.0f, 2.0f, 3.0f } ) );
}

/**
 * @brief [TickPrerequisiteTest] 선행 조건이 씬에 있어도 사슬 밖 오브젝트는 보통 길(오브젝트 칸)로 돌고, 스테이지는 선행 조건을 가진 항목만 돈다
 * @details 선행 조건이 하나라도 있으면 씬 전체가 스테이지 길로 가면 사슬 밖 오브젝트의 자기 쓰기까지 쓰기 큐로 간다. 사슬 밖 무버는 칸 목록에
 *          그대로 있고, 선행 조건이 없는 리더도 보통 길에서 돈다 — 팔로워는 그 뒤 스테이지에서 리더의 이번 프레임 위치를 본다.
 */
SW_TEST_CASE( TickPrerequisiteTest, ObjectsOutsideTheChainStayOnTheObjectPath )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();
    const sw::TickRegistry& registry = manager.getTickRegistry();

    constexpr uint32                        kMoverCount = 40;
    sw::vector<sw::MockTickSceneComponent*> listMover;
    for ( uint32 index = 0; index < kMoverCount; ++index )
    {
        sw::GameObject*             pObj   = manager.createGameObject( sw::hashed_string( "OutsideMover" ) );
        sw::MockTickSceneComponent* pMover = pObj->addComponent<sw::MockTickSceneComponent>();
        pMover->_bWriteLocalOnTick         = SW_TRUE;
        pMover->_tickLocalPos              = sw::float3{ static_cast<float32>( index ), 2.0f, 0.0f };
        listMover.push_back( pMover );
    }
    sw::SubTickHandle              leaderTick{};
    sw::SubTickHandle              followerTick{};
    sw::MockSubTickMoverComponent* pLeader   = createMover( manager, "PathLeader", sw::float3{ 4.0f, 0.0f, 0.0f }, nullptr, leaderTick );
    sw::MockSubTickMoverComponent* pFollower = createMover( manager, "PathFollower", sw::float3{ 0.0f, 1.0f, 0.0f }, pLeader, followerTick );
    SW_ASSERT_TRUE( pFollower->addSubTickPrerequisite( kMove, leaderTick ) );

    manager.update( 0.016f );

    const uint32 kDuring = static_cast<uint32>( sw::TickGroup::DuringPhysics );
    SW_EXPECT_EQUAL( kMoverCount + 1u, static_cast<uint32>( registry.getEntries( kDuring ).size() ) );
    SW_EXPECT_EQUAL( 1u, registry.getStageItemCount() );
    // 스테이지 길도 자기 오브젝트를 "틱하는 오브젝트" 로 적어 자기 쓰기가 대기 칸으로 간다.
    SW_EXPECT_EQUAL( static_cast<const sw::GameObject*>( pFollower->getOwner() ), pFollower->_pTickingObjectSeen );
    SW_EXPECT_EQUAL( static_cast<const sw::GameObject*>( pLeader->getOwner() ), pLeader->_pTickingObjectSeen );
    SW_EXPECT_TRUE( isNear( pFollower->_observedLocal, sw::float3{ 4.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( isNear( pFollower->getLocalPosition(), sw::float3{ 4.0f, 1.0f, 0.0f } ) );
    uint32 wrongCount = 0;
    for ( const sw::MockTickSceneComponent* pMover : listMover )
    {
        if ( isNear( pMover->getWorldPosition(), pMover->_tickLocalPos ) == false )
            ++wrongCount;
    }
    SW_EXPECT_EQUAL( 0u, wrongCount );

    // 사슬 밖 오브젝트를 만들고 지워도 스테이지는 다시 짓지 않는다.
    const uint32    buildBefore = registry.getStageBuildCount();
    sw::GameObject* pSpawned    = manager.createGameObject( sw::hashed_string( "OutsideSpawn" ) );
    pSpawned->addComponent<sw::MockTickSceneComponent>();
    manager.update( 0.016f );
    manager.destroyObject( pSpawned );
    manager.update( 0.016f );
    SW_EXPECT_EQUAL( buildBefore, registry.getStageBuildCount() );
    SW_EXPECT_EQUAL( 3u, pFollower->_subTickCount );
}

/**
 * @brief [TickPrerequisiteTest] 보통 길의 선행 조건이 뒤 그룹에 있으면 기다리는 쪽이 그 그룹으로 옮겨 그 결과를 같은 프레임에 본다
 */
SW_TEST_CASE( TickPrerequisiteTest, ObjectPathPrerequisiteInALaterGroupMovesTheDependent )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

    sw::SubTickHandle              leaderTick{};
    sw::SubTickHandle              followerTick{};
    sw::MockSubTickMoverComponent* pLeader = createMover( manager, "LateLeader", sw::float3{ 0.0f, 0.0f, 6.0f }, nullptr, leaderTick, sw::TickGroup::PostUpdate );
    sw::MockSubTickMoverComponent* pFollower =
        createMover( manager, "EarlyFollower", sw::float3{ 1.0f, 0.0f, 0.0f }, pLeader, followerTick, sw::TickGroup::PrePhysics );
    SW_ASSERT_TRUE( pFollower->addSubTickPrerequisite( kMove, leaderTick ) );

    manager.update( 0.016f );

    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( manager.getTickRegistry().getStages().size() ) );
    SW_EXPECT_EQUAL( static_cast<uint8>( sw::TickGroup::PostUpdate ), manager.getTickRegistry().getStages()[0]._group );
    SW_EXPECT_TRUE( isNear( pFollower->_observedLocal, sw::float3{ 0.0f, 0.0f, 6.0f } ) );
    SW_EXPECT_TRUE( isNear( pFollower->getLocalPosition(), sw::float3{ 1.0f, 0.0f, 6.0f } ) );
}

/**
 * @brief [TickPrerequisiteTest] 파괴 — 틱 중에 파괴 표시된 기다리는 쪽은 그 틱부터 돌지 않고, 선행 조건이 사라진 쪽은 계속 돈다
 */
SW_TEST_CASE( TickPrerequisiteTest, DestroyedItemsLeaveTheChainSafely )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

    sw::SubTickHandle              tickA{};
    sw::SubTickHandle              tickB{};
    sw::SubTickHandle              tickC{};
    sw::MockSubTickMoverComponent* pA = createMover( manager, "DestroyA", sw::float3{ 1.0f, 0.0f, 0.0f }, nullptr, tickA );
    sw::MockSubTickMoverComponent* pB = createMover( manager, "DestroyB", sw::float3{ 0.0f, 1.0f, 0.0f }, pA, tickB );
    sw::MockSubTickMoverComponent* pC = createMover( manager, "DestroyC", sw::float3{ 0.0f, 0.0f, 1.0f }, pA, tickC );
    SW_ASSERT_TRUE( pB->addSubTickPrerequisite( kMove, tickA ) );
    SW_ASSERT_TRUE( pC->addSubTickPrerequisite( kMove, tickA ) );
    sw::GameObject* pObjectA = pA->getOwner();

    // A 가 B 를 파괴한다 — B 는 같은 틱의 뒤 스테이지에 있지만 삭제 표시가 서서 돌지 않는다.
    pA->_pDestroyObject = pB->getOwner();
    manager.update( 0.016f );
    SW_EXPECT_EQUAL( 0u, pB->_subTickCount );
    SW_EXPECT_EQUAL( 1u, pC->_subTickCount );

    // 선행 조건(A)이 사라지면 C 는 기다릴 것 없이 계속 돈다(스테이지는 다시 지어진다).
    const uint32 buildBefore = manager.getTickRegistry().getStageBuildCount();
    pC->_pWatched            = nullptr;
    manager.destroyObject( pObjectA );
    manager.update( 0.016f );
    manager.update( 0.016f );
    SW_EXPECT_EQUAL( 3u, pC->_subTickCount );
    SW_EXPECT_TRUE( manager.getTickRegistry().getStageBuildCount() > buildBefore );
    SW_EXPECT_TRUE( isNear( pC->getLocalPosition(), sw::float3{ 0.0f, 0.0f, 1.0f } ) );
}

/**
 * @brief [TickPrerequisiteTest] 전체 재구축(핫 리로드 · 타입 재바인딩이 부른다)과 선행 조건 컴포넌트의 교체 뒤에도 사슬이 돈다
 * @details 핫 리로드는 레지스트리 전체를 다시 짓고(`markTickStagesDirty`) 컴포넌트를 새 id 로 다시 만든다. 새로 만든 쪽의 핸들로 선행 조건을 다시
 *          걸기 전까지 옛 핸들은 아무것도 가리키지 않는다 — 기다리는 쪽은 기다릴 것 없이 돈다(죽은 포인터를 따라가지 않는다).
 */
SW_TEST_CASE( TickPrerequisiteTest, ChainSurvivesRebuildAndReplacedPrerequisite )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

    sw::SubTickHandle              leaderTick{};
    sw::SubTickHandle              followerTick{};
    sw::MockSubTickMoverComponent* pLeader   = createMover( manager, "ReloadLeader", sw::float3{ 2.0f, 0.0f, 0.0f }, nullptr, leaderTick );
    sw::MockSubTickMoverComponent* pFollower = createMover( manager, "ReloadFollower", sw::float3{ 0.0f, 1.0f, 0.0f }, pLeader, followerTick );
    SW_ASSERT_TRUE( pFollower->addSubTickPrerequisite( kMove, leaderTick ) );
    manager.update( 0.016f );

    manager.markTickStagesDirty();
    pLeader->_offset = sw::float3{ 3.0f, 0.0f, 0.0f };
    manager.update( 0.016f );
    SW_EXPECT_TRUE( isNear( pFollower->_observedLocal, sw::float3{ 3.0f, 0.0f, 0.0f } ) );

    // 리더 컴포넌트를 새로 만든다(새 컴포넌트 id). 옛 핸들의 선행 조건은 비고, 새 핸들을 걸면 다시 같은 프레임에 따라온다.
    sw::GameObject* pLeaderObj = pLeader->getOwner();
    pFollower->_pWatched       = nullptr;
    SW_ASSERT_TRUE( pLeaderObj->removeComponent( pLeader ) );
    manager.update( 0.016f );
    SW_EXPECT_EQUAL( 3u, pFollower->_subTickCount );

    sw::MockSubTickMoverComponent* pNewLeader = pLeaderObj->addComponent<sw::MockSubTickMoverComponent>();
    pNewLeader->_offset                       = sw::float3{ 5.0f, 0.0f, 0.0f };
    const sw::SubTickHandle newLeaderTick     = pNewLeader->registerSubTick( sw::TickGroup::DuringPhysics, kMove );
    pFollower->_pWatched                      = pNewLeader;
    SW_ASSERT_TRUE( pFollower->addSubTickPrerequisite( kMove, newLeaderTick ) );
    manager.update( 0.016f );
    SW_EXPECT_TRUE( isNear( pFollower->_observedLocal, sw::float3{ 5.0f, 0.0f, 0.0f } ) );
}

/**
 * @brief [TickPrerequisiteTest] 다른 컴포넌트의 주 틱(`getTickHandle`)을 선행 조건으로 걸면 그 `onTick` 이 옮긴 자리를 같은 프레임에 읽고, 떼면 보통 길로 돌아간다
 * @details 언리얼 `AddTickPrerequisiteComponent` 의 자리다. 대상이 바뀌면 떼고 다시 거는 모양(`removeSubTickPrerequisite`)도 본다.
 */
SW_TEST_CASE( TickPrerequisiteTest, MainTickOfAnotherComponentIsAPrerequisite )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();
    const sw::TickRegistry& registry = manager.getTickRegistry();

    sw::GameObject*             pLeaderObj = manager.createGameObject( sw::hashed_string( "MainTickLeader" ) );
    sw::MockTickSceneComponent* pLeader    = pLeaderObj->addComponent<sw::MockTickSceneComponent>();
    pLeader->_bWriteLocalOnTick            = SW_TRUE;
    pLeader->_tickLocalPos                 = sw::float3{ 8.0f, 0.0f, 0.0f };

    sw::SubTickHandle              followerTick{};
    sw::MockSubTickMoverComponent* pFollower  = createMover( manager, "MainTickFollower", sw::float3{ 0.0f, 0.0f, 1.0f }, pLeader, followerTick );
    const sw::SubTickHandle        leaderTick = pLeader->getTickHandle();
    SW_EXPECT_EQUAL( 0u, leaderTick._subTickID );
    SW_EXPECT_EQUAL( pLeaderObj->getObjectID(), leaderTick._objectID );
    SW_ASSERT_TRUE( pFollower->addSubTickPrerequisite( kMove, leaderTick ) );

    manager.update( 0.016f );
    SW_EXPECT_EQUAL( 1u, registry.getStageItemCount() );
    SW_EXPECT_TRUE( isNear( pFollower->_observedLocal, sw::float3{ 8.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( isNear( pFollower->getLocalPosition(), sw::float3{ 8.0f, 0.0f, 1.0f } ) );

    // 떼면 다시 보통 길 — 다른 오브젝트가 같은 그룹에서 쓴 값은 틱 전 값으로 읽힌다.
    SW_ASSERT_TRUE( pFollower->removeSubTickPrerequisite( kMove, leaderTick ) );
    pLeader->_tickLocalPos = sw::float3{ 9.0f, 0.0f, 0.0f };
    manager.update( 0.016f );
    SW_EXPECT_EQUAL( 0u, registry.getStageItemCount() );
    SW_EXPECT_TRUE( isNear( pFollower->_observedLocal, sw::float3{ 8.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_FALSE( pFollower->removeSubTickPrerequisite( kMove, leaderTick ) );
}

/**
 * @brief [TickPrerequisiteTest] 팔로워를 만들고 지우고 껐다 켜는 변동 속에서도 기다리는 쪽은 매 프레임 한 번만 돌고 선행 조건의 같은 프레임 값을 읽는다
 * @details 말단 팔로워(아무도 기다리지 않고 선행 조건이 모두 보통 길)의 스폰 · 파괴는 스테이지를 다시 짓지 않고 끼우고 뺀다(`getStageDeltaCount`).
 *          다른 팔로워를 기다리는 팔로워가 섞이면 그 대상이 가리켜진 오브젝트가 되어 전체 재구축으로 돌아간다. 두 길 모두 같은 결과여야 한다.
 *          리더 하나는 뒤 그룹(PostPhysics)에 둬 팔로워가 그 그룹으로 옮겨 가는 길도 지나게 한다.
 */
SW_TEST_CASE( TickPrerequisiteTest, FollowerChurnKeepsOrderUnderIncrementalStageUpdates )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

    sw::SubTickHandle              earlyTick{};
    sw::SubTickHandle              lateTick{};
    sw::MockSubTickMoverComponent* pEarlyLeader = createMover( manager, "EarlyLeader", sw::float3{ 3.0f, 0.0f, 0.0f }, nullptr, earlyTick );
    sw::MockSubTickMoverComponent* pLateLeader  = createMover( manager, "LateLeader", sw::float3{ 0.0f, 0.0f, 7.0f }, nullptr, lateTick, sw::TickGroup::PostPhysics );

    struct Follower
    {
        sw::MockSubTickMoverComponent* _pMover{ nullptr };
        sw::MockSubTickMoverComponent* _pWatchedFollower{ nullptr }; ///< 다른 팔로워를 기다리면 그 팔로워다
        sw::SubTickHandle              _handle{};                    ///< 이 팔로워의 서브틱 핸들(다른 팔로워가 선행 조건으로 건다)
        sw::MockSubTickMoverComponent* _pLeader{ nullptr };
        uint32                         _expectedCount{ 0 };
        bool                           _bActive{ true };
        bool                           _bChain{ false };
    };
    sw::vector<Follower> listFollower;
    uint32               seed       = 12345;
    const auto           nextRandom = [&seed]( uint32 bound )
    {
        seed = seed * 1664525u + 1013904223u;
        return ( seed >> 8 ) % bound;
    };

    const auto spawn = [&]()
    {
        Follower          follower{};
        const bool        bChain = listFollower.empty() == false && nextRandom( 6 ) == 0;
        sw::SubTickHandle handle{};
        if ( bChain )
        {
            Follower& target                      = listFollower[nextRandom( static_cast<uint32>( listFollower.size() ) )];
            follower._pWatchedFollower            = target._pMover;
            follower._pLeader                     = target._pLeader;
            sw::MockSubTickMoverComponent* pMover = createMover( manager, "ChainFollower", sw::float3{ 0.0f, 0.0f, 0.0f }, target._pMover, handle );
            follower._pMover                      = pMover;
            follower._bChain                      = true;
            SW_EXPECT_TRUE( pMover->addSubTickPrerequisite( kMove, target._handle ) );
        }
        else
        {
            const bool bLate                      = nextRandom( 2 ) == 0;
            follower._pLeader                     = bLate ? pLateLeader : pEarlyLeader;
            sw::MockSubTickMoverComponent* pMover = createMover( manager, "Follower", sw::float3{ 0.0f, 1.0f, 0.0f }, follower._pLeader, handle );
            follower._pMover                      = pMover;
            SW_EXPECT_TRUE( pMover->addSubTickPrerequisite( kMove, bLate ? lateTick : earlyTick ) );
        }
        follower._handle = handle;
        listFollower.push_back( follower );
    };

    for ( uint32 index = 0; index < 20; ++index )
    {
        spawn();
    }
    manager.update( 0.016f );
    for ( Follower& follower : listFollower )
    {
        follower._expectedCount = 1;
    }

    for ( uint32 frame = 0; frame < 300; ++frame )
    {
        const uint32 spawnThisFrame = nextRandom( 4 );
        for ( uint32 index = 0; index < spawnThisFrame; ++index )
        {
            spawn();
        }
        const uint32 destroyThisFrame = nextRandom( 3 );
        for ( uint32 index = 0; index < destroyThisFrame && listFollower.empty() == false; ++index )
        {
            const size_t victim = nextRandom( static_cast<uint32>( listFollower.size() ) );
            // 이 팔로워를 기다리는 팔로워가 있으면 기다리던 쪽은 계속 돈다(선행 조건이 사라진 것 — 감시 값은 이 테스트가 보지 않는다).
            for ( Follower& other : listFollower )
            {
                if ( other._pWatchedFollower == listFollower[victim]._pMover )
                {
                    other._pWatchedFollower  = nullptr;
                    other._pMover->_pWatched = nullptr; // 지워질 컴포넌트를 틱에서 읽지 않게
                }
            }
            manager.destroyObject( listFollower[victim]._pMover->getOwner() );
            listFollower.erase( listFollower.begin() + static_cast<ptrdiff_t>( victim ) );
        }
        if ( listFollower.empty() == false && nextRandom( 3 ) == 0 )
        {
            Follower& toggled = listFollower[nextRandom( static_cast<uint32>( listFollower.size() ) )];
            toggled._bActive  = toggled._bActive == false;
            toggled._pMover->getOwner()->setActive( toggled._bActive );
        }

        manager.update( 0.016f );

        uint32 wrongCount = 0;
        for ( Follower& follower : listFollower )
        {
            if ( follower._bActive )
                ++follower._expectedCount;
            if ( follower._pMover->_subTickCount != follower._expectedCount )
                ++wrongCount;
            if ( follower._bActive == false || follower._bChain )
                continue;
            const sw::float3 expected = follower._pLeader == pLateLeader ? sw::float3{ 0.0f, 0.0f, 7.0f } : sw::float3{ 3.0f, 0.0f, 0.0f };
            if ( isNear( follower._pMover->_observedLocal, expected ) == false || isNear( follower._pMover->_observedWorld, expected ) == false )
                ++wrongCount;
        }
        SW_ASSERT_EQUAL( 0u, wrongCount );
    }
    SW_EXPECT_TRUE( manager.getTickRegistry().getStageDeltaCount() > 0u );
}

/**
 * @brief [TickPrerequisiteTest] 증분으로 끼운 스테이지 배치가 전체 재구축이 만든 배치와 같다(스테이지 안의 순서까지)
 * @details 결과값(틱 횟수 · 관측값)만 보면 증분 경로가 스테이지를 다른 자리에 넣어도 드러나지 않는다. 같은 오브젝트 집합에서 `markTickStagesDirty` 로 전체 재구축을 세워
 *          두 배치를 비교한다. 스테이지 안의 순서가 다르면 같은 씬이 길에 따라 다른 순서로 돌아 결정성이 깨지므로 (그룹, 레벨, 오브젝트 id 차례)를 스테이지마다 비교한다.
 *          예전 오브젝트를 껐다 켜서 새 id 가 아닌 항목이 스테이지 중간에 끼는 경우를 만든다.
 */
SW_TEST_CASE( TickPrerequisiteTest, IncrementalStageLayoutMatchesFullRebuild )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

    sw::SubTickHandle              earlyTick{};
    sw::SubTickHandle              lateTick{};
    sw::MockSubTickMoverComponent* pEarlyLeader = createMover( manager, "EarlyLeader", sw::float3{ 3.0f, 0.0f, 0.0f }, nullptr, earlyTick );
    sw::MockSubTickMoverComponent* pLateLeader  = createMover( manager, "LateLeader", sw::float3{ 0.0f, 0.0f, 7.0f }, nullptr, lateTick, sw::TickGroup::PostPhysics );

    // 비어 있지 않은 스테이지마다 (그룹, 레벨, 안의 오브젝트 id 차례) — 비운 스테이지는 증분 경로에만 남을 수 있어 건너뛴다.
    const auto captureLayout = [&manager]()
    {
        sw::vector<sw::vector<uint64>> listLayout;
        for ( const sw::TickStage& stage : manager.getTickRegistry().getStages() )
        {
            if ( stage._listItemObjectID.empty() )
                continue;
            sw::vector<uint64> listStage{ static_cast<uint64>( stage._group ), static_cast<uint64>( stage._level ) };
            listStage.insert( listStage.end(), stage._listItemObjectID.begin(), stage._listItemObjectID.end() );
            listLayout.push_back( std::move( listStage ) );
        }
        return listLayout;
    };

    uint32     seed       = 777;
    const auto nextRandom = [&seed]( uint32 bound )
    {
        seed = seed * 1664525u + 1013904223u;
        return ( seed >> 8 ) % bound;
    };
    sw::vector<sw::MockSubTickMoverComponent*> listFollower;
    for ( uint32 frame = 0; frame < 60; ++frame )
    {
        const uint32 spawnThisFrame = 1 + nextRandom( 3 );
        for ( uint32 index = 0; index < spawnThisFrame; ++index )
        {
            const bool                     bLate = nextRandom( 2 ) == 0;
            sw::SubTickHandle              handle{};
            sw::MockSubTickMoverComponent* pMover = createMover( manager, "Follower", sw::float3{ 0.0f, 1.0f, 0.0f }, bLate ? pLateLeader : pEarlyLeader, handle );
            SW_ASSERT_TRUE( pMover->addSubTickPrerequisite( kMove, bLate ? lateTick : earlyTick ) );
            listFollower.push_back( pMover );
        }
        if ( listFollower.size() > 8 && nextRandom( 2 ) == 0 )
        {
            const size_t victim = nextRandom( static_cast<uint32>( listFollower.size() ) );
            manager.destroyObject( listFollower[victim]->getOwner() );
            listFollower.erase( listFollower.begin() + static_cast<ptrdiff_t>( victim ) );
        }
        if ( listFollower.empty() == false && nextRandom( 2 ) == 0 )
        {
            // 예전(작은 id) 오브젝트를 껐다 켠다 — 다시 켜질 때 항목이 스테이지 끝이 아닌 중간에 끼어야 한다.
            sw::GameObject* pToggled = listFollower[nextRandom( static_cast<uint32>( listFollower.size() ) )]->getOwner();
            pToggled->setActive( pToggled->isActive() == false );
        }
        manager.update( 0.016f );
    }
    SW_ASSERT_TRUE( manager.getTickRegistry().getStageDeltaCount() > 0u );

    const auto incremental = captureLayout();
    manager.markTickStagesDirty();
    manager.update( 0.016f );
    const auto rebuilt = captureLayout();
    SW_EXPECT_EQUAL( rebuilt.size(), incremental.size() );
    SW_EXPECT_TRUE( rebuilt == incremental );
}

/**
 * @brief [TickPrerequisiteTest] 스테이지가 비어 있을 때 처음 들어온 팔로워도 증분으로 스테이지가 생기고, 뒤 그룹 리더의 같은 프레임 값을 읽는다
 * @details 리더가 뒤 그룹(PostPhysics)의 보통 길에서 돌면 팔로워는 그 그룹으로 옮겨 가고, 그 스테이지는 돌기 전에 틱 중 쓰기를 적용해야 한다.
 *          전체 재구축 없이(빌드 횟수 그대로) 끼운 스테이지도 같아야 한다.
 */
SW_TEST_CASE( TickPrerequisiteTest, FirstFollowerAddedIncrementallyAppliesTransformsBeforeItsStage )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

    sw::SubTickHandle              leaderTick{};
    sw::SubTickHandle              followerTick{};
    sw::MockSubTickMoverComponent* pLeader = createMover( manager, "LateLeader", sw::float3{ 0.0f, 0.0f, 7.0f }, nullptr, leaderTick, sw::TickGroup::PostPhysics );
    manager.update( 0.016f ); // 의존 오브젝트가 없다 — 스테이지는 비어 있다
    SW_ASSERT_EQUAL( 0u, static_cast<uint32>( manager.getTickRegistry().getStages().size() ) );

    const uint32                   buildBefore = manager.getTickRegistry().getStageBuildCount();
    sw::MockSubTickMoverComponent* pFollower   = createMover( manager, "EarlyFollower", sw::float3{ 1.0f, 0.0f, 0.0f }, pLeader, followerTick );
    SW_ASSERT_TRUE( pFollower->addSubTickPrerequisite( kMove, leaderTick ) );
    manager.update( 0.016f );

    SW_EXPECT_EQUAL( buildBefore, manager.getTickRegistry().getStageBuildCount() );
    SW_EXPECT_EQUAL( 1u, manager.getTickRegistry().getStageDeltaCount() );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( manager.getTickRegistry().getStages().size() ) );
    SW_EXPECT_EQUAL( static_cast<uint8>( sw::TickGroup::PostPhysics ), manager.getTickRegistry().getStages()[0]._group );
    SW_EXPECT_TRUE( isNear( pFollower->_observedLocal, sw::float3{ 0.0f, 0.0f, 7.0f } ) );
    SW_EXPECT_TRUE( isNear( pFollower->_observedWorld, sw::float3{ 0.0f, 0.0f, 7.0f } ) ); // 월드 값은 적용을 거쳐야 보인다
    SW_EXPECT_TRUE( isNear( pFollower->getLocalPosition(), sw::float3{ 1.0f, 0.0f, 7.0f } ) );

    // 지우면 스테이지에서 빠진다(빈 스테이지는 남아도 돌 것이 없다).
    manager.destroyObject( pFollower->getOwner() );
    manager.update( 0.016f );
    SW_EXPECT_EQUAL( 0u, manager.getTickRegistry().getStageItemCount() );
    SW_EXPECT_EQUAL( buildBefore, manager.getTickRegistry().getStageBuildCount() );
}
