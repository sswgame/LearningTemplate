#include "pch.h"

#include "Core/Memory/Memory.h"

#include "Editor/Panels/UIPreview/UIPreviewLogic.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Document/UIDocumentCache.h"
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Layout/UIResolutionPresetTable.h"
#include "Engine/UI/Layout/UIScale.h"
#include "Engine/UI/Layout/WidgetLayoutSlot.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/BorderPanel.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "TestFramework/Harness/TestHostRuntime.h"
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
    sw::UIResolutionPreset    fullHD;
    fullHD._name   = "1920x1080";
    fullHD._width  = 1920;
    fullHD._height = 1080;
    sw::UIResolutionPreset ultraHD;
    ultraHD._name             = "3840x2160";
    ultraHD._width            = 3840;
    ultraHD._height           = 2160;
    const sw::UIViewport full = UIPreviewLogic::makeViewport( settings, fullHD, 1.0f, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 1920.0f, full._physicalSize._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, full._uiScale, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1920.0f, full._size._x, 1e-2f );

    const sw::UIViewport uhd = UIPreviewLogic::makeViewport( settings, ultraHD, 1.0f, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, uhd._uiScale, 1e-4f ); // 4K = 기준의 두 배 — UI 단위 크기는 같다
    SW_EXPECT_NEAR_EQUAL( 1920.0f, uhd._size._x, 1e-2f );

    const sw::UIViewport doubled = UIPreviewLogic::makeViewport( settings, fullHD, 2.0f, 0.05f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, doubled._uiScale, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 960.0f, doubled._size._x, 1e-2f );
    SW_EXPECT_TRUE( doubled._safeInsets._x > 0.0f );
    SW_EXPECT_TRUE( UIPreviewLogic::makeTargetPath( full ) != UIPreviewLogic::makeTargetPath( uhd ) );

    // 패널의 견본은 데이터 표다 — 읽히고, 모든 줄이 이름과 양의 크기를 가진다.
    sw::UIResolutionPresetTable table;
    SW_ASSERT_TRUE( table.loadFromResource( ::test::getEngineDefaultAssets()._uiResolutionPresets.get() ) );
    for ( const sw::UIResolutionPreset& resolution : table._listResolution )
    {
        SW_EXPECT_TRUE_MSG( resolution._name.empty() == false && resolution._width > 0u && resolution._height > 0u, resolution._name.c_str() );
    }
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

/**
 * @brief [UIPreviewLogicTest] 언어 콤보는 게임 언어 → 실제 문화권(사전순, 중복 없음) → 의사 문화권 순이고, 의사 문화권은 목록에 없어도 끝에 들어간다
 */
SW_TEST_CASE( UIPreviewLogicTest, LanguageChoicesOrderRealThenPseudo )
{
    using sw::editor::UIPreviewLogic;
    sw::vector<sw::string> listAvailable;
    listAvailable.push_back( "ko" );
    listAvailable.push_back( "qps_ploc" );
    listAvailable.push_back( "en" );
    listAvailable.push_back( "ko" );
    listAvailable.push_back( "ko_kr" );
    sw::vector<sw::string> listCode;
    UIPreviewLogic::makeLanguageChoices( listAvailable, listCode );
    SW_ASSERT_EQUAL( 6u, static_cast<uint32>( listCode.size() ) );
    SW_EXPECT_STREQ( "", listCode[0].c_str() ); // 게임 언어 그대로
    SW_EXPECT_STREQ( "en", listCode[1].c_str() );
    SW_EXPECT_STREQ( "ko", listCode[2].c_str() );
    SW_EXPECT_STREQ( "ko_kr", listCode[3].c_str() );
    SW_EXPECT_STREQ( "qps_ploc", listCode[4].c_str() );
    SW_EXPECT_STREQ( "qps_plocm", listCode[5].c_str() );
    SW_EXPECT_TRUE( UIPreviewLogic::isPseudoLanguage( "qps_plocm" ) );
    SW_EXPECT_FALSE( UIPreviewLogic::isPseudoLanguage( "ko" ) );

    // 문화권 표가 없는 실행(목록이 비었다)도 게임 언어와 의사 문화권 둘은 고를 수 있다.
    UIPreviewLogic::makeLanguageChoices( {}, listCode );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( listCode.size() ) );
}

