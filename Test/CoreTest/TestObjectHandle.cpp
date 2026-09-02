#include "pch.h"

#include "Core/Container/HandleTable.h"
#include "Core/Container/ObjectHandle.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

SW_TEST_CASE( ObjectHandle, PackedRoundTripAndInvalid )
{
    ObjectHandle invalid;
    SW_EXPECT_FALSE( invalid.isValid() );
    SW_EXPECT_EQUAL( 0u, invalid.packed() );

    const ObjectHandle handle = ObjectHandle::make( 7u, 3u );
    SW_EXPECT_TRUE( handle.isValid() );
    SW_EXPECT_EQUAL( 7u, handle.index() );
    SW_EXPECT_EQUAL( 3u, handle.generation() );
    SW_EXPECT_EQUAL( handle, ObjectHandle::fromPacked( handle.packed() ) );
    SW_EXPECT_NOT_EQUAL( handle, ObjectHandle::make( 7u, 4u ) );
}

SW_TEST_CASE( HandleTable, GenerationInvalidatesStaleHandles )
{
    HandleTable<uint32> table;
    const ObjectHandle  first = table.insert( 42u );
    SW_EXPECT_TRUE( first.isValid() );
    uint32* slot = table.get( first );
    SW_ASSERT_NOT_NULL( slot );
    SW_EXPECT_EQUAL( 42u, *slot );

    uint32 taken{ 0 };
    SW_EXPECT_TRUE( table.take( first, taken ) );
    SW_EXPECT_EQUAL( 42u, taken );
    SW_EXPECT_TRUE( table.get( first ) == nullptr );

    const ObjectHandle second = table.insert( 99u );
    SW_EXPECT_EQUAL( first.index(), second.index() );
    SW_EXPECT_NOT_EQUAL( first, second );
    SW_EXPECT_TRUE( table.get( first ) == nullptr );
    uint32* reused = table.get( second );
    SW_ASSERT_NOT_NULL( reused );
    SW_EXPECT_EQUAL( 99u, *reused );
}

SW_TEST_CASE( HandleTable, MultiSlotAndFreeListRecycling )
{
    HandleTable<int32> table;
    ObjectHandle       h0 = table.insert( 100 );
    ObjectHandle       h1 = table.insert( 200 );
    ObjectHandle       h2 = table.insert( 300 );
    ObjectHandle       h3 = table.insert( 400 );

    SW_EXPECT_EQUAL( 100, *table.get( h0 ) );
    SW_EXPECT_EQUAL( 200, *table.get( h1 ) );
    SW_EXPECT_EQUAL( 300, *table.get( h2 ) );
    SW_EXPECT_EQUAL( 400, *table.get( h3 ) );

    // h1(index 1)과 h3(index 3) 삭제
    table.erase( h1 );
    table.erase( h3 );

    SW_EXPECT_TRUE( table.get( h1 ) == nullptr );
    SW_EXPECT_TRUE( table.get( h3 ) == nullptr );
    SW_EXPECT_TRUE( table.get( h0 ) != nullptr );
    SW_EXPECT_TRUE( table.get( h2 ) != nullptr );

    // 새로운 아이템 삽입 (프리 리스트에서 재활용)
    ObjectHandle hRecycle1 = table.insert( 500 );
    ObjectHandle hRecycle2 = table.insert( 600 );

    // 재활용된 슬롯은 인덱스는 같으나 generation이 증가하여 고유함
    SW_EXPECT_TRUE( hRecycle1.index() == h3.index() || hRecycle1.index() == h1.index() );
    SW_EXPECT_TRUE( hRecycle2.index() == h3.index() || hRecycle2.index() == h1.index() );
    SW_EXPECT_TRUE( table.get( h1 ) == nullptr );
    SW_EXPECT_TRUE( table.get( h3 ) == nullptr );
    SW_EXPECT_EQUAL( 500, *table.get( hRecycle1 ) );
    SW_EXPECT_EQUAL( 600, *table.get( hRecycle2 ) );
}

