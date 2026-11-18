/**
 * @file TestEditorUICommandQueue.cpp
 * @brief UI 출력 계약(`EditorUICommandQueue`)을 봅니다 — UI 프레임 N 에 넣은 명령은 다음 게임 프레임 시작(`takeDue`)에 넣은 순서대로 나오고, 뒤 프레임 번호 칸은 그 프레임까지 남는다.
 * @details 큐는 지역 객체로 만든다. 에디터 범위의 큐(`EditorUICommands::post`)에 넣으면 실제 다음 프레임이 플레이 세션을 바꾼다.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/EditorUICommandQueue.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorUICommandQueueTest] UI 프레임이 넣은 명령은 그 뒤의 게임 프레임 시작에 처음 나온다(한 프레임 지연). 이미 꺼낸 프레임 뒤에 넣은 명령은 다음 프레임으로 간다
 */
SW_TEST_CASE( EditorUICommandQueueTest, PostedCommandAppearsNextFrame )
{
    EditorUICommandQueue    queue;
    vector<EditorUICommand> listDue;
    const uint32            firstFrame = queue.getCollectingFrame();

    // 게임 프레임 1 시작 — 아직 아무도 넣지 않았다.
    queue.takeDue( listDue );
    SW_EXPECT_TRUE( listDue.empty() );
    SW_EXPECT_EQUAL( firstFrame + 1, queue.getCollectingFrame() );

    // UI 프레임 1 이 Pause 를 넣는다 — 게임 프레임 1 은 이미 시작했으니 적용은 게임 프레임 2 다.
    SW_REQUIRE_TRUE( queue.post( EditorUICommand::make( EditorUICommandKind::Pause ) ) );
    queue.takeDue( listDue );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listDue.size() );
    SW_EXPECT_TRUE( listDue[0]._kind == EditorUICommandKind::Pause );

    // 한 번 꺼낸 명령은 다시 나오지 않는다.
    queue.takeDue( listDue );
    SW_EXPECT_TRUE( listDue.empty() );
}

/**
 * @brief [EditorUICommandQueueTest] 한 프레임에 넣은 명령은 넣은 순서대로 값째 나온다(뷰 크기 · Step 수 · 카메라에서 시작)
 */
SW_TEST_CASE( EditorUICommandQueueTest, KeepsOrderAndValues )
{
    EditorUICommandQueue    queue;
    vector<EditorUICommand> listDue;

    SW_REQUIRE_TRUE( queue.post( EditorUICommand::makeFitViewTarget( EditorViewKind::Game, 640, 360 ) ) );
    SW_REQUIRE_TRUE( queue.post( EditorUICommand::makeStart( false, true ) ) );
    SW_REQUIRE_TRUE( queue.post( EditorUICommand::makeStep( 7 ) ) );
    SW_REQUIRE_TRUE( queue.post( EditorUICommand::makeStart( true, false ) ) );
    SW_REQUIRE_TRUE( queue.post( EditorUICommand::make( EditorUICommandKind::Stop ) ) );
    queue.takeDue( listDue );

    SW_REQUIRE_EQUAL( size_t{ 5 }, listDue.size() );
    SW_EXPECT_TRUE( listDue[0]._kind == EditorUICommandKind::FitViewTarget );
    SW_EXPECT_TRUE( listDue[0]._view == EditorViewKind::Game );
    SW_EXPECT_EQUAL( 640u, listDue[0]._width );
    SW_EXPECT_EQUAL( 360u, listDue[0]._height );
    SW_EXPECT_TRUE( listDue[1]._kind == EditorUICommandKind::StartPlay );
    SW_EXPECT_TRUE( listDue[1]._bStartAtCamera == SW_TRUE );
    SW_EXPECT_TRUE( listDue[2]._kind == EditorUICommandKind::Step );
    SW_EXPECT_EQUAL( 7u, listDue[2]._stepFrameCount );
    SW_EXPECT_TRUE( listDue[3]._kind == EditorUICommandKind::StartSimulate );
    SW_EXPECT_TRUE( listDue[3]._bStartAtCamera == SW_FALSE );
    SW_EXPECT_TRUE( listDue[4]._kind == EditorUICommandKind::Stop );
}

/**
 * @brief [EditorUICommandQueueTest] 뒤 프레임 번호로 넣은 명령은 그 프레임까지 남고(먼저 넣은 순서를 지킨다), 지난 번호로 넣은 명령은 다음 프레임에 나온다
 */
SW_TEST_CASE( EditorUICommandQueueTest, FrameNumberedCommands )
{
    EditorUICommandQueue    queue;
    vector<EditorUICommand> listDue;
    const uint32            frame = queue.getCollectingFrame();

    SW_REQUIRE_TRUE( queue.postAt( EditorUICommand::makeStep( 1 ), frame + 1 ) );
    SW_REQUIRE_TRUE( queue.postAt( EditorUICommand::makeStep( 2 ), frame ) );
    queue.takeDue( listDue ); // 프레임 frame
    SW_REQUIRE_EQUAL( size_t{ 1 }, listDue.size() );
    SW_EXPECT_EQUAL( 2u, listDue[0]._stepFrameCount );

    SW_REQUIRE_TRUE( queue.post( EditorUICommand::makeStep( 3 ) ) ); // 프레임 frame + 1 에 모은다 — 남은 칸 뒤에 선다
    queue.takeDue( listDue );                                        // 프레임 frame + 1
    SW_REQUIRE_EQUAL( size_t{ 2 }, listDue.size() );
    SW_EXPECT_EQUAL( 1u, listDue[0]._stepFrameCount );
    SW_EXPECT_EQUAL( 3u, listDue[1]._stepFrameCount );

    SW_REQUIRE_TRUE( queue.postAt( EditorUICommand::makeStep( 4 ), frame ) ); // 이미 지난 번호
    queue.takeDue( listDue );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listDue.size() );
    SW_EXPECT_EQUAL( 4u, listDue[0]._stepFrameCount );
}

/**
 * @brief [EditorUICommandQueueTest] 종료는 적용하지 않은 명령을 버린다(모듈 리로드 · 종료 뒤의 첫 프레임이 옛 명령을 적용하지 않는다)
 */
SW_TEST_CASE( EditorUICommandQueueTest, ShutdownDropsPending )
{
    EditorUICommandQueue    queue;
    vector<EditorUICommand> listDue;
    const uint32            frame = queue.getCollectingFrame();

    SW_REQUIRE_TRUE( queue.post( EditorUICommand::make( EditorUICommandKind::Pause ) ) );
    SW_REQUIRE_TRUE( queue.postAt( EditorUICommand::make( EditorUICommandKind::Stop ), frame + 3 ) );
    queue.takeDue( listDue );
    SW_EXPECT_EQUAL( size_t{ 1 }, listDue.size() );
    queue.shutdownSubsystem();
    for ( uint32 index = 0; index < 4; ++index )
    {
        queue.takeDue( listDue );
        SW_EXPECT_TRUE( listDue.empty() );
    }
    SW_EXPECT_EQUAL( 0u, queue.getDroppedCount() );
}
