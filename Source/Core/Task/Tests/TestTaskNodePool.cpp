// TaskNodePoolTest — 노드 풀의 재사용과 수명: 슬랩 경계, 돌려줄 때의 비우기(후속 참조 · 호출 대상), 스테이지 재사용과 강제 정리, 매니저가 먼저 사라질 때의 소유 넘김, 병렬 그룹의 힙 폴백 경로, 미완료 태스크 진단.
// 풀의 주인 매니저는 nullptr 로 둔다 - 노드는 그때 매니저 없이 곧바로 풀로 돌아간다(`TaskNode::release`). 풀은 큐와 그룹 저장소를 품어 크므로 힙에 둔다.
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Task/TaskNode.h"
#include "Core/Task/TaskNodePool.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

namespace
{
    struct TaskNodePoolTestUtil
    {
        static constexpr uint32 kSlabSize = 64; ///< 풀의 슬랩 크기(`TaskNodePool::kSlabSize`, 비공개)

        /** @brief 제출된(`_bScheduled`) 이름 있는 노드를 꺼냅니다. */
        static TaskNode* allocateSubmitted( TaskNodePool& pool, string_view name, StageNode* pStage = nullptr )
        {
            TaskNode* pNode = pool.allocate();
            pNode->setName( name );
            pNode->_parentStage = pStage;
            pNode->_bScheduled.store( true, std::memory_order_release );
            return pNode;
        }
    };
} // namespace

/**
 * @brief [TaskNodePoolTest] 꺼낸 노드는 기본 상태다 - 참조 하나, 제출 의존 하나, 본문 하나, 깃발 없음, 이 풀 소속
 */
SW_TEST_CASE( TaskNodePoolTest, AllocateReturnsDefaultState )
{
    unique_ptr<TaskNodePool> pPool = make_unique<TaskNodePool>( nullptr );
    TaskNodePool&            pool  = *pPool;
    TaskNode*                pNode = pool.allocate();
    SW_REQUIRE_NOT_NULL( pNode );
    SW_EXPECT_EQUAL( 1, pNode->_refCount.load() );
    SW_EXPECT_EQUAL( 1, pNode->_unresolvedDependencies.load() );
    SW_EXPECT_EQUAL( 1, pNode->_pendingChildren.load() );
    SW_EXPECT_FALSE( pNode->_bScheduled.load() );
    SW_EXPECT_FALSE( pNode->_bCancelled.load() );
    SW_EXPECT_FALSE( pNode->_bParallelGroupParent.load() );
    SW_EXPECT_NULL( pNode->_pParent.load() );
    SW_EXPECT_TRUE( pNode->_affinity == TaskThreadAffinity::Any );
    SW_EXPECT_TRUE( pNode->_priority == TaskPriority::Normal );
    SW_EXPECT_TRUE( pNode->_pPool == &pool );
    pNode->release();
}

/**
 * @brief [TaskNodePoolTest] 한 슬랩의 노드가 모두 다른 주소로 나가고, 돌아온 노드를 새 슬랩보다 먼저 내주며, 다 쓰면 슬랩을 늘린다
 */
SW_TEST_CASE( TaskNodePoolTest, ReusesReturnedNodeBeforeGrowing )
{
    unique_ptr<TaskNodePool> pPool = make_unique<TaskNodePool>( nullptr );
    TaskNodePool&            pool  = *pPool;
    vector<TaskNode*>        listNode;
    for ( uint32 index = 0; index < TaskNodePoolTestUtil::kSlabSize; ++index )
    {
        listNode.push_back( pool.allocate() );
    }
    for ( size_t lhs = 0; lhs < listNode.size(); ++lhs )
    {
        for ( size_t rhs = lhs + 1; rhs < listNode.size(); ++rhs )
        {
            SW_EXPECT_TRUE( listNode[lhs] != listNode[rhs] );
        }
    }

    TaskNode* const pReturned = listNode[5];
    pReturned->_bCancelled.store( true );
    pReturned->_priority = TaskPriority::High;
    pReturned->release();
    SW_EXPECT_EQUAL( 0, pReturned->_refCount.load() ); // 자유 노드는 0 이다(`detachOwner` 가 이것으로 가린다)
    SW_REQUIRE_TRUE( pool.allocate() == pReturned );
    SW_EXPECT_FALSE( pReturned->_bCancelled.load() ); // 다시 나갈 때 앞 태스크의 상태가 남지 않는다
    SW_EXPECT_TRUE( pReturned->_priority == TaskPriority::Normal );

    TaskNode* const pGrown = pool.allocate(); // 자유 노드가 없다 - 새 슬랩
    SW_REQUIRE_NOT_NULL( pGrown );
    SW_EXPECT_TRUE( pGrown->_pPool == &pool );
    for ( TaskNode* pNode : listNode )
    {
        SW_EXPECT_TRUE( pNode != pGrown );
    }

    pGrown->release();
    for ( TaskNode* pNode : listNode )
    {
        pNode->release();
    }
}

