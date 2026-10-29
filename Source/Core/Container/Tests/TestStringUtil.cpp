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
