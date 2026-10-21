#include "pch.h"

#include "Engine/Scene/SceneStateHash.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

/**
 * @brief [StateHashReportLineTest] 이력 한 줄은 로그 머리 · 꼬리가 붙어도 되읽히고, 빈 이력은 `-` 이며, 형식이 다른 항목이 하나라도 있으면 거절한다
 * @details 에디터 Sync 탭이 클라이언트 프로세스의 표준 입력 명령 답(`state.hashline`, 로그 한 줄)에서 이력을 거두는 길이다.
 */
SW_TEST_CASE( StateHashReportLineTest, ALineRoundTripsInsideALogLine )
{
    const vector<StateHashSample> listSample = {
        StateHashSample{12, 0x00000000DEADBEEFull},
        StateHashSample{13, 0xFFFFFFFFFFFFFFFFull}
    };
    string line;
    StateHashHistory::writeReportLine( listSample, line );

    vector<StateHashSample> parsed;
    SW_ASSERT_TRUE( StateHashHistory::tryParseReportLine( "[Info] > state.hashline 2 : " + line + " (main.cpp:10)", parsed ) );
    SW_ASSERT_EQUAL( size_t( 2 ), parsed.size() );
    SW_EXPECT_EQUAL( uint64( 12 ), parsed[0]._tick );
    SW_EXPECT_EQUAL( uint64( 0x00000000DEADBEEFull ), parsed[0]._hash );
    SW_EXPECT_EQUAL( uint64( 0xFFFFFFFFFFFFFFFFull ), parsed[1]._hash );

    string emptyLine;
    StateHashHistory::writeReportLine( {}, emptyLine );
    SW_EXPECT_TRUE( StateHashHistory::tryParseReportLine( emptyLine, parsed ) );
    SW_EXPECT_TRUE( parsed.empty() );

    SW_EXPECT_FALSE( StateHashHistory::tryParseReportLine( "net stats only", parsed ) );
    SW_EXPECT_FALSE( StateHashHistory::tryParseReportLine( string( StateHashHistory::kReportLinePrefix ) + " 12:zz,13:ff", parsed ) );
    SW_EXPECT_TRUE( parsed.empty() );
}