/**
 * @brief [UIPreviewLogicTest] 고른 위젯은 문서 순서 번호로 이어 간다 — 위젯 번호가 모두 바뀌는 새 트리에서도 같은 자리를 찾고, 프로퍼티 텍스트는 쓰고 읽으면 돌아온다
 * @details 문서를 저장하면 핫 리로드가 트리를 새로 지어 위젯 번호가 바뀐다. 그 뒤에도 고른 위젯 · 되돌리기 항목이 같은 위젯을 가리키려면 번호가 아니라 문서 순서가 키여야 한다.
 */
SW_TEST_CASE( UIPreviewLogicTest, SelectionFollowsDocumentOrderAndPropertyTextRoundTrips )
{
    using sw::editor::UIPreviewLogic;
    const auto makeTree = []( sw::WidgetTree& tree )
    {
        sw::unique_ptr<sw::BoxPanel>    root   = sw::make_unique<sw::BoxPanel>();
        sw::unique_ptr<sw::BorderPanel> border = sw::make_unique<sw::BorderPanel>();
        border->setName( "Card" );
        (void)root->addChild( std::move( border ) );
        tree.setRoot( std::move( root ) );
    };
    sw::WidgetTree first;
    makeTree( first );
    const sw::WidgetID cardID = first.findWidgetByName( "Card" )->getID();
    SW_EXPECT_EQUAL( 1u, UIPreviewLogic::findWidgetOrdinal( first, cardID ) );
    SW_EXPECT_EQUAL( UIPreviewLogic::kNoOrdinal, UIPreviewLogic::findWidgetOrdinal( first, sw::kInvalidWidgetID ) );

    sw::WidgetTree second; // 새로 지은 트리 — 번호가 다르다
    makeTree( second );
    const sw::WidgetID newCardID = second.findWidgetByName( "Card" )->getID();
    SW_EXPECT_TRUE( newCardID != cardID );
    SW_EXPECT_EQUAL( newCardID, UIPreviewLogic::findWidgetByOrdinal( second, 1u ) );
    SW_EXPECT_EQUAL( sw::kInvalidWidgetID, UIPreviewLogic::findWidgetByOrdinal( second, 9u ) );

    // 프로퍼티 텍스트: 쓰면 위젯 값이 바뀌고, 읽은 텍스트를 다시 쓰면 같은 값이다(되돌리기 항목이 이 텍스트를 쓴다).
    sw::Widget* pCard = second.findWidgetByName( "Card" );
    SW_ASSERT_NOT_NULL( pCard );
    const sw::PropertyInfo* pOpacity = UIPreviewLogic::findWidgetProperty( *pCard, "_opacity" );
    SW_ASSERT_NOT_NULL( pOpacity );
    const sw::string original = UIPreviewLogic::formatWidgetProperty( *pCard, *pOpacity );
    SW_EXPECT_TRUE( UIPreviewLogic::applyWidgetProperty( *pCard, *pOpacity, "0.25" ) );
    SW_EXPECT_NEAR_EQUAL( 0.25f, pCard->getOpacity(), 1e-4f );
    SW_EXPECT_TRUE( UIPreviewLogic::applyWidgetProperty( *pCard, *pOpacity, original ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pCard->getOpacity(), 1e-4f );
    SW_EXPECT_TRUE( UIPreviewLogic::findWidgetProperty( *pCard, "_noSuchProperty" ) == nullptr );
    SW_EXPECT_STREQ( "0.50 / 2.00 s", UIPreviewLogic::makeTimeLabel( 0.5f, 2.0f ).c_str() );
}

/**
 * @brief [UIPreviewLogicTest] 미리보기에서 고친 프로퍼티는 문서 텍스트에 들어가고, 고치지 않은 필드는 쓰지 않는다 — 저장이 쓰는 `makeDocumentText`
 * @details 저장은 `UIDocumentWriter` 가 화면 트리를 문서로 되쓰는 것이다. 값을 고친 위젯만 속성이 생기고(기본값은 생략), 스타일 시트 목록은 문서의 것이다.
 */
SW_TEST_CASE( UIPreviewLogicTest, EditedPropertyIsWrittenIntoTheDocumentText )
{
    using sw::editor::UIPreviewLogic;
    sw::InputManager input;
    sw::UISystem     ui;
    SW_ASSERT_TRUE( input.initialize() );
    SW_ASSERT_TRUE( ui.initialize( input, nullptr ) );
    ui.getDocumentCache().registerMemoryDocument( "test/preview_save.ui.xml",
                                                  "<UIDocument _schemaVersion=\"1\">\n\t<BoxPanel _spacing=\"4\">\n\t\t<BorderPanel _name=\"Card\" />\n\t</BoxPanel>\n</UIDocument>\n" );
    const sw::UIScreenHandle handle  = ui.openOffscreenScreen( "test/preview_save.ui.xml", "rendertarget/test_preview_save" );
    sw::UIScreen*            pScreen = ui.getOffscreenScreens().findScreen( handle );
    SW_ASSERT_NOT_NULL( pScreen );

    sw::string text;
    sw::string error;
    SW_ASSERT_TRUE( UIPreviewLogic::makeDocumentText( ui, *pScreen, "test/preview_save.ui.xml", text, error ) );
    SW_EXPECT_TRUE( text.find( "_opacity" ) == sw::string::npos ); // 기본값은 쓰지 않는다
    SW_EXPECT_TRUE( text.find( "Card" ) != sw::string::npos );

    sw::Widget* pCard = pScreen->getTree().findWidgetByName( "Card" );
    SW_ASSERT_NOT_NULL( pCard );
    const sw::PropertyInfo* pOpacity = UIPreviewLogic::findWidgetProperty( *pCard, "_opacity" );
    SW_ASSERT_NOT_NULL( pOpacity );
    SW_ASSERT_TRUE( UIPreviewLogic::applyWidgetProperty( *pCard, *pOpacity, "0.5" ) );
    SW_ASSERT_TRUE( UIPreviewLogic::makeDocumentText( ui, *pScreen, "test/preview_save.ui.xml", text, error ) );
    SW_EXPECT_TRUE( text.find( "_opacity=\"0.5\"" ) != sw::string::npos );

    // 없는 문서는 까닭과 함께 실패한다.
    {
        SW_TEST_DEFENSIVE_SCOPE( "a document that is not loaded cannot be written" );
        SW_EXPECT_FALSE( UIPreviewLogic::makeDocumentText( ui, *pScreen, "test/not_there.ui.xml", text, error ) );
        SW_EXPECT_FALSE( error.empty() );
    }
    ui.getOffscreenScreens().remove( handle );
    ui.shutdown();
    input.shutdown();
}

/**
 * @brief [UIPreviewLogicTest] 디자이너: 위젯을 끼우고 떼고 다시 끼우면 트리가 그대로 돌아오고, 놓을 자리는 패널이면 그 안 · 아니면 그 위젯 뒤이며, 패널이 아닌 곳에는 끼우지 못한다
 * @details 되돌리기 항목이 이 함수들로 구조를 되돌린다(추가 → 떼기, 지우기 → 같은 위젯 다시 끼우기). 문서 순서 번호로 부모를 가리키므로 앞 자식이 늘면 번호가 밀리지 않는지도 본다.
 */
SW_TEST_CASE( UIPreviewLogicTest, DesignerInsertDetachAndAttachRoundTrip )
{
    using sw::editor::UIPreviewLogic;
    sw::WidgetTree               tree;
    sw::unique_ptr<sw::BoxPanel> root  = sw::make_unique<sw::BoxPanel>();
    sw::BoxPanel*                pRoot = root.get();
    (void)pRoot->addChild( sw::make_unique<sw::TextWidget>() ); // 번호 1
    tree.setRoot( std::move( root ) );
    sw::vector<sw::editor::UIPreviewWidgetRow> listRow;
    UIPreviewLogic::collectWidgetRows( tree, listRow );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( listRow.size() ) );

    // 패널(루트)에 놓으면 자식 끝, 패널이 아닌 위젯(글)에 놓으면 그 바로 뒤이며 부모는 같다.
    uint32 index{ 99 };
    SW_EXPECT_TRUE( UIPreviewLogic::findDropContainer( tree, pRoot, index ) == pRoot );
    SW_EXPECT_EQUAL( 1u, index );
    sw::Widget* pText = tree.findWidgetByID( listRow[1]._widget );
    SW_ASSERT_NOT_NULL( pText );
    SW_EXPECT_TRUE( UIPreviewLogic::findDropContainer( tree, pText, index ) == pRoot );
    SW_EXPECT_EQUAL( 1u, index );                                                         // 글은 0 번이라 바로 뒤가 1 번
    SW_EXPECT_TRUE( UIPreviewLogic::findDropContainer( tree, nullptr, index ) == pRoot ); // 놓은 곳이 없으면 루트

    // 끼운다 — 두 번째 자식으로.
    sw::Widget* pAdded = UIPreviewLogic::insertWidget( tree, *sw::TextWidget::StaticType(), 0u, 1u );
    SW_ASSERT_NOT_NULL( pAdded );
    SW_EXPECT_EQUAL( 2u, pRoot->getChildCount() );
    SW_EXPECT_EQUAL( 2u, UIPreviewLogic::findWidgetOrdinal( tree, pAdded->getID() ) );

    // 떼면 그 위젯의 소유권이 나오고 트리는 처음과 같다. 같은 자리에 다시 끼우면 같은 위젯이다.
    sw::unique_ptr<sw::Widget> detached = UIPreviewLogic::detachWidget( tree, 0u, 1u );
    SW_ASSERT_NOT_NULL( detached.get() );
    SW_EXPECT_EQUAL( 1u, pRoot->getChildCount() );
    const sw::Widget* pSame = detached.get();
    SW_EXPECT_TRUE( UIPreviewLogic::attachWidget( tree, std::move( detached ), 0u, 1u ) == pSame );
    SW_EXPECT_EQUAL( 2u, pRoot->getChildCount() );

    // 패널이 아닌 곳(글)에는 끼우지 못하고, 없는 자리는 떼지 못한다.
    SW_EXPECT_TRUE( UIPreviewLogic::insertWidget( tree, *sw::TextWidget::StaticType(), 1u, 0u ) == nullptr );
    SW_EXPECT_TRUE( UIPreviewLogic::detachWidget( tree, 0u, 9u ) == nullptr );
    SW_EXPECT_TRUE( UIPreviewLogic::findPanelByOrdinal( tree, 1u ) == nullptr );
    SW_EXPECT_TRUE( UIPreviewLogic::findPanelByOrdinal( tree, 0u ) == pRoot );
}

