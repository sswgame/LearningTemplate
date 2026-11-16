#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Asset/Cache/WeakInternTable.h"

// WeakInternTableTest — 키 → 약한 참조 표(공유 에셋 표 · 코드로 짓는 값 표가 쓴다): 놓인 값은 삭제자가 자기 칸을 지워 만료 칸이 쌓이지 않고,
// 넣기는 표를 훑지 않는다(예전에는 넣을 때마다 표 전체를 돌아 N 개 넣기가 N² 이었다). 떼어 낸 뒤 같은 키로 든 새 값은 옛 값이 사라져도 지워지지 않는다.

namespace
{
    struct FakeValue
    {
        int32 _id{ 0 };
    };

    using FakeTable = sw::WeakInternTable<int32, FakeValue>;

    /** @brief 소멸자에서 같은 표의 다른 값을 놓는 값입니다(칸 주인이 잠금을 푼 뒤 값을 지우는지 본다). */
    struct ChainValue
    {
        sw::shared_ptr<ChainValue> _pNext;
    };
} // namespace

/**
 * @brief [WeakInternTableTest] 5 만 키를 넣어도 넣기가 표를 훑지 않는다 — 쥔 채 넣고, 놓으면 칸이 모두 사라진다
 * @details 예전 구현은 넣을 때마다 표 전체를 돌아 5 만 키에 12 억 번 비교했다(초 단위). 시간 상한은 느린 디버그 빌드도 넉넉히 넘는 값이다.
 */
SW_TEST_CASE( WeakInternTableTest, InsertIsNotQuadratic )
{
    constexpr int32                       kKeyCount = 50'000;
    FakeTable                             table;
    sw::vector<sw::shared_ptr<FakeValue>> listHeld;
    listHeld.reserve( kKeyCount );

    const sw::Stopwatch watch;
    for ( int32 key = 0; key < kKeyCount; ++key )
        listHeld.push_back( table.insertOrGetLive( key, sw::make_shared<FakeValue>( FakeValue{ key } ) ) );
    SW_EXPECT_TRUE( watch.getElapsedMilliseconds() < 2'000 );

    SW_EXPECT_EQUAL( size_t( kKeyCount ), table.countLive() );
    SW_EXPECT_EQUAL( size_t( kKeyCount ), table.countSlots() );
    listHeld.clear();
    SW_EXPECT_EQUAL( size_t( 0 ), table.countSlots() ); // 놓인 값이 자기 칸을 지웠다 — 걷을 만료 칸이 없다
}

/**
 * @brief [WeakInternTableTest] 키가 계속 바뀌는 쪽(크기를 끌어 바꾸는 편집)이 칸을 쌓지 않는다
 */
SW_TEST_CASE( WeakInternTableTest, ChurningKeysLeaveNoSlots )
{
    FakeTable table;
    for ( int32 key = 0; key < 1'000; ++key )
    {
        const sw::shared_ptr<FakeValue> pValue = table.acquire( key, []( int32 k )
        { return sw::make_shared<FakeValue>( FakeValue{ k } ); } );
        SW_REQUIRE_TRUE( pValue != nullptr );
        SW_EXPECT_EQUAL( key, pValue->_id );
        SW_EXPECT_EQUAL( size_t( 1 ), table.countSlots() );
    }
    SW_EXPECT_EQUAL( size_t( 0 ), table.countSlots() );
}

/**
 * @brief [WeakInternTableTest] 같은 키는 같은 객체이고, 늦게 넣은 쪽은 먼저 든 것을 받는다
 */
SW_TEST_CASE( WeakInternTableTest, SharesWhileHeld )
{
    FakeTable                       table;
    const sw::shared_ptr<FakeValue> first  = table.insertOrGetLive( 7, sw::make_shared<FakeValue>( FakeValue{ 1 } ) );
    const sw::shared_ptr<FakeValue> second = table.insertOrGetLive( 7, sw::make_shared<FakeValue>( FakeValue{ 2 } ) );
    SW_EXPECT_TRUE( first.get() == second.get() );
    SW_EXPECT_EQUAL( 1, second->_id );
    SW_EXPECT_TRUE( table.findLive( 7 ).get() == first.get() );
}

/**
 * @brief [WeakInternTableTest] 떼어 낸 뒤 같은 키로 든 새 값은 옛 값을 놓아도 표에 남는다
 */
SW_TEST_CASE( WeakInternTableTest, DetachedOldValueKeepsNewSlot )
{
    FakeTable                 table;
    sw::shared_ptr<FakeValue> pOld = table.insertOrGetLive( 3, sw::make_shared<FakeValue>( FakeValue{ 1 } ) );
    table.erase( 3 );
    const sw::shared_ptr<FakeValue> pNew = table.insertOrGetLive( 3, sw::make_shared<FakeValue>( FakeValue{ 2 } ) );
    SW_EXPECT_TRUE( pOld.get() != pNew.get() );
    pOld.reset();
    SW_EXPECT_TRUE( table.findLive( 3 ).get() == pNew.get() );
    SW_EXPECT_EQUAL( size_t( 1 ), table.countSlots() );
}

/**
 * @brief [WeakInternTableTest] 표가 먼저 사라져도 값을 놓을 수 있고, 값의 소멸자가 같은 표의 값을 놓아도 잠금이 겹치지 않는다
 */
SW_TEST_CASE( WeakInternTableTest, ReleaseOutlivesTableAndNests )
{
    sw::shared_ptr<FakeValue> pSurvivor;
    {
        FakeTable table;
        pSurvivor = table.insertOrGetLive( 1, sw::make_shared<FakeValue>( FakeValue{ 9 } ) );
    }
    SW_EXPECT_EQUAL( 9, pSurvivor->_id );
    pSurvivor.reset();

    sw::WeakInternTable<int32, ChainValue> chainTable;
    sw::shared_ptr<ChainValue>             pHead = chainTable.insertOrGetLive( 1, sw::make_shared<ChainValue>() );
    pHead->_pNext                                = chainTable.insertOrGetLive( 2, sw::make_shared<ChainValue>() );
    SW_EXPECT_EQUAL( size_t( 2 ), chainTable.countSlots() );
    pHead.reset(); // 1 의 칸 주인이 잠금을 푼 뒤 값을 지우고, 그 값이 2 를 놓는다
    SW_EXPECT_EQUAL( size_t( 0 ), chainTable.countSlots() );
}
