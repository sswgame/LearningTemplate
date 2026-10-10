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

    manager.tick( 0.016f );

    SW_EXPECT_EQUAL( 1u, pLeader->_subTickCount );
    SW_EXPECT_EQUAL( 1u, pFollower->_subTickCount );
    SW_EXPECT_TRUE( isNear( pFollower->_observedLocal, sw::float3{ 5.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( isNear( pFollower->_observedWorld, sw::float3{ 15.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( isNear( pFollower->getLocalPosition(), sw::float3{ 5.0f, 1.0f, 0.0f } ) );

    // 다음 프레임에 리더가 옮기면 그 프레임에 따라온다(한 프레임 늦지 않는다).
    pLeader->_offset = sw::float3{ 7.0f, 0.0f, 0.0f };
    manager.tick( 0.016f );
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
    manager.tick( 0.016f );

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

    manager.tick( 0.016f );

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

    manager.tick( 0.016f );

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
    manager.tick( 0.016f );
    manager.destroyObject( pSpawned );
    manager.tick( 0.016f );
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

    manager.tick( 0.016f );

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
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( 0u, pB->_subTickCount );
    SW_EXPECT_EQUAL( 1u, pC->_subTickCount );

    // 선행 조건(A)이 사라지면 C 는 기다릴 것 없이 계속 돈다(스테이지는 다시 지어진다).
    const uint32 buildBefore = manager.getTickRegistry().getStageBuildCount();
    pC->_pWatched            = nullptr;
    manager.destroyObject( pObjectA );
    manager.tick( 0.016f );
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( 3u, pC->_subTickCount );
    SW_EXPECT_TRUE( manager.getTickRegistry().getStageBuildCount() > buildBefore );
    SW_EXPECT_TRUE( isNear( pC->getLocalPosition(), sw::float3{ 0.0f, 0.0f, 1.0f } ) );
}

/**
 * @brief [TickPrerequisiteTest] 전체 재구축(핫 리로드 · 타입 재바인딩이 부른다)과 선행 조건 컴포넌트의 교체 뒤에도 사슬이 돈다
 * @details 핫 리로드는 등록부 전체를 다시 짓고(`markTickStagesDirty`) 컴포넌트를 새 id 로 다시 만든다. 새로 만든 쪽의 핸들로 선행 조건을 다시
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
    manager.tick( 0.016f );

    manager.markTickStagesDirty();
    pLeader->_offset = sw::float3{ 3.0f, 0.0f, 0.0f };
    manager.tick( 0.016f );
    SW_EXPECT_TRUE( isNear( pFollower->_observedLocal, sw::float3{ 3.0f, 0.0f, 0.0f } ) );

    // 리더 컴포넌트를 새로 만든다(새 컴포넌트 id). 옛 핸들의 선행 조건은 비고, 새 핸들을 걸면 다시 같은 프레임에 따라온다.
    sw::GameObject* pLeaderObj = pLeader->getOwner();
    pFollower->_pWatched       = nullptr;
    SW_ASSERT_TRUE( pLeaderObj->removeComponent( pLeader ) );
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( 3u, pFollower->_subTickCount );

    sw::MockSubTickMoverComponent* pNewLeader = pLeaderObj->addComponent<sw::MockSubTickMoverComponent>();
    pNewLeader->_offset                       = sw::float3{ 5.0f, 0.0f, 0.0f };
    const sw::SubTickHandle newLeaderTick     = pNewLeader->registerSubTick( sw::TickGroup::DuringPhysics, kMove );
    pFollower->_pWatched                      = pNewLeader;
    SW_ASSERT_TRUE( pFollower->addSubTickPrerequisite( kMove, newLeaderTick ) );
    manager.tick( 0.016f );
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

    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( 1u, registry.getStageItemCount() );
    SW_EXPECT_TRUE( isNear( pFollower->_observedLocal, sw::float3{ 8.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( isNear( pFollower->getLocalPosition(), sw::float3{ 8.0f, 0.0f, 1.0f } ) );

    // 떼면 다시 보통 길 — 다른 오브젝트가 같은 그룹에서 쓴 값은 틱 전 값으로 읽힌다.
    SW_ASSERT_TRUE( pFollower->removeSubTickPrerequisite( kMove, leaderTick ) );
    pLeader->_tickLocalPos = sw::float3{ 9.0f, 0.0f, 0.0f };
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( 0u, registry.getStageItemCount() );
    SW_EXPECT_TRUE( isNear( pFollower->_observedLocal, sw::float3{ 8.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_FALSE( pFollower->removeSubTickPrerequisite( kMove, leaderTick ) );
}
