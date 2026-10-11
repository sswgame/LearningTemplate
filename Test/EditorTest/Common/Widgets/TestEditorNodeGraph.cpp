#include "pch.h"

#include "Editor/Common/Widgets/EditorNodeGraph.h"

#include "TestFramework/TestFramework.h"

// EditorNodeGraphTest — 노드 그래프 캔버스의 내용 맞추기는 캔버스 크기가 정해진 뒤에 한다. ImGui 없음(판정만).

/**
 * @brief [EditorNodeGraphTest] 내용 맞추기는 캔버스 크기가 앞 프레임과 같고 0 이 아닐 때만 한다 — 처음 열거나 옆 칸이 붙어 크기가 바뀐 프레임은 미룬다
 * @details 크기가 막 바뀐 프레임에 NavigateToContent 를 부르면 노드 편집기가 옛 크기의 보이는 사각형으로 맞춘다는 가설에 따라 맞추기를 한 프레임 미룬다.
 *          그 프레임에 맞추면 Dialogue Graph 를 처음 열거나 Reload 할 때 캔버스가 패널 일부만 덮고 링크가 패널 밖에 그려진다. 이 테스트는 판정만 본다.
 */
SW_TEST_CASE( EditorNodeGraphTest, ContentFitWaitsForASettledCanvasSize )
{
    using sw::float2;
    using sw::editor::EditorNodeGraph;
    SW_EXPECT_FALSE( EditorNodeGraph::isCanvasSizeSettled( float2{ 0.0f, 0.0f }, float2{ 800.0f, 500.0f } ) );     // 처음 그린 프레임
    SW_EXPECT_FALSE( EditorNodeGraph::isCanvasSizeSettled( float2{ 800.0f, 500.0f }, float2{ 576.0f, 500.0f } ) ); // 옆 인스펙터가 붙은 프레임
    SW_EXPECT_TRUE( EditorNodeGraph::isCanvasSizeSettled( float2{ 576.0f, 500.0f }, float2{ 576.0f, 500.0f } ) );
    SW_EXPECT_FALSE( EditorNodeGraph::isCanvasSizeSettled( float2{ 0.0f, 0.0f }, float2{ 0.0f, 0.0f } ) ); // 접힌 캔버스
}
