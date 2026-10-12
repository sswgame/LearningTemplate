#include "pch.h"

#include "Core/Container/IndexFreeList.h"
#include "Core/Container/deque.h"

#include "TestFramework/TestFramework.h"

/**
 * @brief [IndexFreeListTest] 빈 슬롯은 마지막에 반납된 것부터(LIFO) 다시 나가고, 없으면 등록 컨테이너 크기가 새 인덱스다
 * @details RTS 유닛 슬롯처럼 재사용 순서가 결과(유닛 id)에 드러나는 곳이 이 순서에 기댄다. 등록 컨테이너는 `size()` 만 읽으므로
 *          deque 도 된다.
 */
SW_TEST_CASE( IndexFreeListTest, ReusesLastReleasedSlotFirst )
{
    sw::vector<uint32> listFree;
    uint32             popped{ 77u };
    SW_EXPECT_FALSE( sw::tryPopFreeListIndex( listFree, popped ) );
    SW_EXPECT_EQUAL( 77u, popped );

    sw::deque<int32> listRegistered;
    listRegistered.push_back( 0 );
    listRegistered.push_back( 0 );
    listRegistered.push_back( 0 );
    SW_EXPECT_EQUAL( 3u, sw::reserveFreeListIndex( listRegistered, listFree ) );

    listFree.push_back( 0u );
    listFree.push_back( 2u );
    SW_EXPECT_EQUAL( 2u, sw::reserveFreeListIndex( listRegistered, listFree ) );
    SW_EXPECT_TRUE( sw::tryPopFreeListIndex( listFree, popped ) );
    SW_EXPECT_EQUAL( 0u, popped );
    SW_EXPECT_TRUE( listFree.empty() );
}

/**
 * @brief [IndexFreeListTest] 이미 빈 슬롯을 다시 반납하면 **거절한다**
 * @details 거절하지 않으면 프리리스트에 같은 인덱스가 **두 번** 들어가고, 다음 두 번의 할당이
 *          그 인덱스를 **서로 다른 대상에 발급한다**. bindless 라면 한쪽이 다른 쪽의 디스크립터를 덮어쓴다.
 *
 *          가드는 이 도우미 한 자리에 있다 — 쓰는 곳마다 손으로 적으면 어느 한 곳에서 가드가 빠져
 *          그곳의 이중 해제만 통과한다. Debug 에서는 단언이 멈추므로 단언 가로채기 안에서 부른다.
 */
SW_TEST_CASE( IndexFreeListTest, DoubleReleaseIsRejected )
{
    sw::vector<uint32> listRegistered;
    sw::vector<uint32> listFree;

    const uint32 indexA = sw::allocateFreeListIndex( listRegistered, listFree, 100u );
    const uint32 indexB = sw::allocateFreeListIndex( listRegistered, listFree, 200u );
    SW_ASSERT_TRUE( indexA != indexB );

    // 한 번 반납하면 그 자리가 프리리스트로 간다.
    SW_EXPECT_EQUAL( 100u, sw::releaseFreeListIndex( listRegistered, listFree, indexA, 0u ) );
    SW_ASSERT_EQUAL( size_t( 1 ), listFree.size() );

    // **두 번째 반납은 거절된다** — 안 그러면 프리리스트에 같은 인덱스가 둘이 된다.
    {
        test::ScopedAssertCapture assertCapture;
        SW_EXPECT_EQUAL( 0u, sw::releaseFreeListIndex( listRegistered, listFree, indexA, 0u ) );
        if ( test::ScopedAssertCapture::kAssertsAreActive )
            SW_EXPECT_EQUAL( 1u, assertCapture.getCount() );
    }
    SW_EXPECT_TRUE_MSG( listFree.size() == 1, "이중 해제가 프리리스트에 같은 인덱스를 두 번 넣었습니다" );

    // 그래서 다음 두 할당이 서로 다른 인덱스를 받는다.
    const uint32 reused = sw::allocateFreeListIndex( listRegistered, listFree, 300u );
    const uint32 fresh  = sw::allocateFreeListIndex( listRegistered, listFree, 400u );
    SW_EXPECT_EQUAL( indexA, reused );
    SW_EXPECT_TRUE_MSG( fresh != reused, "같은 인덱스가 두 대상에 발급됐습니다" );
    SW_EXPECT_TRUE( fresh != indexB );

    // 범위 밖은 조용히 무시한다(단언도 없다).
    SW_EXPECT_EQUAL( 0u, sw::releaseFreeListIndex( listRegistered, listFree, 9999u, 0u ) );
}
