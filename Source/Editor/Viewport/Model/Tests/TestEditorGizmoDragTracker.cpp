// 기즈모 명령 순서(`EditorGizmoDragTracker`) — 잡기 → 끌기 → 놓기, 끄는 중 대상이 바뀌면 앞 드래그를 닫고 새 대상을 잡는다, 잡기 없는 끌기 · 끌기 없는 잡기.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Viewport/Model/EditorGizmoEdit.h"

namespace
{
    using sw::editor::EditorGizmoCommand;
    using sw::editor::EditorGizmoCommandKind;

    sw::editor::EditorGizmoTarget makeTarget( sw::uint64 objectID )
    {
        return sw::editor::EditorGizmoTarget{ sw::GameObjectHandle::make( objectID ), sw::float4x4{} };
    }

    bool isKind( const sw::vector<EditorGizmoCommand>& listCommand, size_t index, EditorGizmoCommandKind kind )
    {
        return index < listCommand.size() && listCommand[index]._kind == kind;
    }
} // namespace

SW_TEST_CASE( EditorGizmoDragTrackerTest, PressDragReleaseIsCaptureDragEnd )
{
    sw::editor::EditorGizmoDragTracker tracker;
    sw::vector<EditorGizmoCommand>     listCommand;
    const sw::float4x4                 moved{};
    tracker.stepSingle( makeTarget( 1 ), true, true, &moved, false, listCommand );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listCommand.size() );
    SW_EXPECT_TRUE( isKind( listCommand, 0, EditorGizmoCommandKind::Capture ) );
    SW_EXPECT_TRUE( isKind( listCommand, 1, EditorGizmoCommandKind::Drag ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, listCommand[1]._listTarget.size() );
    SW_EXPECT_TRUE( tracker.isTracking() );

    listCommand.clear();
    tracker.stepSingle( makeTarget( 1 ), false, false, nullptr, false, listCommand );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listCommand.size() );
    SW_EXPECT_TRUE( isKind( listCommand, 0, EditorGizmoCommandKind::End ) );
    SW_EXPECT_FALSE( tracker.isTracking() );
}

SW_TEST_CASE( EditorGizmoDragTrackerTest, TargetChangeWhileDraggingEndsAndRecaptures )
{
    sw::editor::EditorGizmoDragTracker tracker;
    sw::vector<EditorGizmoCommand>     listCommand;
    tracker.stepSingle( makeTarget( 1 ), true, true, nullptr, false, listCommand );
    listCommand.clear();

    // 끄는 중에 대상이 2 로 바뀌었다 — 1 의 드래그를 닫고 2 를 잡아 이 끌기도 되돌리기에 남긴다.
    const sw::float4x4 moved{};
    tracker.stepSingle( makeTarget( 2 ), false, true, &moved, false, listCommand );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listCommand.size() );
    SW_EXPECT_TRUE( isKind( listCommand, 0, EditorGizmoCommandKind::End ) );
    SW_EXPECT_TRUE( isKind( listCommand, 1, EditorGizmoCommandKind::Capture ) );
    SW_EXPECT_TRUE( listCommand[1]._listTarget[0]._object == sw::GameObjectHandle::make( 2 ) );
    SW_EXPECT_TRUE( isKind( listCommand, 2, EditorGizmoCommandKind::Drag ) );
    SW_EXPECT_TRUE( tracker.getTrackedObject() == sw::GameObjectHandle::make( 2 ) );
}

SW_TEST_CASE( EditorGizmoDragTrackerTest, PressWithoutGrabCloses )
{
    sw::editor::EditorGizmoDragTracker tracker;
    sw::vector<EditorGizmoCommand>     listCommand;
    // 기즈모 위를 눌렀지만 ImGuizmo 가 잡지 않았다 — 잡기를 같은 프레임에 닫는다(Model 은 끌기 없는 잡기를 기록하지 않는다).
    tracker.stepSingle( makeTarget( 1 ), true, false, nullptr, false, listCommand );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listCommand.size() );
    SW_EXPECT_TRUE( isKind( listCommand, 0, EditorGizmoCommandKind::Capture ) );
    SW_EXPECT_TRUE( isKind( listCommand, 1, EditorGizmoCommandKind::End ) );

    // 잡기 없이 시작된 끌기는 끌기 · 놓기만 낸다.
    listCommand.clear();
    tracker.stepSingle( makeTarget( 1 ), false, true, nullptr, false, listCommand );
    tracker.stepSingle( makeTarget( 1 ), false, false, nullptr, false, listCommand );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listCommand.size() );
    SW_EXPECT_TRUE( isKind( listCommand, 0, EditorGizmoCommandKind::Drag ) );
    SW_EXPECT_TRUE( isKind( listCommand, 1, EditorGizmoCommandKind::End ) );
}

SW_TEST_CASE( EditorGizmoDragTrackerTest, GroupToSingleClosesGroupDrag )
{
    sw::editor::EditorGizmoDragTracker tracker;
    sw::vector<EditorGizmoCommand>     listCommand;
    tracker.captureGroup( { makeTarget( 1 ), makeTarget( 2 ) }, listCommand );
    tracker.stepGroup( true, {}, listCommand );
    listCommand.clear();
    tracker.stepSingle( makeTarget( 1 ), false, false, nullptr, false, listCommand );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listCommand.size() );
    SW_EXPECT_TRUE( isKind( listCommand, 0, EditorGizmoCommandKind::End ) );
}
