// StringUtil::split — 글을 나누는 정본: 빈 토큰 유지(기본) · 자르기 · 빈 토큰 버리기 · 여러 구분자 · 빈 글.
#include "pch.h"

#include "Core/Container/StringUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

SW_TEST_CASE( StringSplitTest, KeepsEmptyTokensByDefault )
{
    const vector<string_view> listToken = StringUtil::split( "a,,b,", "," );
    SW_ASSERT_EQUAL( static_cast<size_t>( 4 ), listToken.size() );
    SW_EXPECT_TRUE( listToken[0] == "a" );
    SW_EXPECT_TRUE( listToken[1].empty() );
    SW_EXPECT_TRUE( listToken[2] == "b" );
    SW_EXPECT_TRUE( listToken[3].empty() );

    const vector<string_view> listEmpty = StringUtil::split( "", "," );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listEmpty.size() );
    SW_EXPECT_TRUE( listEmpty[0].empty() );
}

SW_TEST_CASE( StringSplitTest, TrimsAndDropsEmptyTokensOnRequest )
{
    const vector<string_view> listToken = StringUtil::split( " a.scenario.xml , b ,, \t,", ",", { ._bTrim = true, ._bDropEmpty = true } );
    SW_ASSERT_EQUAL( static_cast<size_t>( 2 ), listToken.size() );
    SW_EXPECT_TRUE( listToken[0] == "a.scenario.xml" );
    SW_EXPECT_TRUE( listToken[1] == "b" );
    SW_EXPECT_TRUE( StringUtil::split( " , ,", ",", { ._bTrim = true, ._bDropEmpty = true } ).empty() );
}

SW_TEST_CASE( StringSplitTest, SplitsOnAnyOfSeveralSeparators )
{
    const vector<string_view> listToken = StringUtil::split( "1 2,3\t4", " ,\t" );
    SW_ASSERT_EQUAL( static_cast<size_t>( 4 ), listToken.size() );
    SW_EXPECT_TRUE( listToken[3] == "4" );
}
