#include "pch.h"

#include "Engine/Profiling/ProfilerTraceExport.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include "TestFramework/TestFramework.h"

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
