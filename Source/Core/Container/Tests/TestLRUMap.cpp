// LRUMap — 쓴 순서(앞이 가장 오래 안 씀)를 함께 드는 맵: 순서 새로 하기, 걸으며 지우기, 밀집 배열 끝 엔트리가 옮겨 올 때의 순서.
#include "pch.h"

#include "Core/Container/LRUMap.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 쓴 순서대로 키를 모읍니다. */
    vector<int32> collectKeys( const LRUMap<int32, int32>& map )
    {
        vector<int32> listKey;
        for ( const auto& entry : map )
        {
            listKey.push_back( entry._key );
        }
        return listKey;
    }
} // namespace

/**
 * @brief [LRUMapTest] 넣은 순서가 쓴 순서의 처음이고, `touch` · 다시 넣기가 끝으로 옮긴다. `find` 는 순서를 바꾸지 않는다
 */
SW_TEST_CASE( LRUMapTest, TouchAndAssignMoveToNewest )
{
    LRUMap<int32, int32> map;
    SW_EXPECT_TRUE( map.empty() );
    SW_EXPECT_TRUE( map.begin() == map.end() );
    map.insertOrAssign( 1, 10 );
    map.insertOrAssign( 2, 20 );
    map.insertOrAssign( 3, 30 );
    SW_EXPECT_TRUE( collectKeys( map ) == vector<int32>( { 1, 2, 3 } ) );

    const int32* pFound = map.find( 1 );
    SW_REQUIRE_TRUE( pFound != nullptr );
    SW_EXPECT_EQUAL( 10, *pFound );
    SW_EXPECT_TRUE( collectKeys( map ) == vector<int32>( { 1, 2, 3 } ) );

    SW_EXPECT_NOT_NULL( map.touch( 1 ) );
    SW_EXPECT_NULL( map.touch( 9 ) );
    SW_EXPECT_TRUE( collectKeys( map ) == vector<int32>( { 2, 3, 1 } ) );

    map.insertOrAssign( 2, 21 );
    SW_EXPECT_TRUE( collectKeys( map ) == vector<int32>( { 3, 1, 2 } ) );
    SW_EXPECT_EQUAL( 21, *map.find( 2 ) );
    SW_EXPECT_EQUAL( size_t( 3 ), map.size() );
}

/**
 * @brief [LRUMapTest] 지우면 밀집 배열 끝 엔트리가 빈 자리로 옮겨 와도 순서 · 조회가 그대로다
 */
SW_TEST_CASE( LRUMapTest, EraseKeepsOrderWhenTheLastEntryMoves )
{
    LRUMap<int32, int32> map;
    for ( int32 key = 0; key < 5; ++key )
    {
        map.insertOrAssign( key, key * 10 );
    }
    SW_EXPECT_NOT_NULL( map.touch( 0 ) ); // 순서 1 2 3 4 0 — 배열 자리는 0 1 2 3 4
    SW_EXPECT_TRUE( map.erase( 1 ) );     // 가장 오래된 것, 배열 끝(4)이 1 의 자리로 온다
    SW_EXPECT_FALSE( map.erase( 1 ) );
    SW_EXPECT_TRUE( collectKeys( map ) == vector<int32>( { 2, 3, 4, 0 } ) );
    SW_EXPECT_TRUE( map.erase( 0 ) ); // 가장 최근
    SW_EXPECT_TRUE( map.erase( 3 ) ); // 가운데
    SW_EXPECT_TRUE( collectKeys( map ) == vector<int32>( { 2, 4 } ) );
    SW_EXPECT_EQUAL( 20, *map.find( 2 ) );
    SW_EXPECT_EQUAL( 40, *map.find( 4 ) );
    SW_EXPECT_NULL( map.find( 3 ) );

    map.clear();
    SW_EXPECT_TRUE( map.empty() );
    map.insertOrAssign( 7, 70 );
    SW_EXPECT_TRUE( collectKeys( map ) == vector<int32>( { 7 } ) );
}

/**
 * @brief [LRUMapTest] 오래된 것부터 걸으며 일부만 지우면, 다음 엔트리가 배열 끝에 있었을 때도 건너뛰거나 두 번 보지 않는다
 */
SW_TEST_CASE( LRUMapTest, EraseWhileWalkingVisitsEachEntryOnce )
{
    LRUMap<int32, unique_ptr<int32>> map; // 옮길 수만 있는 값도 된다
    for ( int32 key = 0; key < 8; ++key )
    {
        map.insertOrAssign( key, make_unique<int32>( key ) );
    }
    SW_EXPECT_NOT_NULL( map.touch( 2 ) ); // 순서 0 1 3 4 5 6 7 2

    vector<int32> listVisited;
    auto          it = map.begin();
    while ( it != map.end() )
    {
        listVisited.push_back( it->_key );
        SW_EXPECT_EQUAL( it->_key, *it->_value );
        if ( it->_key % 2 == 0 )
            it = map.erase( it );
        else
            ++it;
    }
    SW_EXPECT_TRUE( listVisited == vector<int32>( { 0, 1, 3, 4, 5, 6, 7, 2 } ) );
    SW_EXPECT_EQUAL( size_t( 4 ), map.size() );
    SW_EXPECT_NULL( map.find( 4 ) );
    SW_EXPECT_NOT_NULL( map.find( 7 ) );
}

/**
 * @brief [LRUMapTest] 문자열 키 — 지우는 키가 엔트리 자신을 가리켜도 된다
 */
SW_TEST_CASE( LRUMapTest, ErasesByItsOwnStringKey )
{
    LRUMap<string, int32> map;
    map.insertOrAssign( "game/textures/a_long_enough_path_to_leave_the_inline_buffer.texture", 1 );
    map.insertOrAssign( "b", 2 );
    SW_EXPECT_TRUE( map.erase( map.begin()->_key ) );
    SW_EXPECT_EQUAL( size_t( 1 ), map.size() );
    SW_EXPECT_EQUAL( 2, *map.find( "b" ) );
}

/**
 * @brief [LRUMapTest] 옮겨 간 쪽은 순서까지 빈 맵이라 다시 쓸 수 있고, 걸으며 고친 값 · 꺼낸 값이 그대로다
 */
SW_TEST_CASE( LRUMapTest, MovedFromMapIsEmptyAndReusable )
{
    LRUMap<int32, unique_ptr<int32>> source;
    source.insertOrAssign( 1, make_unique<int32>( 10 ) );
    source.insertOrAssign( 2, make_unique<int32>( 20 ) );

    LRUMap<int32, unique_ptr<int32>> target( std::move( source ) );
    SW_EXPECT_TRUE( source.empty() );                 // NOLINT(bugprone-use-after-move): 옮겨 간 맵이 비었음이 계약이다
    SW_EXPECT_TRUE( source.begin() == source.end() ); // NOLINT(bugprone-use-after-move)
    source.insertOrAssign( 3, make_unique<int32>( 30 ) );
    SW_EXPECT_EQUAL( 3, source.begin()->_key );

    const unique_ptr<int32> pTaken = std::move( target.valueAt( target.begin() ) );
    SW_EXPECT_EQUAL( 10, *pTaken );
    (void)target.erase( target.begin() );
    SW_EXPECT_EQUAL( 2, target.begin()->_key );

    std::swap( source, target );
    SW_EXPECT_EQUAL( 2, source.begin()->_key );
    SW_EXPECT_EQUAL( 3, target.begin()->_key );
}
