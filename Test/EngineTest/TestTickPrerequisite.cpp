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
