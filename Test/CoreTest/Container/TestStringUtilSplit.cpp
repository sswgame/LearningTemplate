// StringUtil 의 나누기 도우미 — forEachLine 이 CRLF · 빈 줄 · 끝 줄바꿈을 어떻게 다루는지, false 에서 멈추는지.
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
