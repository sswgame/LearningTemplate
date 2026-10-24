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

/**
 * @brief [StateHashReportLineTest] 오브젝트별 해시 글은 이름에 공백이 있어도 되읽히고, 두 목록에서 다른 오브젝트의 이름만 짚으며, 형식이 다른 글은 거절한다
 * @details 어긋난 틱에서 두 프로세스의 `state.objects` 답을 견줘 어느 오브젝트가 다른지 찾는 길이다(서버 디버깅 S7 의 어긋난 오브젝트 짚기).
 */
SW_TEST_CASE( StateHashReportLineTest, DifferingObjectsAreNamed )
{
    const vector<SceneObjectHash> listLeft = {
        SceneObjectHash{       "Door", 0x1111ull},
        SceneObjectHash{       "Hero", 0x2222ull},
        SceneObjectHash{"Spawn Point", 0x3333ull}
    };
    string text;
    SceneStateHash::writeObjectReport( listLeft, text );
    vector<SceneObjectHash> parsed;
    SW_ASSERT_TRUE( SceneStateHash::parseObjectReport( text, parsed ) );
    SW_ASSERT_EQUAL( size_t( 3 ), parsed.size() );
    SW_EXPECT_TRUE( parsed[2]._name == "Spawn Point" );
    SW_EXPECT_EQUAL( uint64( 0x3333ull ), parsed[2]._hash );

    vector<string> listName;
    SW_EXPECT_FALSE( SceneStateHash::findDifferingObjects( listLeft, parsed, listName ) );
    SW_EXPECT_TRUE( listName.empty() );

    // Hero 의 해시가 다르고, Door 는 한쪽에만 있다.
    vector<SceneObjectHash> listRight = listLeft;
    listRight[1]._hash                = 0x9999ull;
    listRight.erase( listRight.begin() );
    SW_EXPECT_TRUE( SceneStateHash::findDifferingObjects( listLeft, listRight, listName ) );
    SW_ASSERT_EQUAL( size_t( 2 ), listName.size() );
    SW_EXPECT_TRUE( listName[0] == "Door" );
    SW_EXPECT_TRUE( listName[1] == "Hero" );

    SW_EXPECT_FALSE( SceneStateHash::parseObjectReport( "not a hash line\n", parsed ) );
    SW_EXPECT_TRUE( parsed.empty() );
}
