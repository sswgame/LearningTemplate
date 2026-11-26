/**
 * @file TestEditorUIFrameOutput.cpp
 * @brief UI → 게임 값 묶음 계약(`EditorUIFrameOutput`)을 봅니다 — UI 프레임이 쓴 값은 프레임 끝의 넘김(`publishUIFrame`) 뒤에만 게임 스레드 쪽(`getPublishedUIFrame`)에 보이고,
 *        다음 UI 프레임이 쓰는 중인 값은 다음 넘김까지 보이지 않는다.
 * @details 서브시스템은 지역 객체로 만든다(에디터 컨텍스트 없음). 렌더 타깃을 만들지 않으므로 RHI · 에셋이 없어도 돈다.
 */
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Editor/Common/Workspace/EditorRenderViews.h"

#include "Engine/Graphics/View/FrameViewports.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorUIFrameOutputTest] UI 프레임이 쓴 그린 뷰 · 자리 · 초점 · 다시 그릴 까닭은 넘긴 뒤에만 게임 쪽에 보이고, 다음 UI 프레임의 쓰기는 다음 넘김까지 숨는다
 */
SW_TEST_CASE( EditorUIFrameOutputTest, PublishedOnlyAtFrameEnd )
{
    EditorRenderViews views;

    // UI 프레임 1 — 패널이 게임 뷰를 그리고 초점을 알린다. 넘기기 전에는 게임 쪽에 없다.
    views.beginFrameFocus();
    views.clearViewDrawnMarks();
    views.markViewDrawn( EditorViewKind::Game, float2{ 12.4f, 30.6f } );
    views.setGameViewFocused( true );
    views.setGameViewHovered( true );
    SW_EXPECT_TRUE( views.wasViewDrawn( EditorViewKind::Game ) ); // UI 쪽은 그리는 중인 묶음을 읽는다
    SW_EXPECT_FALSE( views.getPublishedUIFrame().wasViewDrawn( EditorViewKind::Game ) );
    SW_EXPECT_FALSE( views.isViewRequested( EditorViewKind::Game ) );

    views.publishUIFrame( 0x5u );
    const EditorUIFrameOutput first = views.getPublishedUIFrame();
    SW_EXPECT_TRUE( first.wasViewDrawn( EditorViewKind::Game ) );
    SW_EXPECT_EQUAL( 12, first._arrView[EditorViewKind::Game]._windowX );
    SW_EXPECT_EQUAL( 31, first._arrView[EditorViewKind::Game]._windowY );
    SW_EXPECT_TRUE( first._bGameViewFocused == SW_TRUE );
    SW_EXPECT_TRUE( first._bGameViewHovered == SW_TRUE );
    SW_EXPECT_EQUAL( 0x5u, first._redrawReasons );
    SW_EXPECT_TRUE( views.isViewRequested( EditorViewKind::Game ) );
    SW_EXPECT_FALSE( views.isViewRequested( EditorViewKind::Scene ) ); // 게임 뷰만 보이면 씬 뷰는 알리지 않는다

    // UI 프레임 2 — 앞 정리가 그리는 중인 묶음을 비우고 씬 뷰만 그린다. 넘기기 전까지 게임 쪽은 프레임 1 그대로다.
    views.beginFrameFocus();
    views.clearViewDrawnMarks();
    views.markViewDrawn( EditorViewKind::Scene, float2{ 0.0f, 0.0f } );
    SW_EXPECT_TRUE( views.wasGameInputFocusedLastFrame() );
    SW_EXPECT_FALSE( views.isGameViewFocused() );
    SW_EXPECT_TRUE( views.getPublishedUIFrame().wasViewDrawn( EditorViewKind::Game ) );
    SW_EXPECT_TRUE( views.getPublishedUIFrame()._bGameViewFocused == SW_TRUE );

    views.publishUIFrame( 0u );
    const EditorUIFrameOutput second = views.getPublishedUIFrame();
    SW_EXPECT_FALSE( second.wasViewDrawn( EditorViewKind::Game ) );
    SW_EXPECT_TRUE( second.wasViewDrawn( EditorViewKind::Scene ) );
    SW_EXPECT_TRUE( second._bGameViewFocused == SW_FALSE );
    SW_EXPECT_EQUAL( 0u, second._redrawReasons );
}

/**
 * @brief [EditorUIFrameOutputTest] 두 뷰가 모두 가려지면 씬 뷰를 알린다(호스트가 백버퍼에 그려 UI 밑에 깔리지 않게) — 묶음의 판정이 셸 판정(`EditorViewTargetUtil`)과 같다
 */
SW_TEST_CASE( EditorUIFrameOutputTest, ViewRequestRule )
{
    EditorUIFrameOutput frame;
    SW_EXPECT_TRUE( frame.isViewRequested( EditorViewKind::Scene ) );
    SW_EXPECT_FALSE( frame.isViewRequested( EditorViewKind::Game ) );
    frame._arrView[EditorViewKind::Scene]._bDrawn = SW_TRUE;
    frame._arrView[EditorViewKind::Game]._bDrawn  = SW_TRUE;
    SW_EXPECT_TRUE( frame.isViewRequested( EditorViewKind::Scene ) );
    SW_EXPECT_TRUE( frame.isViewRequested( EditorViewKind::Game ) );
}

/**
 * @brief [EditorUIFrameOutputTest] 미리보기 뷰의 눈은 묶음에 값으로 실리고, id 없는 제출은 싣지 않으며, 한 프레임 상한을 넘으면 버리고 오류를 남긴다
 */
