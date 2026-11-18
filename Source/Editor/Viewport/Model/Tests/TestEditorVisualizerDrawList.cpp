// 시각화 도형 값 목록 — 상자 12 모서리 · 닫힌 꺾은선 · 물리 선 출구의 통계 칸 · 비우기(그리는 쪽이 값만 받는다).
#include "pch.h"

#include "Core/Math/AABB.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Viewport/Model/EditorVisualizerDrawList.h"

SW_TEST_CASE( EditorVisualizerDrawListTest, BoxAddsTwelveEdgesWithPrototypeStyle )
{
    sw::editor::EditorVisualizerDrawList drawList;
    sw::editor::EditorVisualizerLine     prototype{};
    prototype._thickness = 2.0f;
    prototype._colorRole = sw::editor::EditorVisualizerColorRole::SelectionOutline;
    drawList.addBox( sw::AABB{
                         sw::float3{0.0f, 0.0f, 0.0f},
                         sw::float3{1.0f, 2.0f, 3.0f}
    },
                     prototype );

    SW_REQUIRE_EQUAL( size_t{ 12 }, drawList._listLine.size() );
    for ( const sw::editor::EditorVisualizerLine& line : drawList._listLine )
    {
        SW_EXPECT_EQUAL( 2.0f, line._thickness );
        SW_EXPECT_TRUE( line._colorRole == sw::editor::EditorVisualizerColorRole::SelectionOutline );
        // 모서리는 한 축만 바뀐다.
        const uint32 changedAxes = ( line._from._x != line._to._x ? 1u : 0u ) + ( line._from._y != line._to._y ? 1u : 0u ) + ( line._from._z != line._to._z ? 1u : 0u );
        SW_EXPECT_EQUAL( 1u, changedAxes );
    }
}

SW_TEST_CASE( EditorVisualizerDrawListTest, LoopClosesBackToFirstPoint )
{
    sw::editor::EditorVisualizerDrawList drawList;
    const sw::float3                     arrPoint[3] = {
        sw::float3{0.0f, 0.0f, 0.0f},
        sw::float3{1.0f, 0.0f, 0.0f},
        sw::float3{0.0f, 1.0f, 0.0f}
    };
    drawList.addLoop( arrPoint, 3, sw::editor::EditorVisualizerLine{} );

    SW_REQUIRE_EQUAL( size_t{ 3 }, drawList._listLine.size() );
    SW_EXPECT_EQUAL( 0.0f, drawList._listLine[2]._to._x );
    SW_EXPECT_EQUAL( 0.0f, drawList._listLine[2]._to._y );
    SW_EXPECT_EQUAL( 1.0f, drawList._listLine[2]._from._y );

    drawList.addLoop( arrPoint, 1, sw::editor::EditorVisualizerLine{} ); // 점 하나는 선이 없다
    SW_EXPECT_EQUAL( size_t{ 3 }, drawList._listLine.size() );
}

SW_TEST_CASE( EditorVisualizerDrawListTest, LineSinkTagsStatSlotAndClearResets )
{
    sw::editor::EditorVisualizerDrawList drawList;
    {
        sw::editor::EditorVisualizerLineSink sink{ drawList, sw::editor::EditorVisualizerStatSlot::NavMesh };
        sink.drawLine( sw::float3{ 0.0f, 0.0f, 0.0f }, sw::float3{ 0.0f, 0.0f, 1.0f }, sw::float4{ 1.0f, 0.0f, 0.0f, 1.0f } );
    }
    SW_REQUIRE_EQUAL( size_t{ 1 }, drawList._listLine.size() );
    SW_EXPECT_TRUE( drawList._listLine[0]._statSlot == sw::editor::EditorVisualizerStatSlot::NavMesh );
    SW_EXPECT_TRUE( drawList._listLine[0]._colorRole == sw::editor::EditorVisualizerColorRole::Explicit );
    SW_EXPECT_EQUAL( 1.0f, drawList._listLine[0]._color._x );

    drawList._bDebugDrawGathered = true;
    drawList.clear();
    SW_EXPECT_TRUE( drawList._listLine.empty() );
    SW_EXPECT_FALSE( drawList._bDebugDrawGathered );
}
