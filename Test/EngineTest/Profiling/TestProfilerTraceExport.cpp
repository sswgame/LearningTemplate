#include "pch.h"

#include "Engine/Profiling/ProfilerTraceExport.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include "TestFramework/TestFramework.h"

#if SW_PROFILER_TRACE_ENABLED

// ProfilerTraceExport — Trace Event JSON 의 형식 · 이름 · 마이크로초 단위 · 머리말.

namespace
{
    sw::vector<sw::ProfilerTimelineThread> makeThreads()
    {
        sw::vector<sw::ProfilerTimelineThread> listThread;
        sw::ProfilerTimelineThread             main;
        main._name = "Main \"GT\"";
        main._listEvent.push_back( { 1000, 5500, 0, 0 } );
        main._listEvent.push_back( { 2000, 2500, 1, 1 } );
        listThread.push_back( main );
        sw::ProfilerTimelineThread worker;
        worker._name = "Worker 1";
        worker._listEvent.push_back( { 3000, 3001, 9, 0 } ); // 이름 표 밖의 슬롯
        listThread.push_back( worker );
        return listThread;
    }

    sw::ProfilerTraceMeta makeMeta()
    {
        sw::ProfilerTraceMeta meta;
        meta._processName          = "Game";
        meta._processID            = 4242;
        meta._wallUnixMilliseconds = 1700000000123;
        meta._monotonicNanoseconds = 987654321;
        return meta;
    }
} // namespace

/**
 * @brief [ProfilerTraceExportTest] 사건은 완전 이벤트 · 스레드 이름은 메타 이벤트 · 프레임은 인스턴트로 나가고 시각은 마이크로초다
 */
SW_TEST_CASE( ProfilerTraceExportTest, EmitsCompleteEventsMetaAndFrameMarks )
{
    const sw::vector<sw::string> listName{ "GT.Frame", "GT.Scene.tick" };
    const sw::string             json = sw::ProfilerTraceExport::makeTraceJSON( makeThreads(), { 1000, 6000 }, listName, makeMeta() );

    sw::JSONDocument document;
    SW_ASSERT_TRUE( document.parse( json ) );
    const sw::JSONValue root = document.getRoot();
    SW_EXPECT_EQUAL( sw::string( "Game" ), root.get( "otherData" ).get( "processName" ).asString() );
    SW_EXPECT_EQUAL( static_cast<int64>( 4242 ), root.get( "otherData" ).get( "processID" ).read<int64>().value() );
    SW_EXPECT_EQUAL( static_cast<int64>( 1700000000123 ), root.get( "otherData" ).get( "wallUnixMilliseconds" ).read<int64>().value() );
    SW_EXPECT_EQUAL( static_cast<int64>( 987654321 ), root.get( "otherData" ).get( "monotonicNanoseconds" ).read<int64>().value() );

    const sw::JSONValue events = root.get( "traceEvents" );
    SW_ASSERT_TRUE( events.isArray() );
    size_t completeCount = 0;
    size_t metaCount     = 0;
    size_t instantCount  = 0;
    bool   bFoundFirst   = false;
    bool   bFoundUnnamed = false;
    bool   bFoundThread  = false;
    for ( size_t index = 0; index < events.size(); ++index )
    {
        const sw::JSONValue event = events.at( index );
        const sw::string    phase = event.get( "ph" ).asString();
        if ( phase == "X" )
        {
            ++completeCount;
            SW_EXPECT_EQUAL( static_cast<int64>( 4242 ), event.get( "pid" ).read<int64>().value() );
            if ( event.get( "name" ).asString() == "GT.Frame" )
            {
                bFoundFirst = true;
                SW_EXPECT_EQUAL( 1.0, event.get( "ts" ).read<float64>().value() );  // 1000 ns = 1 us
                SW_EXPECT_EQUAL( 4.5, event.get( "dur" ).read<float64>().value() ); // 4500 ns = 4.5 us
                SW_EXPECT_EQUAL( static_cast<int64>( 1 ), event.get( "tid" ).read<int64>().value() );
            }
            if ( event.get( "name" ).asString() == "slot 9" )
                bFoundUnnamed = true;
        }
        else if ( phase == "M" )
        {
            ++metaCount;
            if ( event.get( "name" ).asString() == "thread_name" && event.get( "args" ).get( "name" ).asString() == "Main \"GT\"" )
                bFoundThread = true;
        }
        else if ( phase == "i" )
        {
            ++instantCount;
        }
    }
    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), completeCount );
    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), metaCount ); // process_name + 스레드 둘
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), instantCount );
    SW_EXPECT_TRUE( bFoundFirst );
    SW_EXPECT_TRUE( bFoundUnnamed );
    SW_EXPECT_TRUE( bFoundThread );
}

