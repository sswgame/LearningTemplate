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

/**
 * @brief [HashUtilTest] 문자열 해시는 FNV-1a 의 알려진 값과 같다(32 · 64 비트, 대소문자 구분)
 * @details 알려진 값은 FNV 명세의 시험 벡터다. 쿠킹 산출물 · 팩 해시 · 저장 파일의 문자열 키가 이 값으로 디스크에 남으므로 바뀌면 안 된다.
 */
SW_TEST_CASE( HashUtilTest, StringHashMatchesFnvKnownValues )
{
    SW_EXPECT_EQUAL( sw::HashUtil::kFnvOffset64, sw::HashUtil::computeStringHash64( "", 0, false ) );
    SW_EXPECT_EQUAL( sw::HashUtil::kFnvOffset32, sw::HashUtil::computeStringHash32( "", 0, false ) );
    SW_EXPECT_EQUAL( uint64{ 0xAF63DC4C8601EC8Cull }, sw::HashUtil::computeStringHash64( "a", 1, false ) );
    SW_EXPECT_EQUAL( uint64{ 0x85944171F73967E8ull }, sw::HashUtil::computeStringHash64( "foobar", 6, false ) );
    SW_EXPECT_EQUAL( uint32{ 0xE40C292Cu }, sw::HashUtil::computeStringHash32( "a", 1, false ) );
    SW_EXPECT_EQUAL( uint32{ 0xBF9CF968u }, sw::HashUtil::computeStringHash32( "foobar", 6, false ) );
    SW_EXPECT_EQUAL( ( sw::HashUtil::kFnvOffset64 ^ uint64{ 'a' } ) * sw::HashUtil::kFnvPrime64, sw::HashUtil::computeStringHash64( "a", 1, false ) );

    // string_view 오버로드와 이어 섞기(seed)는 포인터 + 길이 판과 같다.
    SW_EXPECT_EQUAL( sw::HashUtil::computeStringHash64( "foobar", 6, false ), sw::HashUtil::computeStringHash64( std::string_view( "foobar" ), false ) );
    SW_EXPECT_EQUAL( sw::HashUtil::computeStringHash64( "foobar", 6, false ), sw::HashUtil::computeStringHash64( "bar", 3, false, sw::HashUtil::computeStringHash64( "foo", 3, false ) ) );
    SW_EXPECT_EQUAL( sw::HashUtil::computeStringHash32( "foobar", 6, false ), sw::HashUtil::computeStringHash32( std::string_view( "foobar" ), false ) );

    // 바이트 이어 섞기는 문자열 해시(대소문자 구분)와 같다.
    SW_EXPECT_EQUAL( sw::HashUtil::computeStringHash64( "foobar", 6, false ), sw::HashUtil::appendBytes64( sw::HashUtil::kFnvOffset64, "foobar", 6 ) );
    SW_EXPECT_EQUAL( sw::HashUtil::computeStringHash32( "foobar", 6, false ), sw::HashUtil::appendBytes32( sw::HashUtil::kFnvOffset32, "foobar", 6 ) );
    SW_EXPECT_EQUAL( sw::HashUtil::computeStringHash64( "a", 1, false ), sw::HashUtil::appendValue64( sw::HashUtil::kFnvOffset64, uint8{ 'a' } ) );

    // 기본값은 대소문자를 무시한다.
    SW_EXPECT_EQUAL( sw::HashUtil::computeStringHash64( "EngineResourcePath", 18 ), sw::HashUtil::computeStringHash64( "engineresourcepath", 18, false ) );
    SW_EXPECT_TRUE( sw::HashUtil::computeStringHash64( "ABC", 3, false ) != sw::HashUtil::computeStringHash64( "abc", 3, false ) );
    SW_EXPECT_EQUAL( sw::HashUtil::computeStringHash32( "ABC", 3, true ), sw::HashUtil::computeStringHash32( "abc", 3, true ) );
}

/**
 * @brief [HashUtilTest] 넓은 문자가 하위 한 바이트로 잘리지 않는다
 * @details 부호 확장을 막으려고 `uint8` 로 **고정**하면 안 된다. 이 해시 템플릿은 `utf16` 으로도 불리는데(`std::hash<fixed_wstring>`), 그러면 넓은 문자가
 *          하위 한 바이트만 남는다 — 한글처럼 상위 바이트가 의미를 갖는 문자열이 통째로 한 버킷에 뭉친다. 부호 없는 타입은 맞되 **폭은 CharT 를 따라야** 한다.
 */
SW_TEST_CASE( HashUtilTest, WideCharStringHashIsNotTruncatedToOneByte )
{
    // 상위 바이트만 다르고 하위 바이트는 둘 다 0 이다.
    const utf16 arrWideA[] = { static_cast<utf16>( 0xAC00 ), static_cast<utf16>( 0 ) };
    const utf16 arrWideB[] = { static_cast<utf16>( 0xAD00 ), static_cast<utf16>( 0 ) };

    SW_EXPECT_TRUE( sw::HashUtil::computeStringHash64( arrWideA, 1 ) != sw::HashUtil::computeStringHash64( arrWideB, 1 ) );
    SW_EXPECT_TRUE( sw::HashUtil::computeStringHash32( arrWideA, 1 ) != sw::HashUtil::computeStringHash32( arrWideB, 1 ) );
}

/**
 * @brief [HashUtilTest] 비-ASCII 바이트를 부호 없이 해시한다
 * @details `char` 의 부호성은 구현 정의이고 UTF-8 의 0x80 이상 바이트는 signed char 에서 음수다. 대소문자 무시 경로만 부호 확장되면 같은 바이트가
 *          경로에 따라 다른 값으로 해싱되고, `char` 가 unsigned 인 플랫폼에서는 해시 자체가 달라진다.
 */
SW_TEST_CASE( HashUtilTest, NonAsciiBytesAreHashedAsUnsigned )
{
    // "가" = EA B0 80 — 모든 바이트가 signed char 에서 음수다. 비-ASCII 에는 대소문자가 없으므로 두 해시 경로가 같은 값을 내야 한다.
    const sw::string korean{ "\xEA\xB0\x80" };
    SW_EXPECT_EQUAL( sw::HashUtil::computeStringHash64( korean.c_str(), korean.size(), false ), sw::HashUtil::computeStringHash64( korean.c_str(), korean.size(), true ) );
    SW_EXPECT_EQUAL( sw::HashUtil::computeStringHash32( korean.c_str(), korean.size(), false ), sw::HashUtil::computeStringHash32( korean.c_str(), korean.size(), true ) );

    // 바이트 이어 섞기와도 같다(부호 없는 바이트를 섞는다).
    SW_EXPECT_EQUAL( sw::HashUtil::computeStringHash64( korean.c_str(), korean.size(), false ), sw::HashUtil::appendBytes64( sw::HashUtil::kFnvOffset64, korean.c_str(), korean.size() ) );
}
