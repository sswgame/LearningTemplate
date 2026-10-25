#include "pch.h"

#include "Core/Network/DebugReplyLine.h"

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
    SW_ASSERT_TRUE( StateHashHistory::writeReportLine( listSample, line ) );

    vector<StateHashSample> parsed;
    SW_ASSERT_TRUE( StateHashHistory::tryParseReportLine( "[Info] > state.hashline 2 : " + line + " (main.cpp:10)", parsed ) );
    SW_ASSERT_EQUAL( size_t( 2 ), parsed.size() );
    SW_EXPECT_EQUAL( uint64( 12 ), parsed[0]._tick );
    SW_EXPECT_EQUAL( uint64( 0x00000000DEADBEEFull ), parsed[0]._hash );
    SW_EXPECT_EQUAL( uint64( 0xFFFFFFFFFFFFFFFFull ), parsed[1]._hash );

    string emptyLine;
    SW_ASSERT_TRUE( StateHashHistory::writeReportLine( {}, emptyLine ) );
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

/**
 * @brief [StateHashReportLineTest] 틱을 단 오브젝트 목록은 틱과 함께 되읽히고, 틱 머리가 없거나 숫자가 아니면 거절한다
 */
SW_TEST_CASE( StateHashReportLineTest, ATickedObjectReportRoundTripsAndRejectsAMissingTick )
{
    const vector<SceneObjectHash> listObject = {
        SceneObjectHash{"Door", 0x1111ull},
        SceneObjectHash{"Hero", 0x2222ull}
    };
    string text;
    SceneStateHash::writeTickedObjectReport( 42, listObject, text );
    uint64                  tick = 0;
    vector<SceneObjectHash> parsed;
    SW_ASSERT_TRUE( SceneStateHash::parseTickedObjectReport( text, tick, parsed ) );
    SW_EXPECT_EQUAL( uint64( 42 ), tick );
    SW_ASSERT_EQUAL( size_t( 2 ), parsed.size() );
    SW_EXPECT_TRUE( parsed[1]._name == "Hero" );

    string plain;
    SceneStateHash::writeObjectReport( listObject, plain );
    SW_EXPECT_FALSE( SceneStateHash::parseTickedObjectReport( plain, tick, parsed ) );
    SW_EXPECT_FALSE( SceneStateHash::parseTickedObjectReport( "tick x\n", tick, parsed ) );
    SW_EXPECT_TRUE( parsed.empty() );
}

/**
 * @brief [StateHashReportLineTest] 한 줄 판 오브젝트 목록은 공백 · 쉼표 · 콜론 · 퍼센트가 든 이름도 로그 꼬리가 붙은 채 되읽히고, 항목이 틀리면 거절한다
 * @details 표준 입력 명령의 답은 로그 한 줄로만 나가므로(클라이언트 칸이 이 길로 읽는다) 여러 줄 판을 쓸 수 없다.
 */
SW_TEST_CASE( StateHashReportLineTest, AnObjectLineRoundTripsAwkwardNamesInsideALogLine )
{
    const vector<SceneObjectHash> listObject = {
        SceneObjectHash{ "Door 1", 0x1111ull},
        SceneObjectHash{"a,b:c%d", 0x2222ull}
    };
    string line;
    SW_ASSERT_TRUE( SceneStateHash::writeObjectReportLine( 42, listObject, line ) );
    SW_EXPECT_TRUE( line.find( '\n' ) == string::npos );
    uint64                  tick = 0;
    vector<SceneObjectHash> parsed;
    SW_ASSERT_TRUE( SceneStateHash::tryParseObjectReportLine( "[12:00:00][Info] " + line + " (client)", tick, parsed ) );
    SW_EXPECT_EQUAL( uint64( 42 ), tick );
    SW_ASSERT_EQUAL( size_t( 2 ), parsed.size() );
    SW_EXPECT_TRUE( parsed[0]._name == "Door 1" && parsed[0]._hash == 0x1111ull );
    SW_EXPECT_TRUE( parsed[1]._name == "a,b:c%d" && parsed[1]._hash == 0x2222ull );

    SW_ASSERT_TRUE( SceneStateHash::writeObjectReportLine( 7, {}, line ) );
    SW_ASSERT_TRUE( SceneStateHash::tryParseObjectReportLine( line, tick, parsed ) );
    SW_EXPECT_TRUE( parsed.empty() );
    SW_EXPECT_EQUAL( uint64( 7 ), tick );

    SW_EXPECT_FALSE( SceneStateHash::tryParseObjectReportLine( "no prefix 1 2", tick, parsed ) );
    SW_EXPECT_FALSE( SceneStateHash::tryParseObjectReportLine( string( SceneStateHash::kObjectReportLinePrefix ) + " 5 zz:Door", tick, parsed ) );
    SW_EXPECT_FALSE( SceneStateHash::tryParseObjectReportLine( string( SceneStateHash::kObjectReportLinePrefix ) + " 5 00000000000000aa:Do%2", tick, parsed ) );
    SW_EXPECT_TRUE( parsed.empty() );
}

