#include "pch.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"

#include "TestFramework/TestFramework.h"

namespace
{
    struct HashedKey
    {
        int32 _a;
        int32 _b;

        bool   operator==( const HashedKey& other ) const { return _a == other._a && _b == other._b; }
        uint64 computeHash() const { return sw::HashUtil::makeHash( _a, _b ); }
    };
} // namespace

// splitmix64 · boost 결합 · FNV 는 파일마다 복사돼 있던 식이다 — 모은 함수가 옛 식과 같은 값을 내야 네트워크 비교 · 결정적 시뮬레이션이 그대로다.

/**
 * @brief [HashUtilTest] mix64 는 splitmix64 참고 구현(Vigna)의 마무리와 같은 값을 낸다(씨앗 0 의 첫 두 값)
 */
SW_TEST_CASE( HashUtilTest, Mix64MatchesSplitMixReference )
{
    uint64 state = 0;
    state += sw::HashUtil::kGoldenRatio64;
    SW_EXPECT_EQUAL( uint64{ 0xE220A8397B1DCDAFull }, sw::HashUtil::mix64( state ) );
    state += sw::HashUtil::kGoldenRatio64;
    SW_EXPECT_EQUAL( uint64{ 0x6E789E6AA1B965F4ull }, sw::HashUtil::mix64( state ) );
}

/**
 * @brief [HashUtilTest] combine 은 boost hash_combine 의 64 비트 식과 같다
 */
SW_TEST_CASE( HashUtilTest, CombineMatchesBoostFormula )
{
    const uint64 seed  = 0x0123456789ABCDEFull;
    const uint64 value = 42;
    SW_EXPECT_EQUAL( seed ^ ( value + 0x9E3779B97F4A7C15ull + ( seed << 6 ) + ( seed >> 2 ) ), sw::HashUtil::combine( seed, value ) );
}

/**
 * @brief [HashUtilTest] FNV-1a 기저 · 소수는 명세 값이다(64 비트 기저의 끝 자리까지)
 */
SW_TEST_CASE( HashUtilTest, FnvConstantsAreTheSpecValues )
{
    SW_EXPECT_EQUAL( uint32{ 2166136261u }, sw::HashUtil::kFnvOffset32 );
    SW_EXPECT_EQUAL( uint32{ 16777619u }, sw::HashUtil::kFnvPrime32 );
    SW_EXPECT_EQUAL( uint64{ 14695981039346656037ull }, sw::HashUtil::kFnvOffset64 );
    SW_EXPECT_EQUAL( uint64{ 1099511628211ull }, sw::HashUtil::kFnvPrime64 );
}

/**
 * @brief [HashUtilTest] makeHash 는 값마다 DefaultHash 를 차례로 combine 한 값이고, 순서가 바뀌면 값도 바뀐다
 */
SW_TEST_CASE( HashUtilTest, MakeHashCombinesInOrder )
{
    const uint64 first    = static_cast<uint64>( std::hash<int32>{}( 3 ) );
    const uint64 second   = static_cast<uint64>( std::hash<int32>{}( 4 ) );
    const uint64 expected = sw::HashUtil::combine( sw::HashUtil::combine( 0, first ), second );
    SW_EXPECT_EQUAL( expected, sw::HashUtil::makeHash( int32{ 3 }, int32{ 4 } ) );
    SW_EXPECT_TRUE( sw::HashUtil::makeHash( int32{ 3 }, int32{ 4 } ) != sw::HashUtil::makeHash( int32{ 4 }, int32{ 3 } ) );
}

/**
 * @brief [HashUtilTest] DefaultHash 는 computeHash() 가 있는 키에 그것을 쓰고, 없으면 std::hash 를 쓰며 이종 키 오버로드를 남긴다
 * @details 키 구조체마다 해시 구조체를 두지 않아도 `sw::unordered_map<HashedKey, …>` 가 컴파일되고 찾아야 한다. 문자열 키의 `string_view`
 *          조회(할당 없는 이종 조회)가 기본 해시가 바뀐 뒤에도 같은 해시를 내야 한다.
 */
SW_TEST_CASE( HashUtilTest, DefaultHashUsesComputeHash )
{
    const HashedKey key{ 1, 2 };
    SW_EXPECT_EQUAL( static_cast<size_t>( key.computeHash() ), sw::DefaultHash<HashedKey>{}( key ) );
    SW_EXPECT_EQUAL( std::hash<int32>{}( 7 ), sw::DefaultHash<int32>{}( 7 ) );

    const HashedKey                     swapped{ 2, 1 };
    sw::unordered_map<HashedKey, int32> mapValue;
    mapValue[key]     = 10;
    mapValue[swapped] = 20;
    SW_EXPECT_EQUAL( 10, mapValue[key] );
    SW_EXPECT_EQUAL( 20, mapValue[swapped] );

    const sw::string text( "name" );
    SW_EXPECT_EQUAL( sw::DefaultHash<sw::string>{}( text ), sw::DefaultHash<sw::string>{}( sw::string_view( "name" ) ) );
}
