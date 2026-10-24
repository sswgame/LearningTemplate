#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Graphics/Debug/VisualLogger.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// 비주얼 로거 — 꺼져 있으면 적지 않고, 프레임별 · 오브젝트별 · 분류별로 질의하며, 용량을 넘으면 오래된 것부터 버리고, 프레임을 디버그 드로우로 다시 보고, 파일로 왕복한다.

namespace
{
    const float4 kWhite{ 1.0f, 1.0f, 1.0f, 1.0f };
} // namespace

/**
 * @brief [VisualLoggerTest] 끈 채로는 적지 않고, 켜면 지금 프레임 번호로 적히며, 오브젝트 · 분류 · 프레임 범위로 걸러 낸다
 */
SW_TEST_CASE( VisualLoggerTest, RecordsOnlyWhenEnabledAndFiltersByFrameObjectAndCategory )
{
    VisualLogger logger;
    logger.logText( 7, "AI", float3{}, "ignored", kWhite );
    SW_EXPECT_EQUAL( size_t( 0 ), logger.getEntryCount() ); // 꺼져 있다

    logger.setEnabled( true );
    logger.logText( 7, "AI", float3{ 1.0f, 0.0f, 0.0f }, "chase", kWhite ); // 프레임 0
    logger.logSphere( 9, "Sense", float3{}, 2.0f, kWhite );                 // 프레임 0
    logger.endFrame();
    logger.logLine( 7, "AI", float3{}, float3{ 0.0f, 0.0f, 3.0f }, kWhite, "path" ); // 프레임 1
    logger.logBox( 0, {}, float3{}, float3{ 1.0f, 1.0f, 1.0f }, kWhite );            // 분류 없음 → Default
    logger.endFrame();
    SW_EXPECT_EQUAL( uint64( 2 ), logger.getFrame() );
    SW_EXPECT_EQUAL( size_t( 4 ), logger.getEntryCount() );

    uint64 firstFrame = 99;
    uint64 lastFrame  = 99;
    SW_ASSERT_TRUE( logger.findFrameRange( firstFrame, lastFrame ) );
    SW_EXPECT_EQUAL( uint64( 0 ), firstFrame );
    SW_EXPECT_EQUAL( uint64( 1 ), lastFrame );

    vector<VisualLogEntry> listEntry;
    logger.collect( 0, 1, 7, {}, listEntry ); // 오브젝트 7
    SW_EXPECT_EQUAL( size_t( 2 ), listEntry.size() );
    logger.collect( 0, 1, 0, hashed_string( "Sense" ), listEntry ); // 분류
    SW_ASSERT_EQUAL( size_t( 1 ), listEntry.size() );
    SW_EXPECT_TRUE( listEntry[0]._shape == VisualLogShape::Sphere );
    logger.collect( 1, 1, 0, {}, listEntry ); // 프레임 1 만
    SW_EXPECT_EQUAL( size_t( 2 ), listEntry.size() );
    SW_EXPECT_TRUE( listEntry[1]._category == hashed_string( VisualLogger::kDefaultCategoryName ) );
}

/**
 * @brief [VisualLoggerTest] 용량을 넘으면 가장 오래된 기록부터 버리고, 용량 0 은 거절한다
 */
SW_TEST_CASE( VisualLoggerTest, DropsTheOldestEntriesPastTheCapacity )
{
    VisualLogger logger;
    logger.setEnabled( true );
    SW_ASSERT_TRUE( logger.setCapacity( 3 ) );
    for ( uint32 frame = 0; frame < 5; ++frame )
    {
        logger.logText( 1, "AI", float3{}, "tick", kWhite );
        logger.endFrame();
    }
    SW_EXPECT_EQUAL( size_t( 3 ), logger.getEntryCount() );
    uint64 firstFrame = 0;
    uint64 lastFrame  = 0;
    SW_ASSERT_TRUE( logger.findFrameRange( firstFrame, lastFrame ) );
    SW_EXPECT_EQUAL( uint64( 2 ), firstFrame ); // 프레임 0 · 1 은 밀려났다
    SW_EXPECT_EQUAL( uint64( 4 ), lastFrame );

    SW_ASSERT_TRUE( logger.setCapacity( 1 ) ); // 줄이면 바로 버린다
    SW_EXPECT_EQUAL( size_t( 1 ), logger.getEntryCount() );

    SW_TEST_DEFENSIVE_SCOPE( "a visual log capacity of 0 is refused" );
    SW_EXPECT_FALSE( logger.setCapacity( 0 ) );
}

