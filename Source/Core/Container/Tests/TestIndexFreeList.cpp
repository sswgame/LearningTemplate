// IndexFreeList — 빈 슬롯 LIFO 재사용과 새 인덱스, 이미 빈 슬롯의 두 번째 반납 거절.
#include "pch.h"

#include "Core/Container/IndexFreeList.h"
#include "Core/Container/deque.h"
#include "Core/Testing/ScopedAssertCapture.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

/**
 * @brief [IndexFreeListTest] 빈 슬롯은 마지막에 반납된 것부터(LIFO) 다시 나가고, 없으면 등록 컨테이너 크기가 새 인덱스다
 * @details 등록 컨테이너는 `size()` 만 읽으므로 deque 도 된다.
 */
SW_TEST_CASE( IndexFreeListTest, ReusesLastReleasedSlotFirst )
{
    IndexFreeList freeList;
    uint32        popped{ 77u };
    SW_EXPECT_FALSE( freeList.tryPop( popped ) );
    SW_EXPECT_EQUAL( 77u, popped );

    deque<int32> listRegistered;
    listRegistered.push_back( 0 );
    listRegistered.push_back( 0 );
    listRegistered.push_back( 0 );
    SW_EXPECT_EQUAL( 3u, freeList.reserve( listRegistered ) );

    freeList.push( 0u );
    freeList.push( 2u );
    SW_EXPECT_EQUAL( 2u, freeList.reserve( listRegistered ) );
    SW_EXPECT_TRUE( freeList.tryPop( popped ) );
    SW_EXPECT_EQUAL( 0u, popped );
    SW_EXPECT_TRUE( freeList.empty() );

    // 저장본 되돌리기: 스택 순서(끝이 다음에 나간다)가 그대로다.
    freeList.restore( vector<uint32>{ 5u, 1u } );
    SW_EXPECT_EQUAL( size_t( 2 ), freeList.getIndices().size() );
    SW_EXPECT_EQUAL( 1u, freeList.reserve( listRegistered ) );
}

/**
 * @brief [IndexFreeListTest] 이미 빈 슬롯을 다시 반납하면 거절한다
 * @details 거절하지 않으면 프리리스트에 같은 인덱스가 두 번 들어가 다음 두 할당이 그 인덱스를 서로 다른 대상에 발급한다.
 *          Debug 에서는 단언이 멈추므로 단언 가로채기 안에서 부른다.
 */
SW_TEST_CASE( IndexFreeListTest, DoubleReleaseIsRejected )
{
    vector<uint32> listRegistered;
    IndexFreeList  freeList;

    const uint32 indexA = freeList.allocate( listRegistered, 100u );
    const uint32 indexB = freeList.allocate( listRegistered, 200u );
    SW_REQUIRE_TRUE( indexA != indexB );

    SW_EXPECT_EQUAL( 100u, freeList.release( listRegistered, indexA, 0u ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), freeList.size() );

    {
        test::ScopedAssertCapture assertCapture;
        SW_EXPECT_EQUAL( 0u, freeList.release( listRegistered, indexA, 0u ) );
        if constexpr ( test::ScopedAssertCapture::kAssertsAreActive )
            SW_EXPECT_EQUAL( 1u, assertCapture.getCount() );
    }
    SW_EXPECT_TRUE_MSG( freeList.size() == 1, "double release pushed the same index twice" );

    const uint32 reused = freeList.allocate( listRegistered, 300u );
    const uint32 fresh  = freeList.allocate( listRegistered, 400u );
    SW_EXPECT_EQUAL( indexA, reused );
    SW_EXPECT_TRUE_MSG( fresh != reused, "the same index was issued twice" );
    SW_EXPECT_TRUE( fresh != indexB );

    // 범위 밖은 조용히 무시한다(단언도 없다).
    SW_EXPECT_EQUAL( 0u, freeList.release( listRegistered, 9999u, 0u ) );
}