/**
 * @brief [StateHashReportLineTest] 한 줄 상한(`DebugReplyLine::kMaxBytes`)을 넘는 목록은 쓰는 쪽이 오류로 멈추고, 읽는 쪽도 넘는 줄을 거절한다
 * @details 파이프 · 로그 줄 버퍼가 한 줄을 조용히 자르거나 둘로 쪼개 다음 줄과 섞는 일을 만들지 않는다. 상한 안의 줄은 그대로 쓰이고 읽힌다.
 */
SW_TEST_CASE( StateHashReportLineTest, ALineOverTheLimitIsAnErrorOnBothSides )
{
    // 오브젝트 하나가 `<16자리>:<이름>,` — 이름 8 글자면 26 바이트다.
    vector<SceneObjectHash> listObject;
    for ( size_t index = 0; index < DebugReplyLine::kMaxBytes / 26 + 8; ++index )
    {
        listObject.push_back( SceneObjectHash{ "Obj" + to_string( 10000 + index ), index } );
    }
    string line = "stale";
    {
        SW_TEST_DEFENSIVE_SCOPE( "an over-long object line is refused with an error" );
        test::ScopedLogCollector log;
        SW_EXPECT_FALSE( SceneStateHash::writeObjectReportLine( 1, listObject, line ) );
        SW_EXPECT_EQUAL( 1u, log.countWithID( "State.ReportLineTooLong" ) );
    }
    SW_EXPECT_TRUE( line.empty() ); // 반쯤 쓴 줄을 남기지 않는다

    // 상한 안의 목록은 쓰이고 되읽힌다.
    listObject.resize( 20 );
    SW_ASSERT_TRUE( SceneStateHash::writeObjectReportLine( 1, listObject, line ) );
    SW_EXPECT_TRUE( line.size() <= DebugReplyLine::kMaxBytes );
    uint64                  tick = 0;
    vector<SceneObjectHash> parsed;
    SW_EXPECT_TRUE( SceneStateHash::tryParseObjectReportLine( line, tick, parsed ) );
    SW_EXPECT_EQUAL( size_t( 20 ), parsed.size() );

    // 읽는 쪽: 같은 상수를 넘는 줄(유효한 항목이라도)은 거절한다.
    string tooLong = string( SceneStateHash::kObjectReportLinePrefix ) + " 1 ";
    while ( tooLong.size() <= DebugReplyLine::kMaxBytes )
    {
        tooLong += "00000000000000aa:Door,";
    }
    tooLong.pop_back();
    SW_EXPECT_FALSE( SceneStateHash::tryParseObjectReportLine( tooLong, tick, parsed ) );
    SW_EXPECT_TRUE( parsed.empty() );

    // 이력 한 줄: 최대 샘플 수는 상한 안에 들고, 그보다 많은 샘플은 오류다.
    vector<StateHashSample> listSample;
    for ( uint32 index = 0; index < StateHashHistory::kMaxReportLineSamples; ++index )
    {
        listSample.push_back( StateHashSample{ 0xFFFFFFFFFFFFFFFFull - index, 0xFFFFFFFFFFFFFFFFull } ); // 틱 20자리
    }
    string historyLine = "stale";
    SW_ASSERT_TRUE( StateHashHistory::writeReportLine( listSample, historyLine ) );
    SW_EXPECT_TRUE( historyLine.size() <= DebugReplyLine::kMaxBytes );
    vector<StateHashSample> parsedSample;
    SW_EXPECT_TRUE( StateHashHistory::tryParseReportLine( historyLine, parsedSample ) );
    SW_EXPECT_EQUAL( size_t( StateHashHistory::kMaxReportLineSamples ), parsedSample.size() );

    while ( listSample.size() < DebugReplyLine::kMaxBytes / 20 )
    {
        listSample.push_back( StateHashSample{ 0xFFFFFFFFFFFFFFFFull - listSample.size(), 0xFFFFFFFFFFFFFFFFull } );
    }
    {
        SW_TEST_DEFENSIVE_SCOPE( "an over-long history line is refused with an error" );
        test::ScopedLogCollector log;
        SW_EXPECT_FALSE( StateHashHistory::writeReportLine( listSample, historyLine ) );
        SW_EXPECT_EQUAL( 1u, log.countWithID( "State.ReportLineTooLong" ) );
    }
    SW_EXPECT_TRUE( historyLine.empty() );
    SW_EXPECT_FALSE( StateHashHistory::tryParseReportLine( string( StateHashHistory::kReportLinePrefix ) + " " + string( DebugReplyLine::kMaxBytes, '1' ), parsedSample ) );
}
