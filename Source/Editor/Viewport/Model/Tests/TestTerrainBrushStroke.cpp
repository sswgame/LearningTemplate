// 지형 붓질 명령 순서(`TerrainBrushEdit::stepStroke`) — 누름은 시작 + 적용, 끄는 동안 닿으면 적용, 놓거나 쓸 수 없으면 끝, 붓질 밖의 쓸 수 없는 프레임은 아무것도 없다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Viewport/Model/TerrainBrushEdit.h"

namespace
{
    using sw::editor::TerrainBrushCommand;
    using sw::editor::TerrainBrushCommandKind;
    using sw::editor::TerrainBrushEdit;
    using sw::editor::TerrainBrushStrokeInput;

    bool isKind( const sw::vector<TerrainBrushCommand>& listCommand, size_t index, TerrainBrushCommandKind kind )
    {
        return index < listCommand.size() && listCommand[index]._kind == kind;
    }
} // namespace

SW_TEST_CASE( TerrainBrushStrokeTest, PressBeginsAndAppliesAtTheHit )
{
    TerrainBrushStrokeInput input{};
    input._bUsable  = true;
    input._bHit     = true;
    input._bPressed = true;
    input._hit      = sw::float3{ 1.0f, 2.0f, 3.0f };
    sw::vector<TerrainBrushCommand> listCommand;
    TerrainBrushEdit::stepStroke( input, listCommand );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listCommand.size() );
    SW_EXPECT_TRUE( isKind( listCommand, 0, TerrainBrushCommandKind::Begin ) );
    SW_EXPECT_TRUE( isKind( listCommand, 1, TerrainBrushCommandKind::Apply ) );
    SW_EXPECT_NEAR( 3.0f, listCommand[1]._hit._z, 1e-6f );
}

SW_TEST_CASE( TerrainBrushStrokeTest, StrokeAppliesWhileDownAndEndsOnRelease )
{
    TerrainBrushStrokeInput input{};
    input._bUsable    = true;
    input._bStroking  = true;
    input._bStillDown = true;
    input._bHit       = true;
    sw::vector<TerrainBrushCommand> listCommand;
    TerrainBrushEdit::stepStroke( input, listCommand );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listCommand.size() );
    SW_EXPECT_TRUE( isKind( listCommand, 0, TerrainBrushCommandKind::Apply ) );

    listCommand.clear();
    input._bHit = false; // 지형 밖으로 나가면 쉬기만 한다
    TerrainBrushEdit::stepStroke( input, listCommand );
    SW_EXPECT_TRUE( listCommand.empty() );

    input._bStillDown = false;
    TerrainBrushEdit::stepStroke( input, listCommand );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listCommand.size() );
    SW_EXPECT_TRUE( isKind( listCommand, 0, TerrainBrushCommandKind::End ) );
}

SW_TEST_CASE( TerrainBrushStrokeTest, UnusableViewEndsOnlyAStroke )
{
    TerrainBrushStrokeInput input{};
    input._bPressed = true;
    sw::vector<TerrainBrushCommand> listCommand;
    TerrainBrushEdit::stepStroke( input, listCommand );
    SW_EXPECT_TRUE( listCommand.empty() );

    input._bStroking = true;
    TerrainBrushEdit::stepStroke( input, listCommand );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listCommand.size() );
    SW_EXPECT_TRUE( isKind( listCommand, 0, TerrainBrushCommandKind::End ) );
}