/**
 * @brief [UIPreviewLogicTest] 슬롯 손잡이: 가운데는 두 오프셋을 함께, 변은 그 변만 옮기고, 자동 크기이면 오른쪽 · 아래는 옮기지 않으며, 캔버스 자식만 손잡이로 고친다
 */
SW_TEST_CASE( UIPreviewLogicTest, SlotHandlesMoveTheMatchingOffsets )
{
    using sw::editor::UIPreviewLogic;
    using sw::editor::UIPreviewSlotHandle;
    sw::WidgetLayoutSlot slot{};
    slot._offsetMin = sw::float2{ 10.0f, 20.0f };
    slot._offsetMax = sw::float2{ 110.0f, 70.0f };
    SW_EXPECT_FALSE( UIPreviewLogic::applySlotDrag( slot, UIPreviewSlotHandle::Move, sw::float2{} ) ); // 안 움직였다
    SW_EXPECT_TRUE( UIPreviewLogic::applySlotDrag( slot, UIPreviewSlotHandle::Move, sw::float2{ 5.0f, -5.0f } ) );
    SW_EXPECT_NEAR_EQUAL( 15.0f, slot._offsetMin._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 15.0f, slot._offsetMin._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 115.0f, slot._offsetMax._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 65.0f, slot._offsetMax._y, 1e-4f );
    SW_EXPECT_TRUE( UIPreviewLogic::applySlotDrag( slot, UIPreviewSlotHandle::Right, sw::float2{ 10.0f, 99.0f } ) ); // 오른쪽 변은 가로만 본다
    SW_EXPECT_NEAR_EQUAL( 125.0f, slot._offsetMax._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 65.0f, slot._offsetMax._y, 1e-4f );
    SW_EXPECT_TRUE( UIPreviewLogic::applySlotDrag( slot, UIPreviewSlotHandle::Top, sw::float2{ 99.0f, 4.0f } ) );
    SW_EXPECT_NEAR_EQUAL( 19.0f, slot._offsetMin._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 15.0f, slot._offsetMin._x, 1e-4f );
    slot._bAutoSize = true; // 크기가 내용에서 오므로 오른쪽 · 아래 오프셋은 쓰이지 않는다
    SW_EXPECT_FALSE( UIPreviewLogic::applySlotDrag( slot, UIPreviewSlotHandle::Bottom, sw::float2{ 0.0f, 8.0f } ) );
    SW_EXPECT_TRUE( UIPreviewLogic::applySlotDrag( slot, UIPreviewSlotHandle::Left, sw::float2{ -3.0f, 0.0f } ) );
    SW_EXPECT_NEAR_EQUAL( 12.0f, slot._offsetMin._x, 1e-4f );

    sw::WidgetTree                  tree;
    sw::unique_ptr<sw::CanvasPanel> canvas  = sw::make_unique<sw::CanvasPanel>();
    sw::CanvasPanel*                pCanvas = canvas.get();
    sw::Widget*                     pChild  = pCanvas->addChild( sw::make_unique<sw::BorderPanel>() );
    tree.setRoot( std::move( canvas ) );
    SW_EXPECT_TRUE( UIPreviewLogic::canEditSlotByHandle( *pChild ) );
    SW_EXPECT_FALSE( UIPreviewLogic::canEditSlotByHandle( *pCanvas ) ); // 루트는 부모가 없다
}