/**
 * @brief [TaskNodePoolTest] 돌려줄 때 호출 대상을 파괴하고, 후속과 숨은 후속의 참조를 놓고, 닫힌 후속 목록을 다시 연다 - 다음 태스크가 앞 태스크의 간선을 물려받지 않는다
 */
SW_TEST_CASE( TaskNodePoolTest, DeallocateClearsCallableAndReleasesSuccessors )
{
    unique_ptr<TaskNodePool> pPool      = make_unique<TaskNodePool>( nullptr );
    TaskNodePool&            pool       = *pPool;
    TaskNode*                pNode      = pool.allocate();
    TaskNode*                pSuccessor = pool.allocate();
    TaskNode*                pHidden    = pool.allocate();

    pSuccessor->retain();
    SW_REQUIRE_TRUE( pNode->_successors.tryPushBack( pSuccessor ) );
    pHidden->retain();
    pNode->_pAnyOfSuccessor = pHidden;
    pNode->_callable.emplace<ParallelGroupLaunch>();
    pNode->setName( "named" );
    pNode->_successors.closeAndForEach( []( TaskNode* ) {} );
    SW_EXPECT_FALSE( pNode->_successors.tryPushBack( pSuccessor ) ); // 닫혔다

    pNode->release();
    SW_EXPECT_EQUAL( 1, pSuccessor->_refCount.load() );
    SW_EXPECT_EQUAL( 1, pHidden->_refCount.load() );
    SW_EXPECT_TRUE( std::holds_alternative<std::monostate>( pNode->_callable ) );
    SW_EXPECT_NULL( pNode->_pAnyOfSuccessor );
    SW_EXPECT_EQUAL( 0u, pNode->_successors._count );
    SW_EXPECT_FALSE( pNode->_successors._bClosed ); // 다시 열렸다

    pSuccessor->release();
    pHidden->release();
}

/**
 * @brief [TaskNodePoolTest] 마지막 참조가 놓인 스테이지를 다음 할당이 다시 내주고, 조인 수와 참조를 처음 상태로 되돌린다
 */
SW_TEST_CASE( TaskNodePoolTest, StageIsReusedWithFreshState )
{
    unique_ptr<TaskNodePool> pPool  = make_unique<TaskNodePool>( nullptr );
    TaskNodePool&            pool   = *pPool;
    StageNode*               pStage = pool.allocateStage();
    SW_REQUIRE_NOT_NULL( pStage );
    SW_EXPECT_TRUE( pStage->_pPool == &pool );
    SW_EXPECT_EQUAL( 1, pStage->_refCount.load() );
    (void)pStage->_join.addPending( 3 );

    StageNode* pOther = pool.allocateStage();
    SW_EXPECT_TRUE( pOther != pStage );
    pOther->release();

    pStage->release();
    SW_EXPECT_EQUAL( 0, pStage->_refCount.load() );
    StageNode* pReused = pool.allocateStage();
    SW_EXPECT_TRUE( pReused == pStage ); // 자유 목록은 나중에 돌아온 것부터 내준다
    SW_EXPECT_EQUAL( 0u, pReused->_join.getPending() );
    SW_EXPECT_EQUAL( 1, pReused->_refCount.load() );
    pReused->release();
}

/**
 * @brief [TaskNodePoolTest] 강제 정리는 남은 태스크가 있는 스테이지의 자기 참조만 놓는다 - 핸들의 참조는 그대로이고, 남은 것 없는 스테이지는 건드리지 않는다
 */
SW_TEST_CASE( TaskNodePoolTest, ResetAllStagesReleasesOnlyPendingSelfReference )
{
    unique_ptr<TaskNodePool> pPool    = make_unique<TaskNodePool>( nullptr );
    TaskNodePool&            pool     = *pPool;
    StageNode*               pPending = pool.allocateStage();
    StageNode*               pIdle    = pool.allocateStage();
    pPending->retain(); // 남은 태스크가 있는 동안 스테이지가 스스로 잡는 참조
    (void)pPending->_join.addPending( 2 );

    pool.resetAllStages();

    SW_EXPECT_EQUAL( 0u, pPending->_join.getPending() );
    SW_EXPECT_EQUAL( 1, pPending->_refCount.load() ); // 핸들 몫이 남는다 - 0 으로 덮으면 핸들이 나중에 재사용된 스테이지의 수를 내린다
    SW_EXPECT_EQUAL( 1, pIdle->_refCount.load() );

    pPending->release();
    pIdle->release();
}

/**
 * @brief [TaskNodePoolTest] 남은 참조가 없으면 `detachOwner` 가 true 다 - 돌아온 노드와 스테이지는 세지 않으며 소유자가 지운다
 */
SW_TEST_CASE( TaskNodePoolTest, DetachWithNoReferencesLeavesPoolToOwner )
{
    unique_ptr<TaskNodePool> pPool = make_unique<TaskNodePool>( nullptr );
    pPool->allocate()->release();
    pPool->allocateStage()->release();
    SW_EXPECT_TRUE( pPool->detachOwner() );
    SW_EXPECT_NULL( pPool->getOwner() );
}

