// StringUtil 의 나누기 도우미 — forEachLine 의 CRLF · 빈 줄 · 끝 줄바꿈, forEachToken 의 빈 토큰, 둘 다 false 에서 멈추는지, parseFloatList 가 숫자 아닌 토큰과 개수 상한에서 멈추는지.
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

SW_TEST_CASE( StringUtilSplitTest, ForEachLineStripsCarriageReturnAndKeepsEmptyLines )
{
    vector<string> listLine;
    StringUtil::forEachLine( "a\r\n\nb\n", [&]( string_view line )
    { listLine.push_back( string( line ) ); } );
    SW_ASSERT_EQUAL( static_cast<size_t>( 3 ), listLine.size() );
    SW_EXPECT_TRUE( listLine[0] == "a" );
    SW_EXPECT_TRUE( listLine[1].empty() );
    SW_EXPECT_TRUE( listLine[2] == "b" );

    uint32 count = 0;
    StringUtil::forEachLine( "", [&]( string_view )
    { ++count; } );
    SW_EXPECT_EQUAL( 0u, count );
}

SW_TEST_CASE( StringUtilSplitTest, ForEachLineStopsWhenCallbackReturnsFalse )
{
    uint32 count = 0;
    StringUtil::forEachLine( "x\ny\nz", [&]( string_view line )
    {
        ++count;
        return line != "y";
    } );
    SW_EXPECT_EQUAL( 2u, count );
}

SW_TEST_CASE( StringUtilSplitTest, ForEachTokenSkipsEmptyTokensAndStops )
{
    vector<string> listToken;
    StringUtil::forEachToken( ",,Spring, Fall;;Winter ", ",; ", [&]( string_view token )
    { listToken.push_back( string( token ) ); } );
    SW_ASSERT_EQUAL( static_cast<size_t>( 3 ), listToken.size() );
    SW_EXPECT_TRUE( listToken[0] == "Spring" );
    SW_EXPECT_TRUE( listToken[1] == "Fall" );
    SW_EXPECT_TRUE( listToken[2] == "Winter" );

    uint32 count = 0;
    StringUtil::forEachToken( "a b c", " ", [&]( string_view token )
    {
        ++count;
        return token != "b";
    } );
    SW_EXPECT_EQUAL( 2u, count );
}

SW_TEST_CASE( StringUtilSplitTest, ParseFloatListStopsAtBadTokenAndMaxCount )
{
    float32 arrValue[3]{ 9.0f, 9.0f, 9.0f };
    SW_EXPECT_EQUAL( 3u, StringUtil::parseFloatList( "(1, 0.5 -2)", " ,()", arrValue, 3 ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, arrValue[0], 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, arrValue[1], 1e-6f );
    SW_EXPECT_NEAR_EQUAL( -2.0f, arrValue[2], 1e-6f );

    SW_EXPECT_EQUAL( 2u, StringUtil::parseFloatList( "4 5 6 7", " ", arrValue, 2 ) );
    SW_EXPECT_NEAR_EQUAL( -2.0f, arrValue[2], 1e-6f ); // 상한 뒤 칸은 그대로

    SW_EXPECT_EQUAL( 1u, StringUtil::parseFloatList( "8 x 9", " ", arrValue, 3 ) );
    SW_EXPECT_NEAR_EQUAL( 5.0f, arrValue[1], 1e-6f ); // 못 읽은 칸은 그대로
    SW_EXPECT_EQUAL( 0u, StringUtil::parseFloatList( "", " ", arrValue, 3 ) );
}
