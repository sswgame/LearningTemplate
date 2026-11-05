#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

SW_TEST_CASE( StringUtilTest, MatchesWildcardStarAndQuestionMark )
{
    SW_EXPECT_TRUE( StringUtil::matchesWildcard( "*", "" ) );
    SW_EXPECT_TRUE( StringUtil::matchesWildcard( "Core*", "CoreTest" ) );
    SW_EXPECT_TRUE( StringUtil::matchesWildcard( "a?c", "abc" ) );
    SW_EXPECT_FALSE( StringUtil::matchesWildcard( "a?c", "ac" ) );
    SW_EXPECT_FALSE( StringUtil::matchesWildcard( "Core*", "EngineTest" ) );
}

/** @brief [StringUtilTest] 유니코드 표기 `U+XXXX` 는 대문자 16진 4 ~ 6 자리만 받고, 범위 밖 · 서로게이트는 거절하며 실패하면 값을 그대로 둔다 */
SW_TEST_CASE( StringUtilTest, CodepointNotationParsesUPlusHex )
{
    uint32 codepoint = 7;
    SW_EXPECT_TRUE( StringUtil::tryParseCodepointNotation( "U+E003", codepoint ) && codepoint == 0xE003u );
    SW_EXPECT_TRUE( StringUtil::tryParseCodepointNotation( "U+1F600", codepoint ) && codepoint == 0x1F600u );
    SW_EXPECT_TRUE( StringUtil::tryParseCodepointNotation( "U+10FFFF", codepoint ) && codepoint == 0x10FFFFu );
    codepoint = 7;
    for ( const utf8* pBad : { "E003", "U+e003", "U+12", "U+1234567", "U+110000", "U+D800", "u+E003", "U+E00G", "" } )
        SW_EXPECT_FALSE_MSG( StringUtil::tryParseCodepointNotation( pBad, codepoint ), pBad );
    SW_EXPECT_EQUAL( uint32( 7 ), codepoint );
}

SW_TEST_CASE( StringUtilTest, StartsWithHonoursIgnoreCase )
{
    SW_EXPECT_TRUE( StringUtil::startsWith( "--filter=x", "--filter=" ) );
    SW_EXPECT_FALSE( StringUtil::startsWith( "Engine", "engine" ) );
    SW_EXPECT_TRUE( StringUtil::startsWith( "Engine", "engine", true ) );
}

SW_TEST_CASE( StringUtilTest, TrimRemovesSurroundingWhitespaceOnly )
{
    SW_EXPECT_EQUAL( string_view( "a b" ), StringUtil::trim( "  a b \t\n" ) );
    SW_EXPECT_EQUAL( string_view( "" ), StringUtil::trim( "   " ) );
}

SW_TEST_CASE( StringUtilTest, WipeSecretOverwritesTheBytes )
{
    string       secret = "a longer secret than the small buffer holds";
    const utf8*  pData  = secret.data();
    const size_t size   = secret.size();
    StringUtil::wipeSecret( secret );
    SW_EXPECT_TRUE( secret.empty() );
    for ( size_t index = 0; index < size; ++index )
    {
        SW_EXPECT_EQUAL( 0, static_cast<int32>( pData[index] ) );
    }
}
