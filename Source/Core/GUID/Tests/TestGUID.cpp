#include "pch.h"

#include "Core/Container/unordered_set.h"
#include "Core/GUID/GUID.h"
#include "Core/TestFramework/TestFramework.h"

using namespace sw;

/** @brief [GUIDTest] 만든 값은 모두 0 이 아니고 서로 다르며, 기본 생성은 모두 0(없음)이다 */
SW_TEST_CASE( GUIDTest, CreateGivesDistinctValidValues )
{
    const sw::GUID first  = sw::GUID::create();
    const sw::GUID second = sw::GUID::create();
    SW_EXPECT_TRUE( first.isValid() );
    SW_EXPECT_TRUE( second.isValid() );
    SW_EXPECT_TRUE( first != second );
    SW_EXPECT_FALSE( first < first );

    const sw::GUID none{};
    SW_EXPECT_FALSE( none.isValid() );
    SW_EXPECT_TRUE( none == sw::GUID( 0, 0 ) );
}

/** @brief [GUIDTest] 글 표기는 소문자 16진 32 자(하이픈 없음)이고 읽으면 같은 값이다. 정렬은 글 표기의 사전순과 같다 */
SW_TEST_CASE( GUIDTest, TextIsLowercaseHexWithoutHyphens )
{
    const sw::GUID sample( 0x0123456789abcdefull, 0x0011223344556677ull );
    SW_EXPECT_EQUAL( string( "0123456789abcdef0011223344556677" ), sample.toString() );
    SW_EXPECT_EQUAL( string( "00000000000000000000000000000000" ), sw::GUID{}.toString() );

    const sw::GUID           created = sw::GUID::create();
    const optional<sw::GUID> parsed  = sw::GUID::tryParse( created.toString() );
    SW_REQUIRE_TRUE( parsed.has_value() );
    SW_EXPECT_TRUE( *parsed == created );
    SW_EXPECT_EQUAL( created.computeHash(), parsed->computeHash() );

    const sw::GUID smaller( 0x0123456789abcdefull, 0x0011223344556676ull );
    SW_EXPECT_TRUE( smaller < sample );
    SW_EXPECT_TRUE( smaller.toString() < sample.toString() );
}

/** @brief [GUIDTest] 다른 철자(대문자 · 하이픈 · 길이 · 16진 밖 글자)는 읽지 않는다. 같은 값을 두 철자로 적지 않는다 */
SW_TEST_CASE( GUIDTest, OtherSpellingsAreRejected )
{
    SW_EXPECT_FALSE( sw::GUID::tryParse( "" ).has_value() );
    SW_EXPECT_FALSE( sw::GUID::tryParse( "0123456789ABCDEF0011223344556677" ).has_value() );
    SW_EXPECT_FALSE( sw::GUID::tryParse( "01234567-89ab-cdef-0011-223344556677" ).has_value() );
    SW_EXPECT_FALSE( sw::GUID::tryParse( "0123456789abcdef001122334455667" ).has_value() );
    SW_EXPECT_FALSE( sw::GUID::tryParse( "0123456789abcdef00112233445566778" ).has_value() );
    SW_EXPECT_FALSE( sw::GUID::tryParse( "g123456789abcdef0011223344556677" ).has_value() );
    SW_EXPECT_TRUE( sw::GUID::tryParse( "00000000000000000000000000000000" ).has_value() ); // 모두 0 의 거절은 쓰는 쪽(`AssetGUID`) 몫이다
}

/** @brief [GUIDTest] 해시 컨테이너의 키로 쓴다(`computeHash` — `DefaultHash`) */
SW_TEST_CASE( GUIDTest, HashContainerKey )
{
    unordered_set<sw::GUID> setGUID;
    constexpr int32         kCount = 100;
    for ( int32 index = 0; index < kCount; ++index )
        SW_EXPECT_TRUE( setGUID.insert( sw::GUID::create() ).second );
    SW_EXPECT_EQUAL( static_cast<size_t>( kCount ), setGUID.size() );
}