/**
 * @brief [ProfilerTraceExportTest] `JSONWriter` 로 옮긴 뒤에도 출력 바이트가 손으로 이어 붙이던 때와 같다(이스케이프 · 마이크로초 세 자리 · 잘림 · 표식 · 흐름)
 * @details 기대 글은 옮기기 전 코드(`appendQuoted` + `JSONDocument::escapeString`, `appendMicroseconds`)가 내던 바이트다. 뷰어 · 합치기 · 외부 도구가 이 모양을 읽는다.
 */
SW_TEST_CASE( ProfilerTraceExportTest, OutputBytesMatchTheConcatenatedFormat )
{
    sw::ProfilerTraceMeta meta = makeMeta();
    meta._hostName             = "PC\t1";
    meta._bTruncated           = true;
    meta._listMarker.push_back( { "Slow \"tick\"", 2000 } );
    meta._listFlow.push_back( { 3000, 0x1ABCull, 0x81, false } );
    meta._listFlow.push_back( { 4000, 0x1ABCull, 0x81, true } );
    const sw::string json = sw::ProfilerTraceExport::makeTraceJSON( makeThreads(), { 1000 }, { "GT.Frame", "GT.Scene.tick" }, meta );

    const sw::string expected =
        R"json({"displayTimeUnit":"ms","otherData":{"processName":"Game","hostName":"PC\t1","processID":4242,"wallUnixMilliseconds":1700000000123,)json"
        R"json("monotonicNanoseconds":987654321,"truncated":true},"traceEvents":[)json"
        R"json({"name":"process_name","ph":"M","pid":4242,"tid":0,"args":{"name":"Game"}},)json"
        R"json({"name":"thread_name","ph":"M","pid":4242,"tid":1,"args":{"name":"Main \"GT\""}},)json"
        R"json({"name":"GT.Frame","cat":"cpu","ph":"X","pid":4242,"tid":1,"ts":1.000,"dur":4.500},)json"
        R"json({"name":"GT.Scene.tick","cat":"cpu","ph":"X","pid":4242,"tid":1,"ts":2.000,"dur":0.500},)json"
        R"json({"name":"thread_name","ph":"M","pid":4242,"tid":2,"args":{"name":"Worker 1"}},)json"
        R"json({"name":"slot 9","cat":"cpu","ph":"X","pid":4242,"tid":2,"ts":3.000,"dur":0.001},)json"
        R"json({"name":"TRUNCATED (event budget reached)","cat":"trace","ph":"i","s":"g","pid":4242,"tid":0,"ts":5.500},)json"
        R"json({"name":"Slow \"tick\"","cat":"marker","ph":"i","s":"g","pid":4242,"tid":0,"ts":2.000},)json"
        R"json({"name":"Frame","cat":"frame","ph":"i","s":"g","pid":4242,"tid":0,"ts":1.000},)json"
        R"json({"name":"thread_name","ph":"M","pid":4242,"tid":3,"args":{"name":"Net messages"}},)json"
        R"json({"name":"send 0x81","cat":"net","ph":"X","pid":4242,"tid":3,"ts":3.000,"dur":1},)json"
        R"json({"name":"net","cat":"net","ph":"s","id":"0x1ABC","pid":4242,"tid":3,"ts":3.000},)json"
        R"json({"name":"recv 0x81","cat":"net","ph":"X","pid":4242,"tid":3,"ts":4.000,"dur":1},)json"
        R"json({"name":"net","cat":"net","ph":"f","bp":"e","id":"0x1ABC","pid":4242,"tid":3,"ts":4.000}]})json"
        "\n";
    SW_EXPECT_EQUAL( expected, json );
}

