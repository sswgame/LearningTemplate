// 화면 배지 집기(`EditorViewportPick::findBillboardAt`) — 반지름 안의 가장 가까운 배지, 밖이면 없음, 겹치면 가까운 쪽.
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Editor/Viewport/Model/EditorViewportPick.h"
#include "Editor/Viewport/Model/EditorVisualizerDrawList.h"

namespace
{
    sw::editor::EditorVisualizerScreenBillboard makeBillboard( uint64 objectID, float x, float y )
    {
        sw::editor::EditorVisualizerScreenBillboard billboard{};
        billboard._objectID    = objectID;
        billboard._componentID = objectID * 10;
        billboard._screen      = sw::float2{ x, y };
        billboard._radius      = 13.0f;
        return billboard;
    }
} // namespace

SW_TEST_CASE( EditorViewportPickTest, BillboardInsideRadiusIsFound )
{
    const sw::vector<sw::editor::EditorVisualizerScreenBillboard> listBillboard{ makeBillboard( 1, 100.0f, 100.0f ) };
    SW_EXPECT_EQUAL( uint32{ 0 }, sw::editor::EditorViewportPick::findBillboardAt( listBillboard, sw::float2{ 110.0f, 105.0f } ) );
    SW_EXPECT_EQUAL( sw::constant::invalid_index::kUint32, sw::editor::EditorViewportPick::findBillboardAt( listBillboard, sw::float2{ 120.0f, 100.0f } ) );
}

SW_TEST_CASE( EditorViewportPickTest, OverlappingBillboardPicksNearest )
{
    const sw::vector<sw::editor::EditorVisualizerScreenBillboard> listBillboard{ makeBillboard( 1, 100.0f, 100.0f ), makeBillboard( 2, 110.0f, 100.0f ) };
    SW_EXPECT_EQUAL( uint32{ 1 }, sw::editor::EditorViewportPick::findBillboardAt( listBillboard, sw::float2{ 108.0f, 100.0f } ) );
    SW_EXPECT_EQUAL( uint32{ 0 }, sw::editor::EditorViewportPick::findBillboardAt( listBillboard, sw::float2{ 101.0f, 100.0f } ) );
}

SW_TEST_CASE( EditorViewportPickTest, NoBillboardFindsNothing )
{
    const sw::vector<sw::editor::EditorVisualizerScreenBillboard> listBillboard;
    SW_EXPECT_EQUAL( sw::constant::invalid_index::kUint32, sw::editor::EditorViewportPick::findBillboardAt( listBillboard, sw::float2{ 0.0f, 0.0f } ) );
}