/**
 * @brief [TaskNodePoolTest] 참조가 남은 채 떼면 풀이 마지막 참조까지 산다 - 노드 · 스테이지가 돌아올 때마다 수를 내리고, 자유 목록에 넣지 않으며, 마지막이 풀을 지운다
 * @details 풀의 소멸 자체는 메모리 추적기와 새니타이저가 본다(마지막 뒤에는 풀을 만질 수 없다).
 */
SW_TEST_CASE( TaskNodePoolTest, DetachWithReferencesHandsPoolToLastReference )
{
    unique_ptr<TaskNodePool> pPool      = make_unique<TaskNodePool>( nullptr );
    TaskNode*                pNode      = pPool->allocate();
    TaskNode*                pSuccessor = pPool->allocate();
    StageNode*               pStage     = pPool->allocateStage();
    pPool->allocate()->release(); // 돌아온 노드는 세지 않는다

    pSuccessor->retain();
    SW_REQUIRE_TRUE( pNode->_successors.tryPushBack( pSuccessor ) );
    pSuccessor->release(); // 이제 후속의 참조는 pNode 의 목록이 쥔다

    SW_REQUIRE_FALSE( pPool->detachOwner() );
    TaskNodePool* const pOrphan = pPool.release(); // 소유가 핸들에 넘어갔다(`~TaskManager` 와 같다)
    SW_EXPECT_NULL( pOrphan->getOwner() );

    pStage->release();
    SW_EXPECT_NULL( pOrphan->getOwner() ); // 노드 둘이 남아 풀이 살아 있다
    pNode->release();                      // 후속까지 함께 돌아와 수가 0 이 되고, 앞 노드의 몫이 끝난 뒤에 풀이 지워진다
}

/**
 * @brief [TaskNodePoolTest] 병렬 그룹은 풀이 바닥나면 힙에서 나오고, 어디서 왔든 돌려준 뒤 다시 꺼낼 수 있다(바닥난 풀이 실패가 아니다)
 */
SW_TEST_CASE( TaskNodePoolTest, GroupsOverflowToHeapAndReturn )
{
    unique_ptr<TaskNodePool> pPool = make_unique<TaskNodePool>( nullptr );
    TaskNodePool&            pool  = *pPool;
    vector<ParallelGroup*>   listGroup;
    for ( uint32 index = 0; index < kParallelGroupPoolCapacity + 3; ++index )
    {
        ParallelGroup* pGroup = pool.allocateGroup();
        SW_REQUIRE_NOT_NULL( pGroup );
        listGroup.push_back( pGroup );
    }
    for ( ParallelGroup* pGroup : listGroup )
    {
        pool.deallocateGroup( pGroup );
    }
    pool.deallocateGroup( nullptr ); // 아무 일도 없다

    ParallelGroup* pAgain = pool.allocateGroup();
    SW_EXPECT_NOT_NULL( pAgain );
    pool.deallocateGroup( pAgain );
}

/**
 * @brief [TaskNodePoolTest] 진단은 제출됐고 본문이 안 끝난 이름 있는 노드만 잡고, 스테이지를 주면 그 스테이지 안에서만 찾으며, 참조 수를 그대로 둔다
 */
SW_TEST_CASE( TaskNodePoolTest, DescribeFirstUnfinishedFindsOnlySubmittedNamedNodes )
{
    unique_ptr<TaskNodePool> pPool  = make_unique<TaskNodePool>( nullptr );
    TaskNodePool&            pool   = *pPool;
    StageNode*               pStage = pool.allocateStage();
    StageNode*               pOther = pool.allocateStage();
    SW_EXPECT_TRUE( pool.describeFirstUnfinished( nullptr ).empty() ); // 노드가 없다

    TaskNode* pNotSubmitted = pool.allocate();
    pNotSubmitted->setName( "notSubmitted" );
    TaskNode* pUnnamed = TaskNodePoolTestUtil::allocateSubmitted( pool, "" );
    TaskNode* pDone    = TaskNodePoolTestUtil::allocateSubmitted( pool, "done" );
    pDone->_pendingChildren.store( 0 );
    SW_EXPECT_TRUE( pool.describeFirstUnfinished( nullptr ).empty() );
    SW_EXPECT_EQUAL( 1, pUnnamed->_refCount.load() ); // 잡았다가 거른 노드의 참조를 놓았다

    TaskNode* pInStage = TaskNodePoolTestUtil::allocateSubmitted( pool, "inStage", pStage );
    SW_EXPECT_TRUE( pool.describeFirstUnfinished( nullptr ) == "inStage" );
    SW_EXPECT_TRUE( pool.describeFirstUnfinished( pStage ) == "inStage" );
    SW_EXPECT_TRUE( pool.describeFirstUnfinished( pOther ).empty() );
    SW_EXPECT_EQUAL( 1, pInStage->_refCount.load() );

    pInStage->release(); // 돌아온 노드는 이름이 지워지고 참조가 없어 잡히지 않는다
    SW_EXPECT_TRUE( pool.describeFirstUnfinished( nullptr ).empty() );

    pNotSubmitted->release();
    pUnnamed->release();
    pDone->release();
    pStage->release();
    pOther->release();
}
