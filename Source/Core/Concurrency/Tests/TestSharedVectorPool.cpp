#include "pch.h"

#include "Core/Concurrency/SharedVectorPool.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

/**
 * @brief [SharedVectorPoolTest] 누가 쥔 배열은 다시 주지 않고, 모두 놓은 배열은 비워서 다시 준다
 */
SW_TEST_CASE( SharedVectorPoolTest, ReusesOnlyReleasedVectors )
{
    SharedVectorPool<uint32> pool;

    shared_ptr<vector<uint32>> first = pool.acquire();
    first->push_back( 7u );
    const vector<uint32>* const pFirst = first.get();

    shared_ptr<const vector<uint32>> published = first; // 패킷이 쥔 사본
    first.reset();
    shared_ptr<vector<uint32>> second = pool.acquire();
    SW_EXPECT_TRUE( second.get() != pFirst );
    SW_EXPECT_EQUAL( 2u, pool.getSlotCount() );
    SW_EXPECT_EQUAL( size_t{ 1 }, published->size() ); // 쥔 쪽의 값은 그대로다

    published.reset();
    shared_ptr<vector<uint32>> third = pool.acquire();
    SW_EXPECT_TRUE( third.get() == pFirst );
    SW_EXPECT_TRUE( third->empty() );
    SW_EXPECT_EQUAL( 2u, pool.getSlotCount() );
}
