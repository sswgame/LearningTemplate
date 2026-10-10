#include "pch.h"

#include "Core/Memory/Memory.h"

#include "Editor/Panels/UIPreviewLogic.h"

#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/UIScale.h"
#include "Engine/UI/Widgets/BorderPanel.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "TestFramework/TestFramework.h"

// UIPreviewLogicTest — 에디터 UI 미리보기 패널의 판단(runtime-ui 8-5): 해상도 견본 → 뷰포트(게임과 같은 배율 규칙), 그림 맞춤 · 점 옮기기,
// 위젯 트리 줄 · 점 아래 위젯. ImGui 없음.

/**
 * @brief [UIPreviewLogicTest] 견본 해상도는 게임과 같은 배율 규칙으로 UI 뷰포트가 된다 — 사용자 배율을 곱하고 안전 영역은 물리 크기 비율, 렌더 텍스처 경로는 크기마다 다르다
 */
SW_TEST_CASE( UIPreviewLogicTest, ResolutionPresetMakesScaledViewport )
{
    using sw::editor::UIPreviewLogic;
    const sw::UIScaleSettings settings{}; // 기준 1920 × 1080
    SW_ASSERT_TRUE( UIPreviewLogic::getResolutionCount() >= 6u );
    const sw::UIViewport full = UIPreviewLogic::makeViewport( settings, 1, 1.0f, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 1920.0f, full._physicalSize._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, full._uiScale, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1920.0f, full._size._x, 1e-2f );

    const sw::UIViewport uhd = UIPreviewLogic::makeViewport( settings, 3, 1.0f, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, uhd._uiScale, 1e-4f ); // 4K = 기준의 두 배 — UI 단위 크기는 같다
    SW_EXPECT_NEAR_EQUAL( 1920.0f, uhd._size._x, 1e-2f );

    const sw::UIViewport doubled = UIPreviewLogic::makeViewport( settings, 1, 2.0f, 0.05f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, doubled._uiScale, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 960.0f, doubled._size._x, 1e-2f );
    SW_EXPECT_TRUE( doubled._safeInsets._x > 0.0f );
    SW_EXPECT_TRUE( UIPreviewLogic::makeTargetPath( full ) != UIPreviewLogic::makeTargetPath( uhd ) );
    SW_EXPECT_STREQ( UIPreviewLogic::getResolution( 0 )._pName, UIPreviewLogic::getResolution( 999 )._pName ); // 범위 밖은 첫 견본
}

/**
 * @brief [UIPreviewLogicTest] 그림은 비율을 지켜 자리에 맞추고(늘리지 않음), 그림 위 점 · 사각형은 UI 단위와 오간다. 트리 줄은 문서 순서 · 깊이, 점 아래는 맨 위 위젯
 */
SW_TEST_CASE( UIPreviewLogicTest, ImageMappingAndWidgetRows )
{
    using sw::editor::UIPreviewLogic;
    const sw::float2 fitted = UIPreviewLogic::fitImage( sw::float2{ 1920.0f, 1080.0f }, sw::float2{ 960.0f, 1000.0f } );
    SW_EXPECT_NEAR_EQUAL( 960.0f, fitted._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 540.0f, fitted._y, 1e-3f );
    const sw::float2 unscaled = UIPreviewLogic::fitImage( sw::float2{ 640.0f, 360.0f }, sw::float2{ 2000.0f, 2000.0f } );
    SW_EXPECT_NEAR_EQUAL( 640.0f, unscaled._x, 1e-3f ); // 늘리지 않는다

    sw::UIViewport viewport{};
    viewport._size         = sw::float2{ 1920.0f, 1080.0f };
    viewport._physicalSize = sw::float2{ 3840.0f, 2160.0f };
    viewport._uiScale      = 2.0f;
    const sw::float2 point = UIPreviewLogic::mapImageToUI( sw::float2{ 480.0f, 270.0f }, fitted, viewport );
    SW_EXPECT_NEAR_EQUAL( 960.0f, point._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 540.0f, point._y, 1e-3f );
    const sw::UIRect rect = UIPreviewLogic::mapUIToImage( sw::UIRect{ 0.0f, 0.0f, 1920.0f, 1080.0f }, fitted, viewport );
    SW_EXPECT_NEAR_EQUAL( 960.0f, rect._right, 1e-3f );

    // 루트 상자(0,0 ~ 400,300) 안에 테두리(10,10 ~ 110,60), 그 안에 글.
    sw::WidgetTree                  tree;
    sw::unique_ptr<sw::BoxPanel>    root   = sw::make_unique<sw::BoxPanel>();
    sw::BoxPanel*                   pRoot  = root.get();
    sw::unique_ptr<sw::BorderPanel> border = sw::make_unique<sw::BorderPanel>();
    border->setName( "Card" );
    sw::BorderPanel* pBorder = border.get();
    (void)border->addChild( sw::make_unique<sw::TextWidget>() );
    (void)pRoot->addChild( std::move( border ) );
    tree.setRoot( std::move( root ) );
    pRoot->setArrangedGeometry( sw::WidgetGeometry::makeAxisAligned( sw::float2{ 0.0f, 0.0f }, sw::float2{ 400.0f, 300.0f } ) );
    pBorder->setArrangedGeometry( sw::WidgetGeometry::makeAxisAligned( sw::float2{ 10.0f, 10.0f }, sw::float2{ 100.0f, 50.0f } ) );

    sw::vector<sw::editor::UIPreviewWidgetRow> listRow;
    UIPreviewLogic::collectWidgetRows( tree, listRow );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( listRow.size() ) );
    SW_EXPECT_EQUAL( 0u, listRow[0]._depth );
    SW_EXPECT_EQUAL( 1u, listRow[1]._depth );
    SW_EXPECT_STREQ( "BorderPanel #Card", listRow[1]._label.c_str() );
    SW_EXPECT_EQUAL( 2u, listRow[2]._depth );
    SW_EXPECT_EQUAL( pBorder->getID(), UIPreviewLogic::findWidgetAt( tree, sw::float2{ 50.0f, 30.0f } ) ); // 글은 놓지 않아 크기 0 — 테두리가 맨 위
    SW_EXPECT_EQUAL( pRoot->getID(), UIPreviewLogic::findWidgetAt( tree, sw::float2{ 300.0f, 200.0f } ) );
    SW_EXPECT_EQUAL( sw::kInvalidWidgetID, UIPreviewLogic::findWidgetAt( tree, sw::float2{ 500.0f, 500.0f } ) );
}
