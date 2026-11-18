// 리그 대상 기즈모 명령 순서(`RigTargetGizmoCommand::step`) — 옮긴 프레임은 끌기, 쥐지 않으면 놓기, 옮긴 프레임에 바로 놓으면 끌기 다음 놓기, 끌지 않던 쥐지 않은 프레임은 아무것도 내지 않는다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Viewport/Model/RigTargetGizmoEdit.h"

namespace
{
    using sw::editor::RigTargetGizmoCommand;
    using sw::editor::RigTargetGizmoCommandKind;

    bool isKind( const sw::vector<RigTargetGizmoCommand>& listCommand, size_t index, RigTargetGizmoCommandKind kind )
    {
        return index < listCommand.size() && listCommand[index]._kind == kind;
    }
} // namespace

SW_TEST_CASE( RigTargetGizmoCommandTest, MoveWhileHeldIsDragThenReleaseOnLetGo )
{
    sw::vector<RigTargetGizmoCommand> listCommand;
    sw::float4x4                      moved{};
    moved.setTranslation( sw::float3{ 1.0f, 2.0f, 3.0f } );
    RigTargetGizmoCommand::step( false, &moved, true, listCommand );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listCommand.size() );
    SW_EXPECT_TRUE( isKind( listCommand, 0, RigTargetGizmoCommandKind::Drag ) );
    SW_EXPECT_EQUAL( 2.0f, listCommand[0]._world.getTranslation()._y );

    listCommand.clear();
    RigTargetGizmoCommand::step( true, nullptr, false, listCommand );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listCommand.size() );
    SW_EXPECT_TRUE( isKind( listCommand, 0, RigTargetGizmoCommandKind::Release ) );
}

SW_TEST_CASE( RigTargetGizmoCommandTest, MoveAndLetGoInOneFrameIsDragThenRelease )
{
    sw::vector<RigTargetGizmoCommand> listCommand;
    const sw::float4x4                moved{};
    RigTargetGizmoCommand::step( false, &moved, false, listCommand );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listCommand.size() );
    SW_EXPECT_TRUE( isKind( listCommand, 0, RigTargetGizmoCommandKind::Drag ) );
    SW_EXPECT_TRUE( isKind( listCommand, 1, RigTargetGizmoCommandKind::Release ) );
}

SW_TEST_CASE( RigTargetGizmoCommandTest, IdleSendsNothing )
{
    sw::vector<RigTargetGizmoCommand> listCommand;
    RigTargetGizmoCommand::step( false, nullptr, false, listCommand );
    RigTargetGizmoCommand::step( true, nullptr, true, listCommand ); // 쥐고 있지만 이번 프레임에 옮기지 않았다
    SW_EXPECT_TRUE( listCommand.empty() );
}