/**
 * @brief [UIPreviewLogicTest] 앵커 프리셋: 표의 앵커 값을 넣고, 위젯 사각형이 그대로 나오도록 오프셋을 다시 채운다. 같은 프리셋을 다시 고르면 바뀌지 않는다
 */
SW_TEST_CASE( UIPreviewLogicTest, AnchorPresetKeepsTheWidgetInPlace )
{
    using sw::editor::UIPreviewAnchorPreset;
    using sw::editor::UIPreviewLogic;
    const sw::UIRect rect{ 100.0f, 50.0f, 300.0f, 90.0f }; // 부모 왼쪽 위 원점
    const sw::float2 parentSize{ 1000.0f, 500.0f };

    sw::WidgetLayoutSlot slot{};
    slot._offsetMin = sw::float2{ 100.0f, 50.0f };
    slot._offsetMax = sw::float2{ 300.0f, 90.0f };
    SW_EXPECT_FALSE( UIPreviewLogic::applyAnchorPreset( slot, UIPreviewAnchorPreset::TopLeft, rect, parentSize ) ); // 이미 좌상

    SW_EXPECT_TRUE( UIPreviewLogic::applyAnchorPreset( slot, UIPreviewAnchorPreset::Center, rect, parentSize ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f, slot._anchorMin._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, slot._anchorMax._y, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( -400.0f, slot._offsetMin._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( -200.0f, slot._offsetMin._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( -200.0f, slot._offsetMax._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( -160.0f, slot._offsetMax._y, 1e-4f );

    SW_EXPECT_TRUE( UIPreviewLogic::applyAnchorPreset( slot, UIPreviewAnchorPreset::StretchHorizontal, rect, parentSize ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, slot._anchorMin._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, slot._anchorMax._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, slot._anchorMin._y, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 100.0f, slot._offsetMin._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( -700.0f, slot._offsetMax._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( -160.0f, slot._offsetMax._y, 1e-4f );

    SW_EXPECT_TRUE( UIPreviewLogic::applyAnchorPreset( slot, UIPreviewAnchorPreset::StretchAll, rect, parentSize ) );
    SW_EXPECT_NEAR_EQUAL( 100.0f, slot._offsetMin._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 50.0f, slot._offsetMin._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( -700.0f, slot._offsetMax._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( -410.0f, slot._offsetMax._y, 1e-4f );
    SW_EXPECT_FALSE( UIPreviewLogic::applyAnchorPreset( slot, UIPreviewAnchorPreset::StretchAll, rect, parentSize ) );

    // 어느 프리셋이든 앵커 × 부모 크기 + 오프셋이 원래 사각형이다(CanvasPanel 의 배치 식).
    for ( uint32 index = 0; index < static_cast<uint32>( UIPreviewAnchorPreset::Count ); ++index )
    {
        const auto preset = static_cast<UIPreviewAnchorPreset>( index );
        (void)UIPreviewLogic::applyAnchorPreset( slot, preset, rect, parentSize ); // 바뀌었는지가 아니라 배치 식이 맞는지를 본다
        SW_EXPECT_NEAR_EQUAL( rect._left, slot._anchorMin._x * parentSize._x + slot._offsetMin._x, 1e-3f );
        SW_EXPECT_NEAR_EQUAL( rect._bottom, slot._anchorMax._y * parentSize._y + slot._offsetMax._y, 1e-3f );
        SW_EXPECT_TRUE( UIPreviewLogic::getAnchorPresetMark( preset )[0] != '\0' );
    }
    const sw::editor::UIPreviewAnchorValues outOfRange = UIPreviewLogic::getAnchorPreset( UIPreviewAnchorPreset::Count ); // 범위 밖은 좌상
    SW_EXPECT_NEAR_EQUAL( 0.0f, outOfRange._anchorMax._x, 1e-6f );
    SW_EXPECT_STREQ( "topleft", UIPreviewLogic::getAnchorPresetMark( UIPreviewAnchorPreset::Count ) );
}

/**
 * @brief [UIPreviewLogicTest] 슬롯에 안쪽 여백이 있어도 앵커 프리셋을 고른 직후 위젯 사각형이 그대로다 — 슬롯 사각형(앵커 × 부모 + 오프셋)에서 여백을 뺀 것이 원래 위젯 사각형이다
 * @details UIPreview.U2 의 "부모 여백에서 밀리는지" 확인을 계산으로 본다. 변이: `applyAnchorPreset` 이 여백을 더하지 않으면 모든 프리셋에서 위젯이 여백만큼 밀리고 줄어 진다.
 */
SW_TEST_CASE( UIPreviewLogicTest, AnchorPresetKeepsTheWidgetInPlaceWithSlotPadding )
{
    using sw::editor::UIPreviewAnchorPreset;
    using sw::editor::UIPreviewLogic;
    const sw::UIRect rect{ 100.0f, 50.0f, 300.0f, 90.0f }; // 위젯의 지금 사각형(여백이 이미 들어간 자리)
    const sw::float2 parentSize{ 1000.0f, 500.0f };
    for ( uint32 index = 0; index < static_cast<uint32>( UIPreviewAnchorPreset::Count ); ++index )
    {
        sw::WidgetLayoutSlot slot{};
        slot._padding = sw::float4{ 10.0f, 20.0f, 30.0f, 40.0f };                                                       // 왼쪽, 위, 오른쪽, 아래
        (void)UIPreviewLogic::applyAnchorPreset( slot, static_cast<UIPreviewAnchorPreset>( index ), rect, parentSize ); // 바뀌었는지가 아니라 슬롯 사각형이 맞는지를 본다
        const float32 slotLeft   = slot._anchorMin._x * parentSize._x + slot._offsetMin._x;
        const float32 slotTop    = slot._anchorMin._y * parentSize._y + slot._offsetMin._y;
        const float32 slotRight  = slot._anchorMax._x * parentSize._x + slot._offsetMax._x;
        const float32 slotBottom = slot._anchorMax._y * parentSize._y + slot._offsetMax._y;
        // CanvasPanel 이 슬롯 사각형을 만들고 `UILayoutPass::applySlot` 이 여백만큼 안쪽에 위젯을 놓는다.
        SW_EXPECT_NEAR_EQUAL( rect._left, slotLeft + 10.0f, 1e-3f );
        SW_EXPECT_NEAR_EQUAL( rect._top, slotTop + 20.0f, 1e-3f );
        SW_EXPECT_NEAR_EQUAL( rect._right, slotRight - 30.0f, 1e-3f );
        SW_EXPECT_NEAR_EQUAL( rect._bottom, slotBottom - 40.0f, 1e-3f );
    }
}

/**
 * @brief [UIPreviewLogicTest] 상자 순서 손잡이 — 놓은 점보다 가운데가 앞인 자식 수가 사이 자리이고, 자기 뒤로 옮기면 떼어 낸 만큼 번호를 하나 당긴다
 */
SW_TEST_CASE( UIPreviewLogicTest, BoxReorderIndexFromDropPoint )
{
    using sw::editor::UIPreviewLogic;
    const sw::vector<float32> listCenter{ 10.0f, 30.0f, 50.0f, 70.0f };
    SW_EXPECT_EQUAL( 0u, UIPreviewLogic::findBoxInsertIndex( listCenter, 0.0f ) );
    SW_EXPECT_EQUAL( 2u, UIPreviewLogic::findBoxInsertIndex( listCenter, 40.0f ) );
    SW_EXPECT_EQUAL( 4u, UIPreviewLogic::findBoxInsertIndex( listCenter, 100.0f ) );
    SW_EXPECT_EQUAL( 0u, UIPreviewLogic::findBoxInsertIndex( sw::vector<float32>{}, 5.0f ) );

    // 1 번 자식을 끌어 — 맨 앞 · 제자리(자기 앞뒤 틈) · 맨 뒤
    SW_EXPECT_EQUAL( 0u, UIPreviewLogic::findReorderIndex( 1, UIPreviewLogic::findBoxInsertIndex( listCenter, 0.0f ) ) );
    SW_EXPECT_EQUAL( 1u, UIPreviewLogic::findReorderIndex( 1, UIPreviewLogic::findBoxInsertIndex( listCenter, 20.0f ) ) );
    SW_EXPECT_EQUAL( 1u, UIPreviewLogic::findReorderIndex( 1, UIPreviewLogic::findBoxInsertIndex( listCenter, 40.0f ) ) );
    SW_EXPECT_EQUAL( 2u, UIPreviewLogic::findReorderIndex( 1, UIPreviewLogic::findBoxInsertIndex( listCenter, 60.0f ) ) );
    SW_EXPECT_EQUAL( 3u, UIPreviewLogic::findReorderIndex( 1, UIPreviewLogic::findBoxInsertIndex( listCenter, 100.0f ) ) );

    // 오른쪽에서 왼쪽(RTL 거울) — 0 번 자식이 오른쪽 끝이라 오른쪽 바깥이 맨 앞, 왼쪽 바깥이 맨 뒤다
    const sw::vector<float32> listMirrored{ 70.0f, 50.0f, 30.0f, 10.0f };
    SW_EXPECT_EQUAL( 0u, UIPreviewLogic::findBoxInsertIndex( listMirrored, 100.0f ) );
    SW_EXPECT_EQUAL( 2u, UIPreviewLogic::findBoxInsertIndex( listMirrored, 40.0f ) );
    SW_EXPECT_EQUAL( 4u, UIPreviewLogic::findBoxInsertIndex( listMirrored, 0.0f ) );
}

/**
 * @brief [UIPreviewLogicTest] 격자 칸 손잡이 — 점이 속한 줄은 시작이 점 이하인 마지막 줄, 첫 줄 앞은 0, 끝을 넘으면 새 줄(줄 수), 폭 0 인 빈 줄은 건너뛴다
 */
SW_TEST_CASE( UIPreviewLogicTest, GridTrackFromDropPoint )
{
    using sw::editor::UIPreviewLogic;
    const sw::vector<float32> listStart{ 0.0f, 100.0f, 200.0f };
    SW_EXPECT_EQUAL( 0u, UIPreviewLogic::findGridTrack( listStart, 300.0f, -10.0f ) );
    SW_EXPECT_EQUAL( 0u, UIPreviewLogic::findGridTrack( listStart, 300.0f, 50.0f ) );
    SW_EXPECT_EQUAL( 1u, UIPreviewLogic::findGridTrack( listStart, 300.0f, 100.0f ) );
    SW_EXPECT_EQUAL( 2u, UIPreviewLogic::findGridTrack( listStart, 300.0f, 250.0f ) );
    SW_EXPECT_EQUAL( 3u, UIPreviewLogic::findGridTrack( listStart, 300.0f, 300.0f ) );
    SW_EXPECT_EQUAL( 0u, UIPreviewLogic::findGridTrack( sw::vector<float32>{}, 0.0f, 50.0f ) );

    // 폭 0 인 줄(빈 Auto 줄)은 건너뛴다 — 100~200 은 2 번 줄이다
    const sw::vector<float32> listGap{ 0.0f, 100.0f, 100.0f };
    SW_EXPECT_EQUAL( 2u, UIPreviewLogic::findGridTrack( listGap, 200.0f, 150.0f ) );
    SW_EXPECT_EQUAL( 0u, UIPreviewLogic::findGridTrack( listGap, 200.0f, 50.0f ) );
}

/**
 * @brief [UIPreviewLogicTest] 격자 줄 시작은 배치의 줄 크기와 간격으로 만든다 — 자식 없는 줄도 제 폭을 가져 그 칸에 놓을 수 있다
 */
SW_TEST_CASE( UIPreviewLogicTest, GridTrackStartsFromArrangedSizes )
{
    using sw::editor::UIPreviewLogic;
    sw::vector<float32> listStart;
    float32             trackEnd{ -1.0f };
    UIPreviewLogic::makeGridTrackStarts( sw::vector<float32>{ 100.0f, 50.0f, 80.0f }, 10.0f, listStart, trackEnd );
    SW_ASSERT_EQUAL( size_t{ 3 }, listStart.size() );
    SW_EXPECT_NEAR_EQUAL( 0.0f, listStart[0], 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 110.0f, listStart[1], 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 170.0f, listStart[2], 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 250.0f, trackEnd, 1.0e-4f );
    // 1 번 줄(110~160)에 자식이 없어도 그 줄을 고른다
    SW_EXPECT_EQUAL( 1u, UIPreviewLogic::findGridTrack( listStart, trackEnd, 130.0f ) );

    UIPreviewLogic::makeGridTrackStarts( sw::vector<float32>{}, 10.0f, listStart, trackEnd );
    SW_EXPECT_TRUE( listStart.empty() );
    SW_EXPECT_NEAR_EQUAL( 0.0f, trackEnd, 1.0e-4f );
}