SW_TEST_CASE( HandleTable, ForEachAndIteration )
{
    HandleTable<int32> table;
    ObjectHandle       h0 = table.insert( 10 );
    ObjectHandle       h1 = table.insert( 20 );
    ObjectHandle       h2 = table.insert( 30 );
    table.erase( h1 );

    // 1) forEach
    int32 sum{ 0 };
    table.forEach( [&sum]( int32 val )
    {
        sum += val;
    } );
    SW_EXPECT_EQUAL( 40, sum );

    // 2) forEachHandle (non-const)
    uint32 visitedCount{ 0 };
    table.forEachHandle( [&visitedCount]( ObjectHandle handle, int32& val )
    {
        SW_EXPECT_TRUE( handle.isValid() );
        val += 1;
        ++visitedCount;
    } );
    SW_EXPECT_EQUAL( 2u, visitedCount );
    SW_EXPECT_EQUAL( 11, *table.get( h0 ) );
    SW_EXPECT_EQUAL( 31, *table.get( h2 ) );

    // 3) forEachHandle (const)
    const HandleTable<int32>& constTable = table;
    int32                     constSum{ 0 };
    constTable.forEachHandle( [&constSum]( ObjectHandle handle, const int32& val )
    {
        SW_EXPECT_TRUE( handle.isValid() );
        constSum += val;
    } );
    SW_EXPECT_EQUAL( 42, constSum );
}

SW_TEST_CASE( HandleTable, ClearAndInvalidHandleSafety )
{
    HandleTable<int32> table;
    ObjectHandle       h0 = table.insert( 10 );
    ObjectHandle       h1 = table.insert( 20 );

    // 1) 무효 핸들 접근
    ObjectHandle invalidHandle{};
    SW_EXPECT_TRUE( table.get( invalidHandle ) == nullptr );
    int32 takenVal{ 0 };
    SW_EXPECT_FALSE( table.take( invalidHandle, takenVal ) );

    // 2) 범위 밖의 인덱스 핸들
    ObjectHandle outOfRange = ObjectHandle::make( 9999u, 1u );
    SW_EXPECT_TRUE( table.get( outOfRange ) == nullptr );

    // 3) clear
    table.clear();
    SW_EXPECT_TRUE( table.get( h0 ) == nullptr );
    SW_EXPECT_TRUE( table.get( h1 ) == nullptr );
}

SW_TEST_CASE( HandleTable, StressGenerationRolloverAndRandomChurn )
{
    HandleTable<int32>   table;
    constexpr size_t     kCount = 500;
    vector<ObjectHandle> listHandles;
    listHandles.reserve( kCount );

    // 1) 500개 연속 할당
    for ( size_t index = 0; index < kCount; ++index )
    {
        listHandles.push_back( table.insert( static_cast<int32>( index * 10 ) ) );
    }

    // 2) 짝수 인덱스 250개 해제 (징검다리 해제)
    for ( size_t index = 0; index < kCount; index += 2 )
    {
        table.erase( listHandles[index] );
        SW_EXPECT_TRUE( table.get( listHandles[index] ) == nullptr );
    }

    // 홀수 인덱스는 여전히 유효해야 함
    for ( size_t index = 1; index < kCount; index += 2 )
    {
        int32* pVal = table.get( listHandles[index] );
        SW_ASSERT_NOT_NULL( pVal );
        SW_EXPECT_EQUAL( static_cast<int32>( index * 10 ), *pVal );
    }

    // 3) 250개 신규 재할당 (프리리스트 재사용 및 Generation 증가 검증)
    vector<ObjectHandle> listReusedHandles;
    listReusedHandles.reserve( kCount / 2 );
    for ( size_t index = 0; index < kCount / 2; ++index )
    {
        listReusedHandles.push_back( table.insert( static_cast<int32>( 10000 + index ) ) );
    }

    // 이전 짝수 핸들은 여전히 nullptr이어야 함 (stale handle 무효화)
    for ( size_t index = 0; index < kCount; index += 2 )
    {
        SW_EXPECT_TRUE( table.get( listHandles[index] ) == nullptr );
    }

    // 신규 할당된 핸들은 정상 조회되어야 함
    for ( size_t index = 0; index < kCount / 2; ++index )
    {
        int32* pVal = table.get( listReusedHandles[index] );
        SW_ASSERT_NOT_NULL( pVal );
        SW_EXPECT_EQUAL( static_cast<int32>( 10000 + index ), *pVal );
    }
}