SW_TEST_CASE( EditorUIFrameOutputTest, PreviewSubmitsTravelAsValues )
{
    EditorRenderViews views;
    const uint32      targetID = views.allocatePreviewTarget();

    views.clearViewDrawnMarks();
    views.submitPreviewView( 0, 7, float4x4{}, float3{} ); // id 없음 — 싣지 않는다
    views.submitPreviewView( targetID, 7, float4x4{}, float3{ 1.0f, 2.0f, 3.0f } );
    SW_EXPECT_TRUE( views.isAnimationRequested() );
    views.publishUIFrame( 0u );
    const EditorUIFrameOutput frame = views.getPublishedUIFrame();
    SW_REQUIRE_EQUAL( 1u, frame._previewCount );
    SW_EXPECT_EQUAL( targetID, frame._arrPreview[0]._id );
    SW_EXPECT_EQUAL( 7u, frame._arrPreview[0]._sceneKey );
    SW_EXPECT_NEAR( 2.0f, frame._arrPreview[0]._cameraPos._y, 0.0001f );

    // 다음 UI 프레임의 앞 정리가 그리는 중인 묶음의 미리보기를 비운다.
    views.clearViewDrawnMarks();
    SW_EXPECT_FALSE( views.isAnimationRequested() );
    SW_EXPECT_LOG( "EditorRenderViews.PreviewViewOverflow" );
    for ( uint32 index = 0; index < kMaxPreviewSceneView + 1; ++index )
    {
        views.submitPreviewView( targetID, 7, float4x4{}, float3{} );
    }
    views.publishUIFrame( 0u );
    SW_EXPECT_EQUAL( kMaxPreviewSceneView, views.getPublishedUIFrame()._previewCount );
}

/**
 * @brief [EditorUIFrameOutputTest] 타깃은 게임 스레드가 가진다 — 게임 쪽에서 만들고 놓은 미리보기 타깃은 게임이 넘긴 뒤 다음 UI 프레임 시작에만 UI 값에 보인다(게임 → UI 값 묶음)
 */
SW_TEST_CASE( EditorUIFrameOutputTest, TargetStatesReachUIAtFrameStart )
{
    EditorRenderViews views;
    const uint32      targetID = views.allocatePreviewTarget();
    SW_EXPECT_TRUE( targetID != 0 );
    SW_EXPECT_TRUE( views.allocatePreviewTarget() != targetID ); // 다시 쓰지 않는다

    // 게임 틱 — FitPreviewTarget 의 적용이 슬롯을 만든다(디바이스가 없어 렌더 타깃은 요청하지 않는다). 넘기기 전에는 UI 에 없다.
    views.ensurePreviewTargetSize( targetID, 64, 32 );
    SW_EXPECT_TRUE( views.findPreviewTarget( targetID ) != nullptr );
    views.clearViewDrawnMarks();
    SW_EXPECT_TRUE( views.findUIPreviewTargetState( targetID ) == nullptr );

    // 게임이 넘기고 다음 UI 프레임이 시작해야 보인다.
    views.applyReadyAndPublishTargets();
    SW_EXPECT_TRUE( views.findUIPreviewTargetState( targetID ) == nullptr ); // 이번 UI 프레임 값은 그대로다
    views.clearViewDrawnMarks();
    SW_EXPECT_TRUE( views.findUIPreviewTargetState( targetID ) != nullptr );

    // FreePreviewTarget 의 적용 뒤에도 같은 길로 사라진다.
    views.freePreviewTarget( targetID );
    SW_EXPECT_TRUE( views.findUIPreviewTargetState( targetID ) != nullptr );
    views.applyReadyAndPublishTargets();
    views.clearViewDrawnMarks();
    SW_EXPECT_TRUE( views.findUIPreviewTargetState( targetID ) == nullptr );
}

/**
 * @brief [EditorUIFrameOutputTest] 새로 띄운 에디터로 되돌리면 앞 단계가 그린 뷰 · 초점 · 미리보기 눈이 UI 쪽에도 게임 쪽에도 남지 않는다
 */
SW_TEST_CASE( EditorUIFrameOutputTest, ResetForgetsDrawnViews )
{
    EditorRenderViews views;
    views.markViewDrawn( EditorViewKind::Game, float2{ 1.0f, 2.0f } );
    views.setGameViewFocused( true );
    views.submitPreviewView( views.allocatePreviewTarget(), 1u, float4x4{}, float3{} );
    views.publishUIFrame( 0x5u );
    views.beginFrameFocus();
    SW_REQUIRE_TRUE( views.isViewRequested( EditorViewKind::Game ) );
    SW_REQUIRE_TRUE( views.wasGameInputFocusedLastFrame() );

    views.resetToFreshEditor();

    SW_EXPECT_FALSE( views.isViewRequested( EditorViewKind::Game ) );
    SW_EXPECT_FALSE( views.wasViewDrawn( EditorViewKind::Game ) );
    SW_EXPECT_FALSE( views.wasGameInputFocusedLastFrame() );
    SW_EXPECT_FALSE( views.isAnimationRequested() );
    const EditorUIFrameOutput published = views.getPublishedUIFrame();
    SW_EXPECT_EQUAL( 0u, published._previewCount );
    SW_EXPECT_EQUAL( 0u, published._redrawReasons );
    SW_EXPECT_FALSE( published.isGameInputFocused() );
}