/**
 * @brief [ProfilerTraceExportTest] 사건이 없어도 파일은 유효한 JSON 이다
 */
SW_TEST_CASE( ProfilerTraceExportTest, EmptyInputIsStillValidJSON )
{
    const sw::string json = sw::ProfilerTraceExport::makeTraceJSON( {}, {}, {}, makeMeta() );
    sw::JSONDocument document;
    SW_ASSERT_TRUE( document.parse( json ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), document.getRoot().get( "traceEvents" ).size() ); // process_name 하나
}

/**
 * @brief [ProfilerTraceExportTest] `profile.trace` 첫 낱말 — 하위 명령(save · record · merge)은 그 글자 그대로, 켬 · 끔은 불리언 철자 표 하나로 읽는다
 * @details 하위 명령 철자가 불리언 철자와 겹치지 않는다(겹치면 어느 쪽으로 읽을지 갈린다). 모르는 글은 Unknown 이다.
 */
SW_TEST_CASE( ProfilerTraceExportTest, CommandWordUsesOneBoolSpellingTable )
{
    using Word = sw::ProfilerTraceExport::CommandWord;
    bool bOn{ false };
    SW_EXPECT_TRUE( sw::ProfilerTraceExport::parseCommandWord( "save", bOn ) == Word::Save );
    SW_EXPECT_TRUE( sw::ProfilerTraceExport::parseCommandWord( "record", bOn ) == Word::Record );
    SW_EXPECT_TRUE( sw::ProfilerTraceExport::parseCommandWord( "merge", bOn ) == Word::Merge );
    for ( const utf8* pText : { "on", "ON", "1", "true", "yes" } )
    {
        bOn = false;
        SW_EXPECT_TRUE_MSG( sw::ProfilerTraceExport::parseCommandWord( pText, bOn ) == Word::Switch && bOn, pText );
    }
    for ( const utf8* pText : { "off", "Off", "0", "false", "no" } )
    {
        bOn = true;
        SW_EXPECT_TRUE_MSG( sw::ProfilerTraceExport::parseCommandWord( pText, bOn ) == Word::Switch && bOn == false, pText );
    }
    for ( const utf8* pText : { "", "2", "start", "Save" } )
    {
        SW_EXPECT_TRUE_MSG( sw::ProfilerTraceExport::parseCommandWord( pText, bOn ) == Word::Unknown, pText );
    }
}

/**
 * @brief [ProfilerTraceExportTest] 잘린 캡처는 머리말과 전역 표식으로 알린다
 */
SW_TEST_CASE( ProfilerTraceExportTest, TruncatedCaptureIsMarked )
{
    sw::ProfilerTraceMeta meta = makeMeta();
    meta._bTruncated           = true;
    const sw::string json      = sw::ProfilerTraceExport::makeTraceJSON( makeThreads(), {}, { "a", "b" }, meta );
    sw::JSONDocument document;
    SW_ASSERT_TRUE( document.parse( json ) );
    const sw::JSONValue root = document.getRoot();
    SW_EXPECT_TRUE( root.get( "otherData" ).get( "truncated" ).read<bool>().value() );
    bool bFoundMark = false;
    for ( size_t index = 0; index < root.get( "traceEvents" ).size(); ++index )
    {
        if ( root.get( "traceEvents" ).at( index ).get( "name" ).asString().find( "TRUNCATED" ) != sw::string::npos )
            bFoundMark = true;
    }
    SW_EXPECT_TRUE( bFoundMark );
}

/**
 * @brief [ProfilerTraceExportTest] 낸 파일을 다시 읽으면 스레드 이름 · 구간 이름 · 시각 · 깊이 · 프레임 경계가 돌아온다
 */
