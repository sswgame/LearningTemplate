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

#endif // SW_PROFILER_TRACE_ENABLED
