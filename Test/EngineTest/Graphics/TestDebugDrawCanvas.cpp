#include "pch.h"

#include "Core/Math/MatrixMath.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/Canvas/DebugDrawCanvas.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

/**
 * @brief [DebugDrawCanvasTest] 단위 뷰-투영에서 clip 원점은 화면 가운데, (1, 1) 은 오른쪽 위 모서리로 간다(왼쪽 위 원점, 아래로 +y)
 */
SW_TEST_CASE( DebugDrawCanvasTest, ProjectSegmentMapsClipSpaceToTopLeftPixels )
{
    const float4x4 viewProj{};
    float2         from;
    float2         to;
    SW_ASSERT_TRUE( DebugDrawCanvasUtil::projectSegment( viewProj, float3{ 0.0f, 0.0f, 0.5f }, float3{ 1.0f, 1.0f, 0.5f }, float2{ 200.0f, 100.0f }, from, to ) );
    SW_EXPECT_NEAR_EQUAL( 100.0f, from._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 50.0f, from._y, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 200.0f, to._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, to._y, 0.001f );
}

/**
 * @brief [DebugDrawCanvasTest] 큐가 확정한 선 하나는 캔버스 사각형 하나, 구 하나는 대원 셋의 선분만큼이 된다 — 게임 화면 겹치기는 씬 뷰와 같은 선분을 그린다
 */
SW_TEST_CASE( DebugDrawCanvasTest, PaintAddsOneQuadPerVisibleSegment )
{
    DebugDrawQueue queue;
    queue.drawLine( float3{ -0.5f, 0.0f, 0.5f }, float3{ 0.5f, 0.0f, 0.5f }, float4{ 1.0f, 0.0f, 0.0f, 1.0f } );
    queue.drawSphere( float3{ 0.0f, 0.0f, 0.5f }, 0.25f, float4{ 0.0f, 1.0f, 0.0f, 1.0f } );
    queue.endFrame( 1.0f / 60.0f );

    CanvasDrawList canvas;
    canvas._targetSize         = float2{ 256.0f, 256.0f };
    const uint32 paintedCount  = DebugDrawCanvasUtil::paint( queue, float4x4{}, canvas._targetSize, nullptr, 0, canvas );
    const uint32 expectedCount = 1 + 3 * DebugDrawQueue::kSphereCircleSegmentCount;
    SW_EXPECT_EQUAL( expectedCount, paintedCount );
    SW_EXPECT_EQUAL( size_t{ expectedCount }, canvas._listQuad.size() );
}