/**
 * @brief [VisualLoggerTest] 프레임을 다시 보면 그 프레임의 도형이 한 프레임짜리 디버그 드로우로 들어가고, 글이 붙은 도형은 글도 그린다
 */
SW_TEST_CASE( VisualLoggerTest, ReplayDrawsTheRecordedFrameThroughTheDebugDrawQueue )
{
    VisualLogger logger;
    logger.setEnabled( true );
    logger.logLine( 1, "AI", float3{}, float3{ 0.0f, 0.0f, 2.0f }, kWhite, "to target" ); // 프레임 0: 선 + 설명 글
    logger.logSphere( 1, "AI", float3{}, 1.0f, kWhite );                                  // 프레임 0: 구
    logger.endFrame();
    logger.logBox( 1, "AI", float3{}, float3{ 1.0f, 1.0f, 1.0f }, kWhite ); // 프레임 1

    DebugDrawQueue queue;
    SW_EXPECT_EQUAL( 2u, logger.replayFrame( queue, 0, 0, {} ) );
    queue.endFrame( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( size_t( 1 ), queue.getVisibleLines().size() );   // 선
    SW_EXPECT_EQUAL( size_t( 1 ), queue.getVisibleSpheres().size() ); // 구
    SW_EXPECT_EQUAL( size_t( 1 ), queue.getVisibleTexts().size() );   // 선의 설명 글
    queue.endFrame( 1.0f / 60.0f );
    SW_EXPECT_TRUE( queue.getVisibleLines().empty() ); // 한 프레임짜리다

    SW_EXPECT_EQUAL( 0u, logger.replayFrame( queue, 5, 0, {} ) ); // 기록이 없는 프레임
}

/**
 * @brief [VisualLoggerTest] 파일로 왕복하고, 깨진 파일은 지금 기록을 건드리지 않고 거절한다
 */
SW_TEST_CASE( VisualLoggerTest, RoundTripsThroughAFileAndRefusesABrokenOne )
{
    VisualLogger logger;
    logger.setEnabled( true );
    logger.logText( 3, "AI", float3{ 1.0f, 2.0f, 3.0f }, "hello", float4{ 1.0f, 0.5f, 0.25f, 1.0f } );
    logger.endFrame();
    logger.logArrow( 3, "Move", float3{}, float3{ 0.0f, 1.0f, 0.0f }, kWhite, "up" );
    logger.endFrame();
    const string path = FileUtil::joinPath( test::makeTempPath( "vlog" ), "session.vlog.json" );
    SW_ASSERT_TRUE( FileUtil::ensureParentDirectoryExists( path ) );
    SW_ASSERT_TRUE( logger.saveToFile( path ) );

    VisualLogger loaded;
    SW_ASSERT_TRUE( loaded.loadFromFile( path ) );
    SW_EXPECT_EQUAL( size_t( 2 ), loaded.getEntryCount() );
    SW_EXPECT_EQUAL( uint64( 2 ), loaded.getFrame() );
    vector<VisualLogEntry> listEntry;
    loaded.collect( 0, 1, 0, {}, listEntry );
    SW_ASSERT_EQUAL( size_t( 2 ), listEntry.size() );
    SW_EXPECT_STREQ( "hello", listEntry[0]._text.c_str() );
    SW_EXPECT_EQUAL( uint64( 3 ), listEntry[0]._objectID );
    SW_EXPECT_NEAR_EQUAL( 3.0f, listEntry[0]._a._z, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, listEntry[0]._color._y, 1.0e-5f );
    SW_EXPECT_TRUE( listEntry[1]._shape == VisualLogShape::Arrow );
    SW_EXPECT_TRUE( listEntry[1]._category == hashed_string( "Move" ) );

    SW_TEST_DEFENSIVE_SCOPE( "a visual log file with another version or a broken entry is refused" );
    const string brokenPath = FileUtil::joinPath( test::makeTempPath( "vlog" ), "broken.vlog.json" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( brokenPath, R"({"version": 99, "frame": 0, "entries": []})" ) );
    SW_EXPECT_FALSE( loaded.loadFromFile( brokenPath ) );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( brokenPath, R"({"version": 1, "frame": 0, "entries": [{"frame": 0}]})" ) );
    SW_EXPECT_FALSE( loaded.loadFromFile( brokenPath ) );
    SW_EXPECT_EQUAL( size_t( 2 ), loaded.getEntryCount() ); // 거절한 읽기는 지금 기록을 건드리지 않는다
}
