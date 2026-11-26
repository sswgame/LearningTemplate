// 콜라이더 손잡이 명령 순서(`EditorColliderHandleDragTracker`) — 누름은 잡기, 끄는 동안 레이마다 끌기, 놓으면 커밋하는 놓기, 대상이 사라지면 커밋 없는 놓기.
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Editor/Viewport/Model/EditorColliderHandleEdit.h"

namespace
{
    using sw::editor::EditorColliderHandleCommand;
    using sw::editor::EditorColliderHandleCommandKind;

    bool isKind( const sw::vector<EditorColliderHandleCommand>& listCommand, size_t index, EditorColliderHandleCommandKind kind )
    {
        return index < listCommand.size() && listCommand[index]._kind == kind;
    }
} // namespace

SW_TEST_CASE( EditorColliderHandleDragTrackerTest, PressDragReleaseIsCaptureDragCommittedEnd )
{
    sw::editor::EditorColliderHandleDragTracker tracker;
    sw::vector<EditorColliderHandleCommand>     listCommand;
    sw::editor::ColliderHandle                  handle{};
    handle._shapeIndex = 2;
    tracker.press( sw::GameObjectHandle::make( 7, 0 ), handle, listCommand );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listCommand.size() );
    SW_EXPECT_TRUE( isKind( listCommand, 0, EditorColliderHandleCommandKind::Capture ) );
    SW_EXPECT_TRUE( tracker.isDragging() );

    listCommand.clear();
    const sw::Ray ray{};
    SW_EXPECT_TRUE( tracker.step( true, true, &ray, listCommand ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listCommand.size() );
    SW_EXPECT_TRUE( isKind( listCommand, 0, EditorColliderHandleCommandKind::Drag ) );
    SW_EXPECT_EQUAL( uint32{ 2 }, listCommand[0]._handle._shapeIndex );
    SW_EXPECT_EQUAL( uint64{ 7 }, listCommand[0]._object.objectID() );

    listCommand.clear();
    SW_EXPECT_FALSE( tracker.step( true, false, &ray, listCommand ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listCommand.size() );
    SW_EXPECT_TRUE( isKind( listCommand, 0, EditorColliderHandleCommandKind::End ) );
    SW_EXPECT_TRUE( listCommand[0]._bCommit );
    SW_EXPECT_FALSE( tracker.isDragging() );
}

SW_TEST_CASE( EditorColliderHandleDragTrackerTest, LostTargetEndsWithoutCommit )
{
    sw::editor::EditorColliderHandleDragTracker tracker;
    sw::vector<EditorColliderHandleCommand>     listCommand;
    tracker.press( sw::GameObjectHandle::make( 3, 0 ), sw::editor::ColliderHandle{}, listCommand );
    listCommand.clear();
    SW_EXPECT_FALSE( tracker.step( false, true, nullptr, listCommand ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listCommand.size() );
    SW_EXPECT_TRUE( isKind( listCommand, 0, EditorColliderHandleCommandKind::End ) );
    SW_EXPECT_FALSE( listCommand[0]._bCommit );
}

SW_TEST_CASE( EditorColliderHandleDragTrackerTest, IdleStepSendsNothing )
{
    sw::editor::EditorColliderHandleDragTracker tracker;
    sw::vector<EditorColliderHandleCommand>     listCommand;
    SW_EXPECT_TRUE( tracker.step( true, true, nullptr, listCommand ) );
    SW_EXPECT_FALSE( tracker.step( false, false, nullptr, listCommand ) );
    SW_EXPECT_TRUE( listCommand.empty() );
}
