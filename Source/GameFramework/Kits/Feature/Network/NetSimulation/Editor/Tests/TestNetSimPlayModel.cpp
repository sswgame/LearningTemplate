#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/Editor/EditorNetSimSession.h"
#include "GameFramework/Kits/Feature/Network/NetSimulation/Editor/Model/NetSimPlayModel.h"
#include "GameFramework/Kits/Feature/Network/NetSimulation/Editor/Sample/NetSimSampleGame.h"
#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimGameRegistry.h"

#include <limits>

// NetSimPlayModelTest · EditorNetSimSessionTest — 한 프로세스 재생: 프레임 시간 → 고정 스텝, 뷰포트 칸 크기, 돌지 않는 세션의 답과 시작 거절(에디터 케이스, 씬을 세우지 않는다).

using namespace sw;
using namespace sw::editor;

SW_TEST_CASE( NetSimPlayModelTest, FrameTimeBecomesFixedSteps )
{
    NetSimStepPacer pacer;
    SW_EXPECT_EQUAL( uint32( 0 ), pacer.consume( 0.5, 1.0, 4 ) ); // 아직 한 스텝이 안 된다
    SW_EXPECT_EQUAL( uint32( 1 ), pacer.consume( 0.5, 1.0, 4 ) );
    SW_EXPECT_NEAR( 0.0, pacer.getAccumulatedSeconds(), 1e-9 );
    SW_EXPECT_EQUAL( uint32( 2 ), pacer.consume( 2.5, 1.0, 4 ) );
    SW_EXPECT_NEAR( 0.5, pacer.getAccumulatedSeconds(), 1e-9 ); // 남는 몫은 다음 프레임으로 간다
    pacer.reset();
    SW_EXPECT_EQUAL( 0.0, pacer.getAccumulatedSeconds() );
}

/** @brief 중단점 뒤 밀린 시간은 몰아 돌지 않는다. */
SW_TEST_CASE( NetSimPlayModelTest, BacklogBeyondTheCapIsDropped )
{
    NetSimStepPacer pacer;
    SW_EXPECT_EQUAL( uint32( 4 ), pacer.consume( 100.0, 1.0, 4 ) );
    SW_EXPECT_EQUAL( 0.0, pacer.getAccumulatedSeconds() );
    SW_EXPECT_EQUAL( uint32( 0 ), pacer.consume( 0.25, 1.0, 4 ) );
}

/** @brief 결함: 상한에 딱 닿기만 해도 남은 몫을 버려, 느린 프레임이 이어지면 재생 시간이 그만큼 빠졌다. */
SW_TEST_CASE( NetSimPlayModelTest, ReachingTheCapKeepsTheRemainder )
{
    NetSimStepPacer pacer;
    SW_EXPECT_EQUAL( uint32( 4 ), pacer.consume( 4.5, 1.0, 4 ) );
    SW_EXPECT_NEAR( 0.5, pacer.getAccumulatedSeconds(), 1e-9 );
}

/** @brief 결함: NaN 프레임 시간 하나로 모은 시간이 영원히 NaN 이 되어 재생이 말없이 멈췄다. 음수는 시간을 되돌렸다. */
SW_TEST_CASE( NetSimPlayModelTest, UnusableFrameTimeIsIgnored )
{
    NetSimStepPacer pacer;
    SW_EXPECT_EQUAL( uint32( 0 ), pacer.consume( 0.5, 1.0, 4 ) );
    SW_EXPECT_EQUAL( uint32( 0 ), pacer.consume( std::numeric_limits<float64>::quiet_NaN(), 1.0, 4 ) );
    SW_EXPECT_EQUAL( uint32( 0 ), pacer.consume( -3.0, 1.0, 4 ) );
    SW_EXPECT_NEAR( 0.5, pacer.getAccumulatedSeconds(), 1e-9 );
    SW_EXPECT_EQUAL( uint32( 1 ), pacer.consume( 0.5, 1.0, 4 ) ); // 이어서 돈다

    SW_EXPECT_EQUAL( uint32( 4 ), pacer.consume( std::numeric_limits<float64>::infinity(), 1.0, 4 ) ); // 무한대는 밀린 시간이라 버린다
    SW_EXPECT_EQUAL( 0.0, pacer.getAccumulatedSeconds() );
}

