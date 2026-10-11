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
    SW_EXPECT_EQUAL( static_cast<int64>( 4242 ), root.get( "otherData" ).get( "processID" ).asInt() );
    SW_EXPECT_EQUAL( static_cast<int64>( 1700000000123 ), root.get( "otherData" ).get( "wallUnixMilliseconds" ).asInt() );
    SW_EXPECT_EQUAL( static_cast<int64>( 987654321 ), root.get( "otherData" ).get( "monotonicNanoseconds" ).asInt() );

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
            SW_EXPECT_EQUAL( static_cast<int64>( 4242 ), event.get( "pid" ).asInt() );
            if ( event.get( "name" ).asString() == "GT.Frame" )
            {
                bFoundFirst = true;
                SW_EXPECT_EQUAL( 1.0, event.get( "ts" ).asFloat() );  // 1000 ns = 1 us
                SW_EXPECT_EQUAL( 4.5, event.get( "dur" ).asFloat() ); // 4500 ns = 4.5 us
                SW_EXPECT_EQUAL( static_cast<int64>( 1 ), event.get( "tid" ).asInt() );
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
    SW_EXPECT_TRUE( root.get( "otherData" ).get( "truncated" ).asBool() );
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
            const sw::JSONValue event = events.at( index );
            if ( event.get( "ph" ).asString() == "X" && event.get( "name" ).asString() == "GT.Frame" && event.get( "pid" ).asInt() == pid )
                return event.get( "ts" ).asFloat();
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
            SW_EXPECT_EQUAL( static_cast<int64>( 77 ), event.get( "id" ).asInt() );
            SW_EXPECT_EQUAL( static_cast<int64>( 101 ), event.get( "pid" ).asInt() );
            SW_EXPECT_EQUAL( 500002.0, event.get( "ts" ).asFloat() );
        }
    }
    SW_EXPECT_TRUE( bFoundFlow );
    const sw::JSONValue sources = document.getRoot().get( "otherData" ).get( "sources" );
    SW_EXPECT_EQUAL( static_cast<int64>( 500000000 ), sources.at( 1 ).get( "shiftNanoseconds" ).asInt() );
}

/**
 * @brief [ProfilerTraceExportTest] 하나뿐이거나 깨진 입력은 이유와 함께 거절한다
 */
SW_TEST_CASE( ProfilerTraceExportTest, MergeRejectsBadInput )
{
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
            SW_EXPECT_EQUAL( 2.0, event.get( "ts" ).asFloat() );
        }
    }
    SW_EXPECT_TRUE( bFoundMark );

    sw::ProfilerLoadedTrace trace;
    sw::string              error;
    SW_ASSERT_TRUE( sw::ProfilerTraceExport::parseTraceJSON( json, trace, error ) );
    SW_EXPECT_EQUAL( sw::string( "PC1" ), trace._meta._hostName );
}

#endif // SW_PROFILER_TRACE_ENABLED