SW_TEST_CASE( ProfilerTraceExportTest, WrittenFileParsesBackWithDepthAndNames )
{
    const sw::vector<sw::string> listName{ "GT.Frame", "GT.Scene.tick" };
    const sw::string             json = sw::ProfilerTraceExport::makeTraceJSON( makeThreads(), { 1000, 6000 }, listName, makeMeta() );

    sw::ProfilerLoadedTrace trace;
    sw::string              error;
    SW_ASSERT_TRUE( sw::ProfilerTraceExport::parseTraceJSON( json, trace, error ) );
    SW_EXPECT_EQUAL( sw::string( "Game" ), trace._meta._processName );
    SW_EXPECT_EQUAL( static_cast<int32>( 4242 ), trace._meta._processID );
    SW_EXPECT_EQUAL( static_cast<int64>( 1700000000123 ), trace._meta._wallUnixMilliseconds );
    SW_ASSERT_EQUAL( static_cast<size_t>( 2 ), trace._listThread.size() );
    SW_EXPECT_EQUAL( sw::string( "Main \"GT\"" ), trace._listThread[0]._name );
    SW_ASSERT_EQUAL( static_cast<size_t>( 2 ), trace._listThread[0]._listEvent.size() );
    const sw::ProfilerTimelineEvent& outer = trace._listThread[0]._listEvent[0];
    const sw::ProfilerTimelineEvent& inner = trace._listThread[0]._listEvent[1];
    SW_EXPECT_EQUAL( 1000ull, outer._beginNanos );
    SW_EXPECT_EQUAL( 5500ull, outer._endNanos );
    SW_EXPECT_EQUAL( static_cast<uint16>( 0 ), outer._depth );
    SW_EXPECT_EQUAL( static_cast<uint16>( 1 ), inner._depth ); // 겹침으로 다시 센다
    SW_EXPECT_EQUAL( sw::string( "GT.Frame" ), trace._listSlotName[outer._slot] );
    SW_EXPECT_EQUAL( sw::string( "GT.Scene.tick" ), trace._listSlotName[inner._slot] );
    SW_ASSERT_EQUAL( static_cast<size_t>( 2 ), trace._listFrameBegin.size() );
    SW_EXPECT_EQUAL( 6000ull, trace._listFrameBegin[1] );
    SW_EXPECT_EQUAL( 1000ull, trace._beginNanos );
    SW_EXPECT_EQUAL( 5500ull, trace._endNanos );
}

/**
 * @brief [ProfilerTraceExportTest] 깨진 글 · 사건 없는 글은 이유와 함께 거절한다
 */
SW_TEST_CASE( ProfilerTraceExportTest, ParseRejectsBrokenAndEmptyInput )
{
    SW_TEST_DEFENSIVE_SCOPE( "broken and empty input is rejected" ); // 거절 · 실패 로그가 이 테스트가 보려는 동작이다
    sw::ProfilerLoadedTrace trace;
    sw::string              error;
    SW_EXPECT_FALSE( sw::ProfilerTraceExport::parseTraceJSON( "not json", trace, error ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_FALSE( sw::ProfilerTraceExport::parseTraceJSON( "{\"traceEvents\":[]}", trace, error ) );
    SW_EXPECT_FALSE( sw::ProfilerTraceExport::parseTraceJSON( "{\"other\":1}", trace, error ) );
    // 배열만 있는 형식과 다른 도구가 낸 최소 파일도 읽는다.
    SW_EXPECT_TRUE( sw::ProfilerTraceExport::parseTraceJSON( "[{\"name\":\"a\",\"ph\":\"X\",\"pid\":1,\"tid\":7,\"ts\":5,\"dur\":2}]", trace, error ) );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), trace._listThread.size() );
    SW_EXPECT_EQUAL( sw::string( "tid 7" ), trace._listThread[0]._name );
}

namespace
{
    /** @brief 합치기 테스트용 — 이름 · 호스트 · pid · 시계 쌍만 다른 트레이스 하나. */
    sw::string makeProcessTrace( const sw::string& processName, const sw::string& hostName, int32 processID, int64 wallMilliseconds,
                                 uint64 monotonicNanoseconds )
    {
        sw::ProfilerTraceMeta meta = makeMeta();
        meta._processName          = processName;
        meta._hostName             = hostName;
        meta._processID            = processID;
        meta._wallUnixMilliseconds = wallMilliseconds;
        meta._monotonicNanoseconds = monotonicNanoseconds;
        return sw::ProfilerTraceExport::makeTraceJSON( makeThreads(), { 1000 }, { "GT.Frame", "GT.Scene.tick" }, meta );
    }

