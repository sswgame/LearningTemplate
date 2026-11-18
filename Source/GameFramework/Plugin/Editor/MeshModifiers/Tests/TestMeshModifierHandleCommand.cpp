// Modifiers 화면 도구의 집기 판정(`MeshModifierHandleViewState::pick`)과 명령 순서(`MeshModifierHandleCommand::step` · `press`) — 씬 · ImGui 없이 값으로 본다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Model/MeshModifierHandleEdit.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct MeshModifierHandleCommandTestUtil
    {
        /** @brief 원점에서 +X 로 길이 1 인 이동 화살표 하나와 제어점 둘(하나는 화살표에서 먼 곳)을 든 값입니다. */
        static MeshModifierHandleViewState makeState()
        {
            MeshModifierHandleViewState state;
            state._bAvailable      = true;
            state._modifierIndex   = 2;
            state._selectedPoint   = 0;
            state._bPointSet       = true;
            state._pointPickRadius = 0.1f;
            state._listHandle.push_back( MeshModifierHandle{
                MeshModifierHandleKind::GizmoMove, 0, float3{0.0f, 0.0f, 0.0f},
                float3{1.0f, 0.0f, 0.0f},
                0.0f
            } );
            state._listPoint.push_back( float3{ 0.0f, 0.0f, 0.0f } );
            state._listPoint.push_back( float3{ 0.0f, 5.0f, 0.0f } );
            return state;
        }

        /** @brief 점 @p target 을 위(-Z 쪽)에서 내려다보는 레이입니다. */
        static Ray makeRayAt( const float3& target )
        {
            Ray ray{};
            ray._origin    = float3{ target._x, target._y, -10.0f };
            ray._direction = float3{ 0.0f, 0.0f, 1.0f };
            return ray;
        }
    };
} // namespace

SW_TEST_CASE( MeshModifierHandleCommandTest, PickPrefersHandleThenFallsBackToPoint )
{
    using Util                              = MeshModifierHandleCommandTestUtil;
    const MeshModifierHandleViewState state = Util::makeState();
    optional<int32>                   handle;
    optional<int32>                   point;
    state.pick( Util::makeRayAt( float3{ 0.5f, 0.0f, 0.0f } ), handle, point );
    SW_REQUIRE_TRUE( handle.has_value() );
    SW_EXPECT_EQUAL( 0, *handle );
    SW_EXPECT_FALSE( point.has_value() );

    state.pick( Util::makeRayAt( float3{ 0.0f, 5.0f, 0.0f } ), handle, point );
    SW_EXPECT_FALSE( handle.has_value() );
    SW_REQUIRE_TRUE( point.has_value() );
    SW_EXPECT_EQUAL( 1, *point );

    state.pick( Util::makeRayAt( float3{ 3.0f, 3.0f, 0.0f } ), handle, point );
    SW_EXPECT_FALSE( handle.has_value() );
    SW_EXPECT_FALSE( point.has_value() );
}

SW_TEST_CASE( MeshModifierHandleCommandTest, PressHandleBeginsAndPressPointSelectsThenBegins )
{
    using Util                              = MeshModifierHandleCommandTestUtil;
    const MeshModifierHandleViewState state = Util::makeState();
    vector<MeshModifierHandleCommand> listCommand;
    MeshModifierHandleCommand::press( state, 0, nullopt, Ray{}, listCommand );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listCommand.size() );
    SW_EXPECT_TRUE( listCommand[0]._kind == MeshModifierHandleCommandKind::Begin );
    SW_EXPECT_TRUE( listCommand[0]._handle._kind == MeshModifierHandleKind::GizmoMove );
    SW_EXPECT_EQUAL( 2, listCommand[0]._modifierIndex );
    SW_EXPECT_FALSE( listCommand[0]._bSelectPoint );

    listCommand.clear();
    MeshModifierHandleCommand::press( state, nullopt, 1, Ray{}, listCommand );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listCommand.size() );
    SW_EXPECT_TRUE( listCommand[0]._handle._kind == MeshModifierHandleKind::PointFree );
    SW_EXPECT_TRUE( listCommand[0]._bSelectPoint );
    SW_EXPECT_EQUAL( 1, listCommand[0]._pointIndex );
    SW_EXPECT_EQUAL( 5.0f, listCommand[0]._handle._origin._y );

    listCommand.clear();
    MeshModifierHandleCommand::press( state, nullopt, nullopt, Ray{}, listCommand );
    SW_EXPECT_TRUE( listCommand.empty() );
}

SW_TEST_CASE( MeshModifierHandleCommandTest, StepDragsWhileHeldAndEndsOnReleaseOrModifierChange )
{
    using Util                              = MeshModifierHandleCommandTestUtil;
    MeshModifierHandleViewState       state = Util::makeState();
    vector<MeshModifierHandleCommand> listCommand;
    SW_EXPECT_FALSE( MeshModifierHandleCommand::step( state, true, nullptr, listCommand ) ); // 끌지 않는 중
    SW_EXPECT_TRUE( listCommand.empty() );

    state._bDragging         = true;
    state._dragModifierIndex = 2;
    const Ray ray{};
    SW_EXPECT_TRUE( MeshModifierHandleCommand::step( state, true, &ray, listCommand ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listCommand.size() );
    SW_EXPECT_TRUE( listCommand[0]._kind == MeshModifierHandleCommandKind::Drag );
    SW_EXPECT_TRUE( listCommand[0]._bHasRay );

    listCommand.clear();
    SW_EXPECT_TRUE( MeshModifierHandleCommand::step( state, true, nullptr, listCommand ) ); // 레이가 없어도 마우스는 다시 잡는다
    SW_REQUIRE_EQUAL( size_t{ 1 }, listCommand.size() );
    SW_EXPECT_FALSE( listCommand[0]._bHasRay );

    listCommand.clear();
    SW_EXPECT_FALSE( MeshModifierHandleCommand::step( state, false, &ray, listCommand ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listCommand.size() );
    SW_EXPECT_TRUE( listCommand[0]._kind == MeshModifierHandleCommandKind::End );

    listCommand.clear();
    state._modifierIndex = 3; // 끄는 중에 다른 모디파이어를 골랐다
    SW_EXPECT_FALSE( MeshModifierHandleCommand::step( state, true, &ray, listCommand ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listCommand.size() );
    SW_EXPECT_TRUE( listCommand[0]._kind == MeshModifierHandleCommandKind::End );
}
