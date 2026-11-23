// 지형 붓질 명령 순서(`TerrainBrushEdit::stepStroke`) — 누름은 시작 + 적용, 끄는 동안 닿으면 적용, 놓거나 쓸 수 없으면 끝, 붓질 밖의 쓸 수 없는 프레임은 아무것도 없다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Viewport/Model/TerrainBrushEdit.h"

#include "Engine/Environment/Placement/SurfaceLayerWeights.h"

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

/** @brief [TerrainBrushStrokeTest] 레이어 지우기(폴리지 밀도 지우기) — 그 레이어만 줄고 합은 1 이며, 전부였던 레이어는 바탕이 받고, 바탕 자신은 지우지 않는다 */
SW_TEST_CASE( TerrainBrushStrokeTest, EraseLayerLowersOnlyThatLayer )
{
    using sw::editor::TerrainBrush;
    sw::vector<sw::SurfaceLayerWeights> listWeight( 4 );
    listWeight[0].set( 0, 0.5f );
    listWeight[0].set( 3, 0.5f );
    listWeight[1].set( 0, 0.0f );
    listWeight[1].set( 3, 1.0f );
    const sw::vector<sw::editor::TerrainBrushSample> listSample{
        {0, 0, 1.0f},
        {1, 0, 1.0f},
        {0, 1, 1.0f}
    };
    sw::editor::TerrainBrushSettings settings;
    settings._mode       = sw::editor::TerrainBrushMode::EraseLayer;
    settings._layerIndex = 3;
    settings._strength   = 0.5f;
    SW_EXPECT_TRUE( TerrainBrush::getTarget( settings._mode ) == sw::editor::TerrainBrushTarget::LayerWeight );
    SW_EXPECT_TRUE( TerrainBrush::applyLayerWeight( listWeight, 2, listSample, settings, 1.0f ) );
    SW_EXPECT_NEAR( 0.25f, listWeight[0].get( 3 ), 1.0e-4f );
    SW_EXPECT_NEAR( 1.0f, listWeight[0].getSum(), 1.0e-4f );
    SW_EXPECT_NEAR( 0.5f, listWeight[1].get( 3 ), 1.0e-4f );
    SW_EXPECT_NEAR( 0.5f, listWeight[1].get( 0 ), 1.0e-4f ); // 전부였던 레이어는 바탕이 받는다
    SW_EXPECT_NEAR( 0.0f, listWeight[2].get( 3 ), 1.0e-4f ); // 없는 레이어는 그대로다

    settings._layerIndex = 0;
    SW_EXPECT_FALSE( TerrainBrush::applyLayerWeight( listWeight, 2, {
                                                                        { 0, 1, 1.0f }
    },
                                                     settings, 1.0f ) );
}

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