SW_TEST_CASE( NetSimPlayModelTest, ViewSizeSplitsTheWidthAndKeepsAMinimumHeight )
{
    const float2 three = NetSimWorldLayout::computeViewSize( float2{ 900.0f, 600.0f }, 3, 8.0f, 20.0f, 160.0f );
    SW_EXPECT_NEAR( 292.0f, three._x, 0.001f );
    SW_EXPECT_NEAR( 560.0f, three._y, 0.001f ); // 머리글 두 줄을 뺀다

    const float2 cramped = NetSimWorldLayout::computeViewSize( float2{ 4.0f, 50.0f }, 3, 8.0f, 20.0f, 160.0f ); // `small` 은 Windows 헤더의 매크로다
    SW_EXPECT_NEAR( 1.0f, cramped._x, 0.001f );                                                                 // 뷰포트 크기가 0 이하로 가지 않는다
    SW_EXPECT_NEAR( 160.0f, cramped._y, 0.001f );

    const float2 none = NetSimWorldLayout::computeViewSize( float2{ 100.0f, 600.0f }, 0, 8.0f, 20.0f, 160.0f );
    SW_EXPECT_NEAR( 92.0f, none._x, 0.001f ); // 0 으로 나누지 않는다
}

/** @brief 돌지 않는 세션은 값을 지어내지 않는다 — 탭과 자동화 감시 값이 돌기 전에도 부른다. */
SW_TEST_CASE( EditorNetSimSessionTest, IdleSessionAnswersEmpty )
{
    EditorNetSimSession session;
    SW_EXPECT_FALSE( session.isRunning() );
    SW_EXPECT_EQUAL( uint32( 0 ), session.update( 1.0 ) );
    SW_EXPECT_FALSE( session.setInputFocus( 1 ) );
    SW_EXPECT_EQUAL( -1, session.getInputFocus() );
    vector<int32> listWorldIndex{ 7 };
    session.collectWorldIndices( listWorldIndex );
    SW_EXPECT_TRUE( listWorldIndex.empty() ); // 앞의 값을 남기지 않는다
    SW_EXPECT_NULL( session.findViewport( 0 ) );
    SW_EXPECT_NULL( session.getHarness() );
    SW_EXPECT_EQUAL( 0, session.getViewportCount() );
    session.stop(); // 두 번 내려도 된다
}

SW_TEST_CASE( EditorNetSimSessionTest, StartRefusesWhatCannotRunAndLeavesNothing )
{
    EditorNetSimSession  session;
    EditorNetSimSettings settings;
    settings._gameName           = "no such game";
    const expected<void> unknown = session.start( settings );
    SW_REQUIRE_FALSE( unknown.has_value() );
    SW_EXPECT_TRUE( unknown.error().detail() == "no such game" );
    SW_EXPECT_FALSE( session.isRunning() );

    settings._gameName = NetSimSampleGame::kName; // 이 확장이 올린다
    SW_REQUIRE_NOT_NULL( NetSimGameRegistry::findGame( settings._gameName ) );
    for ( const int32 clientCount : { 0, -1, EditorNetSimSession::kMaxClients + 1 } )
    {
        settings._clientCount        = clientCount;
        const expected<void> started = session.start( settings ); // 범위로 맞추지 않는다
        SW_REQUIRE_FALSE( started.has_value() );
        SW_EXPECT_FALSE( started.error().message().empty() );
        SW_EXPECT_FALSE( session.isRunning() );
    }
}

/** @brief 본보기 게임이 뷰포트에 아레나 전체를 잡아 준다. */
SW_TEST_CASE( EditorNetSimSessionTest, SampleGameFramesItsArena )
{
    NetSimSampleSettings settings;
    settings._arenaHalfSize = 10.0f;
    const NetSimSampleGame game{ settings };
    NetSimPreviewFraming   framing;
    SW_REQUIRE_TRUE( game.describePreviewFraming( framing ) );
    SW_EXPECT_NEAR( 30.0f, framing._distance, 0.001f );
    SW_EXPECT_TRUE( framing._floorSize > settings._arenaHalfSize * 2.0f ); // 바닥이 벽 밖으로 더 나간다
}