    /** @brief 합친 문서에서 @p pid 의 "GT.Frame" 완전 이벤트 시각(마이크로초)입니다. 없으면 -1. */
    float64 findFrameTimestamp( const sw::JSONValue& events, int64 pid )
    {
        for ( size_t index = 0; index < events.size(); ++index )
        {
            const sw::JSONValue event                 = events.at( index );
            const bool          bGameThreadFrameEvent = event.get( "ph" ).asString() == "X" && event.get( "name" ).asString() == "GT.Frame" && event.get( "pid" ).read<int64>().value() == pid;
            if ( bGameThreadFrameEvent )
                return event.get( "ts" ).read<float64>().value();
        }
        return -1.0;
    }
} // namespace

/**
 * @brief [ProfilerTraceExportTest] 같은 기계의 두 프로세스는 단조 시계를 나눠 쓰므로 옮기지 않고, 프로세스마다 줄 묶음이 하나다
 */
SW_TEST_CASE( ProfilerTraceExportTest, MergeKeepsSameHostTimestamps )
{
    const sw::vector<sw::ProfilerTraceMergeInput> listInput{
        {makeProcessTrace( "Server", "PC1", 100, 1700000000000, 987654321 ),        ""},
        {makeProcessTrace( "Client", "PC1", 200, 1700000000999, 987654321 ), "Client1"},
    };
    sw::string json;
    sw::string report;
    SW_ASSERT_TRUE( sw::ProfilerTraceExport::mergeTraceJSON( listInput, json, report ) );

    sw::JSONDocument document;
    SW_ASSERT_TRUE( document.parse( json ) );
    const sw::JSONValue root   = document.getRoot();
    const sw::JSONValue events = root.get( "traceEvents" );
    SW_EXPECT_EQUAL( 1.0, findFrameTimestamp( events, 100 ) );
    SW_EXPECT_EQUAL( 1.0, findFrameTimestamp( events, 200 ) ); // 벽시계가 달라도 같은 기계면 그대로
    const sw::JSONValue sources = root.get( "otherData" ).get( "sources" );
    SW_ASSERT_EQUAL( static_cast<size_t>( 2 ), sources.size() );
    SW_EXPECT_EQUAL( sw::string( "Client1" ), sources.at( 1 ).get( "label" ).asString() );
    SW_EXPECT_EQUAL( sw::string( "same host (exact)" ), sources.at( 1 ).get( "alignment" ).asString() );

    size_t processNameCount = 0;
    for ( size_t index = 0; index < events.size(); ++index )
    {
        if ( events.at( index ).get( "name" ).asString() == "process_name" )
            ++processNameCount;
    }
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), processNameCount );

    // 다시 읽으면 스레드 줄 이름 앞에 프로세스가 붙는다(두 프로세스의 같은 스레드 이름을 가른다).
    sw::ProfilerLoadedTrace trace;
    sw::string              error;
    SW_ASSERT_TRUE( sw::ProfilerTraceExport::parseTraceJSON( json, trace, error ) );
    SW_ASSERT_EQUAL( static_cast<size_t>( 4 ), trace._listThread.size() );
    SW_EXPECT_TRUE( trace._listThread[0]._name.find( "Server" ) == 0 );
    SW_EXPECT_TRUE( trace._listThread[2]._name.find( "Client1" ) == 0 );
}

/**
 * @brief [ProfilerTraceExportTest] 다른 기계의 파일은 벽시계 − 단조 시계 차이만큼 옮기고, 겹치는 pid 는 새 번호를 받고, 흐름 이벤트는 그대로 간다
 */
