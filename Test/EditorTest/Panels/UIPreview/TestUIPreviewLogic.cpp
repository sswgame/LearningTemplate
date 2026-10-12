#include "pch.h"

#include "Core/Memory/Memory.h"

#include "Editor/Panels/UIPreview/UIPreviewLogic.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Document/UIDocumentCache.h"
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/UIScale.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"
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
    sw::UIScreen*            pScreen = ui.findOffscreenScreen( handle );
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
    ui.closeOffscreenScreen( handle );
    ui.shutdown();
    input.shutdown();
}
