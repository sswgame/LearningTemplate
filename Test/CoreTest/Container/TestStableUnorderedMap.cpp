#include "pch.h"

#include "Core/Container/stable_unordered_map.h"
#include "Core/Container/string.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /// 복사도 이동도 안 되는 값입니다 — 주소 고정 맵은 값을 옮기지 않으므로 이런 값도 담을 수 있어야 합니다.
    struct PinnedValue
    {
        PinnedValue()                                = default;
        PinnedValue( const PinnedValue& )            = delete;
        PinnedValue& operator=( const PinnedValue& ) = delete;

        int32 _value{ 0 };
    };
} // namespace

/**
 * @brief [StableUnorderedMapTest] 키를 많이 넣어 표가 여러 번 자라도 먼저 쥔 값 주소가 그대로다
 */
SW_TEST_CASE( StableUnorderedMapTest, ValueAddressSurvivesGrowth )
{
    stable_unordered_map<int32, PinnedValue> map;
    PinnedValue*                             pFirst = &map[-1];
    pFirst->_value                                  = 7;

    for ( int32 index = 0; index < 1000; ++index )
    {
        map[index]._value = index;
    }

    SW_EXPECT_EQUAL( size_t( 1001 ), map.size() );
    SW_EXPECT_TRUE( pFirst == &map[-1] );
    SW_EXPECT_EQUAL( 7, pFirst->_value );
}

/**
 * @brief [StableUnorderedMapTest] 다른 키를 지워도 남은 값의 주소가 그대로다 (일반 해시맵은 마지막 원소를 지운 자리로 옮긴다)
 */
SW_TEST_CASE( StableUnorderedMapTest, ValueAddressSurvivesEraseOfOtherKeys )
{
    stable_unordered_map<int32, PinnedValue> map;
    PinnedValue*                             arrAddress[16]{};
    for ( int32 key = 0; key < 16; ++key )
    {
        arrAddress[key]         = &map[key];
        arrAddress[key]->_value = key * 10;
    }

    SW_EXPECT_EQUAL( size_t( 1 ), map.erase( 3 ) );
    SW_EXPECT_EQUAL( size_t( 0 ), map.erase( 3 ) );

    for ( int32 key = 0; key < 16; ++key )
    {
        if ( key == 3 )
        {
            SW_EXPECT_TRUE( map.find( key ) == map.end() );
            continue;
        }
        SW_ASSERT_TRUE( map.find( key ) != map.end() );
        SW_EXPECT_TRUE( arrAddress[key] == &map.find( key )->second );
        SW_EXPECT_EQUAL( key * 10, map.find( key )->second._value );
    }
}

/**
 * @brief [StableUnorderedMapTest] 이종 키(string_view)로 찾고, 순회는 구조체 바인딩으로 키와 값을 읽는다
 */
SW_TEST_CASE( StableUnorderedMapTest, FindsByViewAndIteratesWithStructuredBinding )
{
    stable_unordered_map<string, PinnedValue> map;
    map["a"]._value = 1;
    map["b"]._value = 2;

    SW_EXPECT_TRUE( map.contains( string_view{ "a" } ) );
    SW_EXPECT_TRUE( map.find( string_view{ "zz" } ) == map.end() );

    int32 sum = 0;
    for ( auto& [name, value] : map )
    {
        SW_EXPECT_EQUAL( size_t( 1 ), name.size() );
        sum += value._value;
    }
    SW_EXPECT_EQUAL( 3, sum );
}

/**
 * @brief [StableUnorderedMapTest] try_emplace 는 키가 있으면 인자를 건드리지 않고, clear 는 모두 비운다
 */
SW_TEST_CASE( StableUnorderedMapTest, TryEmplaceKeepsExistingAndClearEmpties )
{
    stable_unordered_map<int32, string> map;
    const auto                          first  = map.try_emplace( 1, "one" );
    const auto                          second = map.try_emplace( 1, "uno" );
    SW_EXPECT_TRUE( first.second );
    SW_EXPECT_FALSE( second.second );
    SW_EXPECT_TRUE( &first.first->second == &second.first->second );
    SW_EXPECT_TRUE( map[1] == "one" );

    map.clear();
    SW_EXPECT_TRUE( map.empty() );
}