SW_TEST_CASE( ProfilerTraceExportTest, MergeShiftsOtherHostsByWallClock )
{
    // 기준: 벽 T, 단조 M. 다른 기계: 벽 T + 1000 ms, 단조 M + 500 ms → 그 기계의 사건은 500 ms 뒤로 간다.
    sw::string clientJSON = makeProcessTrace( "Client", "PC2", 100, 1700000001000, 987654321 + 500000000ull );
    // 흐름 끝(`f`)을 하나 붙인다 — id 로 보낸 쪽과 이어진다.
    const size_t closing = clientJSON.rfind( "]}" );
    SW_ASSERT_TRUE( closing != sw::string::npos );
    clientJSON.insert( closing, ",{\"name\":\"msg\",\"cat\":\"net\",\"ph\":\"f\",\"bp\":\"e\",\"id\":77,\"pid\":100,\"tid\":1,\"ts\":2}" );

    const sw::vector<sw::ProfilerTraceMergeInput> listInput{
        { makeProcessTrace( "Server", "PC1", 100, 1700000000000, 987654321 ), "" },
        { clientJSON, "" },
    };
    sw::string json;
    sw::string report;
    SW_ASSERT_TRUE( sw::ProfilerTraceExport::mergeTraceJSON( listInput, json, report ) );

    sw::JSONDocument document;
    SW_ASSERT_TRUE( document.parse( json ) );
    const sw::JSONValue events = document.getRoot().get( "traceEvents" );
    SW_EXPECT_EQUAL( 1.0, findFrameTimestamp( events, 100 ) );
    SW_EXPECT_EQUAL( 500001.0, findFrameTimestamp( events, 101 ) ); // pid 100 이 겹쳐 101

    bool bFoundFlow = false;
    for ( size_t index = 0; index < events.size(); ++index )
    {
        const sw::JSONValue event = events.at( index );
        if ( event.get( "ph" ).asString() == "f" )
        {
            bFoundFlow = true;
            SW_EXPECT_EQUAL( static_cast<int64>( 77 ), event.get( "id" ).read<int64>().value() );
            SW_EXPECT_EQUAL( static_cast<int64>( 101 ), event.get( "pid" ).read<int64>().value() );
            SW_EXPECT_EQUAL( 500002.0, event.get( "ts" ).read<float64>().value() );
        }
    }
    SW_EXPECT_TRUE( bFoundFlow );
    const sw::JSONValue sources = document.getRoot().get( "otherData" ).get( "sources" );
    SW_EXPECT_EQUAL( static_cast<int64>( 500000000 ), sources.at( 1 ).get( "shiftNanoseconds" ).read<int64>().value() );
}

/**
 * @brief [ProfilerTraceExportTest] 하나뿐이거나 깨진 입력은 이유와 함께 거절한다
 */
SW_TEST_CASE( ProfilerTraceExportTest, MergeRejectsBadInput )
{
    SW_TEST_DEFENSIVE_SCOPE( "bad input is rejected" ); // 거절 · 실패 로그가 이 테스트가 보려는 동작이다
    sw::string                              json;
    sw::string                              report;
    sw::vector<sw::ProfilerTraceMergeInput> listInput;
    listInput.push_back( { makeProcessTrace( "A", "PC1", 1, 0, 0 ), "" } );
    SW_EXPECT_FALSE( sw::ProfilerTraceExport::mergeTraceJSON( listInput, json, report ) );
    SW_EXPECT_FALSE( report.empty() );
    report.clear();
    listInput.push_back( { "not json", "" } );
    SW_EXPECT_FALSE( sw::ProfilerTraceExport::mergeTraceJSON( listInput, json, report ) );
    SW_EXPECT_FALSE( report.empty() );
}

/**
 * @brief [ProfilerTraceExportTest] 표식(느린 틱)은 전역 인스턴트로 나가고 머리말에 호스트 이름이 있다
 */
SW_TEST_CASE( ProfilerTraceExportTest, MarkersAndHostNameAreWritten )
{
    sw::ProfilerTraceMeta meta = makeMeta();
    meta._hostName             = "PC1";
    meta._listMarker.push_back( { "Slow tick 45.2 ms", 2000 } );
    const sw::string json = sw::ProfilerTraceExport::makeTraceJSON( makeThreads(), {}, { "a", "b" }, meta );

    sw::JSONDocument document;
    SW_ASSERT_TRUE( document.parse( json ) );
    SW_EXPECT_EQUAL( sw::string( "PC1" ), document.getRoot().get( "otherData" ).get( "hostName" ).asString() );
    const sw::JSONValue events     = document.getRoot().get( "traceEvents" );
    bool                bFoundMark = false;
    for ( size_t index = 0; index < events.size(); ++index )
    {
        const sw::JSONValue event = events.at( index );
        if ( event.get( "name" ).asString() == "Slow tick 45.2 ms" )
        {
            bFoundMark = true;
            SW_EXPECT_EQUAL( sw::string( "i" ), event.get( "ph" ).asString() );
            SW_EXPECT_EQUAL( 2.0, event.get( "ts" ).read<float64>().value() );
        }
    }
    SW_EXPECT_TRUE( bFoundMark );

    sw::ProfilerLoadedTrace trace;
    sw::string              error;
    SW_ASSERT_TRUE( sw::ProfilerTraceExport::parseTraceJSON( json, trace, error ) );
    SW_EXPECT_EQUAL( sw::string( "PC1" ), trace._meta._hostName );
}

/**
 * @brief [ProfilerTraceExportTest] 네트워크 흐름은 "Net messages" 줄에 구간 하나와 흐름 이벤트(보냄 `s`, 받음 `f` + `bp:e`)로 나가고 id 는 16 진 글이다
 * @details 흐름 이벤트는 감싸는 구간에 묶여야 뷰어가 화살표를 그린다. 같은 시각의 1 us 구간이 그 자리다. 읽기(`parseTraceJSON`)는 구간만 읽는다.
 */
SW_TEST_CASE( ProfilerTraceExportTest, NetFlowsAreWrittenAsBoundFlowEvents )
{
    sw::ProfilerTraceMeta meta = makeMeta();
    meta._listFlow.push_back( { 3000, 0x1ABCull, 0x81, false } );
    meta._listFlow.push_back( { 4000, 0x1ABCull, 0x81, true } );
    const sw::string json = sw::ProfilerTraceExport::makeTraceJSON( makeThreads(), {}, { "a", "b" }, meta );

    sw::JSONDocument document;
    SW_ASSERT_TRUE( document.parse( json ) );
    const sw::JSONValue events       = document.getRoot().get( "traceEvents" );
    int64               flowTid      = -1;
    uint32              sliceCount   = 0;
    bool                bFoundStart  = false;
    bool                bFoundFinish = false;
    for ( size_t index = 0; index < events.size(); ++index )
    {
        const sw::JSONValue event = events.at( index );
        const sw::string    phase = event.get( "ph" ).asString();
        if ( phase == "M" && event.get( "args" ).get( "name" ).asString() == "Net messages" )
            flowTid = event.get( "tid" ).read<int64>().value();
        if ( phase == "X" && event.get( "cat" ).asString() == "net" )
            ++sliceCount;
        if ( phase == "s" )
        {
            bFoundStart = true;
            SW_EXPECT_EQUAL( sw::string( "0x1ABC" ), event.get( "id" ).asString() );
            SW_EXPECT_EQUAL( 3.0, event.get( "ts" ).read<float64>().value() );
        }
        if ( phase == "f" )
        {
            bFoundFinish = true;
            SW_EXPECT_EQUAL( sw::string( "0x1ABC" ), event.get( "id" ).asString() );
            SW_EXPECT_EQUAL( sw::string( "e" ), event.get( "bp" ).asString() );
            SW_EXPECT_EQUAL( flowTid, event.get( "tid" ).read<int64>().value() );
        }
    }
    SW_EXPECT_TRUE( flowTid > 0 );
    SW_EXPECT_EQUAL( 2u, sliceCount );
    SW_EXPECT_TRUE( bFoundStart );
    SW_EXPECT_TRUE( bFoundFinish );

    sw::ProfilerLoadedTrace trace;
    sw::string              error;
    SW_EXPECT_TRUE( sw::ProfilerTraceExport::parseTraceJSON( json, trace, error ) );
}

#endif // SW_PROFILER_TRACE_ENABLED
