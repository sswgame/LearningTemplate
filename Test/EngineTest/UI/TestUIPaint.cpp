#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Text/FontSystem.h"
#include "Engine/Text/GlyphCache.h"
#include "Engine/Text/TextLayout.h"
#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Base/UIEventRouter.h"
#include "Engine/UI/Base/UIFocusManager.h"
#include "Engine/UI/Base/UIPointerState.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Input/UIActionGlyphSource.h"
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Layout/OverlayPanel.h"
#include "Engine/UI/Layout/UILayoutPass.h"
#include "Engine/UI/Render/UIPaintPass.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/BorderPanel.h"
#include "Engine/UI/Widgets/ButtonWidget.h"
#include "Engine/UI/Widgets/CheckBoxWidget.h"
#include "Engine/UI/Widgets/ComboBoxWidget.h"
#include "Engine/UI/Widgets/ImageWidget.h"
#include "Engine/UI/Widgets/ListViewWidget.h"
#include "Engine/UI/Widgets/SliderWidget.h"
#include "Engine/UI/Widgets/TextInputWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "EngineTest/Text/FakeFontRasterizer.h"

#include "TestFramework/TestFramework.h"

// UIPaintTest — 위젯 그리기: 그림 캐시(더러운 위젯만 다시 칠하기) · 조상 불투명도 번짐 · 아틀라스 세대 · 글 위젯 무효화 · 그리기 순서(z 순서) ·
// UISystem 의 그리기 목록 내용 번호 · 포커스 테두리. 가짜 래스터라이저(nogpu).

namespace
{
    /** @brief 고정 크기의 단색 상자 위젯입니다. `paint` 호출 수를 셉니다. */
    class TestPaintWidget : public sw::Widget
    {
    public:
        TestPaintWidget( const sw::hashed_string& name, const sw::float2& size, const sw::float4& color )
            : sw::Widget{}
            , _size{ size }
            , _color{ color }
            , _paintCount{ 0 }
            , _bFocusable{ false }
        {
            setName( name );
        }

        void setColor( const sw::float4& color )
        {
            _color = color;
            invalidate( sw::WidgetDirty::kPaint );
        }
        uint32 getPaintCount() const { return _paintCount; }
        void   setFocusable( bool bFocusable ) { _bFocusable = bFocusable; }
        bool   supportsFocus() const override { return _bFocusable; }

    protected:
        sw::float2 computeDesiredSize( const sw::UILayoutContext& context, const sw::float2& availableSize ) const override
        {
            (void)context;
            (void)availableSize;
            return _size;
        }

        void paint( sw::CanvasPainter& painter, const sw::UIPaintContext& context ) const override
        {
            (void)context;
            ++_paintCount;
            sw::CanvasBrush brush{};
            brush._color = _color;
            painter.fillRect( sw::float2{}, getGeometry()._size, brush );
        }

    private:
        sw::float2     _size;
        sw::float4     _color;
        mutable uint32 _paintCount;
        bool           _bFocusable;
    };

    /** @brief 트리 하나 + 레이아웃 · 그리기 문맥 + 프레임 목록입니다. `runFrame` 이 레이아웃 → 그리기를 한 번 돌립니다. */
    struct UIPaintFixture
    {
        sw::WidgetTree      _tree;
        sw::UILayoutContext _layoutContext;
        sw::UIPaintContext  _paintContext;
        sw::CanvasDrawList  _canvas;

        UIPaintFixture( float32 width, float32 height )
            : _tree{}
            , _layoutContext{}
            , _paintContext{}
            , _canvas{}
        {
            _layoutContext._viewportSize = sw::float2{ width, height };
        }

        /** @brief 레이아웃 → 그리기. 다시 칠한 위젯 수를 돌려준다. */
        uint32 runFrame()
        {
            (void)sw::UILayoutPass::update( _tree, _layoutContext );
            _canvas.clear();
            _canvas._targetSize = _layoutContext._viewportSize;
            sw::CanvasPainter painter( _canvas, _paintContext._uiScale );
            return sw::UIPaintPass::paint( _tree, _paintContext, painter, _canvas );
        }

        template <typename PanelType>
        PanelType* setRoot()
        {
            sw::unique_ptr<PanelType> root  = sw::make_unique<PanelType>();
            PanelType* const          pRoot = root.get();
            _tree.setRoot( std::move( root ) );
            return pRoot;
        }

        static TestPaintWidget* addBox( sw::PanelWidget& parent, const sw::hashed_string& name, const sw::float4& color )
        {
            return static_cast<TestPaintWidget*>( parent.addChild( sw::make_unique<TestPaintWidget>( name, sw::float2{ 20.0f, 10.0f }, color ) ) );
        }
    };

    /** @brief 가짜 글꼴 + 글 배치기입니다(기본 가족 Latin — 글자 0.5 em). */
    struct UIPaintFontFixture
    {
        sw::test::FakeFontSystemFixture      _fonts;
        sw::unique_ptr<sw::TextLayoutEngine> _layout;
        bool                                 _bInitialized;

        UIPaintFontFixture()
            : _fonts{}
            , _layout{}
            , _bInitialized{ false }
        {
            sw::FontCatalogDesc catalog{};
            catalog._defaultFamily = "Latin";
            sw::test::FakeFontSystemFixture::addFamily( catalog, "Latin", "test/fonts/latin.ttf" );
            _bInitialized = _fonts.initialize( catalog );
            _layout       = sw::make_unique<sw::TextLayoutEngine>( *_fonts._fontSystem );
        }

        void bind( UIPaintFixture& fixture )
        {
            fixture._layoutContext._pTextLayout = _layout.get();
            fixture._paintContext._pTextLayout  = _layout.get();
            fixture._paintContext._pGlyphCache  = &_fonts._fontSystem->getGlyphCache();
        }
    };
    struct UIWidgetTestUtil
    {
        /** @brief 캔버스 자식을 왼쪽 위 (@p x, @p y) · 크기 (@p width, @p height) 에 고정합니다. */
        static void pin( sw::Widget& widget, float32 x, float32 y, float32 width, float32 height )
        {
            sw::WidgetLayoutSlot slot = widget.getLayoutSlot();
            slot._offsetMin           = sw::float2{ x, y };
            slot._offsetMax           = sw::float2{ x + width, y + height };
            widget.setLayoutSlot( slot );
        }

        static sw::UIPointerEvent makePointer( sw::UIPointerEventKind kind, float32 x, float32 y )
        {
            sw::UIPointerEvent event{};
            event._kind     = kind;
            event._position = sw::float2{ x, y };
            return event;
        }
    };
} // namespace

/**
 * @brief [UIPaintTest] 위젯 100 개 중 하나의 색만 바꾸면 그 하나만 다시 칠하고(paint 호출 1), 프레임 목록의 사각형 수는 그대로다
 * @details 변이: `UIPaintPass::paintWidget` 의 더러움 판정을 늘 참으로 두면 101 을 다시 칠해 진다.
 */
SW_TEST_CASE( UIPaintTest, OnlyDirtyWidgetsRepaint )
{
    UIPaintFixture fixture( 400.0f, 2000.0f );
    sw::BoxPanel*  pColumn = fixture.setRoot<sw::BoxPanel>();
    pColumn->setOrientation( sw::UIOrientation::Vertical );
    sw::vector<TestPaintWidget*> listBox;
    for ( uint32 index = 0; index < 100; ++index )
    {
        listBox.push_back( UIPaintFixture::addBox( *pColumn, sw::hashed_string( "box" + sw::to_string( index ) ), sw::float4{ 1.0f, 0.0f, 0.0f, 1.0f } ) );
    }
    SW_EXPECT_EQUAL( 101u, fixture.runFrame() ); // 처음은 모두(패널 포함)
    const size_t quadCount = fixture._canvas._listQuad.size();
    SW_EXPECT_EQUAL( size_t{ 100 }, quadCount );
    SW_EXPECT_EQUAL( 0u, fixture.runFrame() ); // 바뀐 것이 없다 — 캐시만 이어 붙인다
    SW_EXPECT_EQUAL( quadCount, fixture._canvas._listQuad.size() );

    listBox[42]->setColor( sw::float4{ 0.0f, 1.0f, 0.0f, 1.0f } );
    SW_EXPECT_EQUAL( 1u, fixture.runFrame() );
    SW_EXPECT_EQUAL( 2u, listBox[42]->getPaintCount() );
    SW_EXPECT_EQUAL( 1u, listBox[41]->getPaintCount() );
    SW_EXPECT_EQUAL( quadCount, fixture._canvas._listQuad.size() );
    SW_EXPECT_NEAR_EQUAL( 1.0f, fixture._canvas._listQuad[42]._color._y, 1e-6f );
}

/**
 * @brief [UIPaintTest] 부모 불투명도 0.5 는 자식 사각형 알파를 0.5 배로 만들고, 부모 불투명도만 바꿔도 자식 캐시가 다시 칠해진다
 * @details 변이: 자손 강제 다시 칠하기(`kSubtreePaintBits`)를 빼면 자식 알파가 0.5 로 남아 진다.
 */
SW_TEST_CASE( UIPaintTest, ParentOpacityPropagatesToChildren )
{
    UIPaintFixture    fixture( 200.0f, 100.0f );
    sw::OverlayPanel* pRoot   = fixture.setRoot<sw::OverlayPanel>();
    sw::BoxPanel*     pParent = static_cast<sw::BoxPanel*>( pRoot->addChild( sw::make_unique<sw::BoxPanel>() ) );
    TestPaintWidget*  pChild  = UIPaintFixture::addBox( *pParent, "child", sw::float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
    pParent->setOpacity( 0.5f );
    (void)fixture.runFrame();
    SW_ASSERT_EQUAL( size_t{ 1 }, fixture._canvas._listQuad.size() );
    SW_EXPECT_NEAR_EQUAL( 0.5f, fixture._canvas._listQuad[0]._color._w, 1e-6f );

    pParent->setOpacity( 0.25f );
    (void)fixture.runFrame();
    SW_EXPECT_EQUAL( 2u, pChild->getPaintCount() );
    SW_EXPECT_NEAR_EQUAL( 0.25f, fixture._canvas._listQuad[0]._color._w, 1e-6f );
}

/**
 * @brief [UIPaintTest] 글리프 아틀라스 세대가 오르면(페이지를 비웠다) 글 위젯만 다시 칠한다 — 상자 위젯은 캐시 그대로
 * @details 변이: `paintWidget` 의 아틀라스 세대 조건을 빼면 글 위젯이 옛 아틀라스 사각형을 든 채 남아 다시 칠한 수가 0 이다.
 */
SW_TEST_CASE( UIPaintTest, AtlasEvictionRepaintsText )
{
    UIPaintFontFixture fonts;
    SW_ASSERT_TRUE( fonts._bInitialized );
    UIPaintFixture fixture( 400.0f, 100.0f );
    fonts.bind( fixture );
    sw::BoxPanel*   pRow  = fixture.setRoot<sw::BoxPanel>();
    sw::TextWidget* pText = static_cast<sw::TextWidget*>( pRow->addChild( sw::make_unique<sw::TextWidget>() ) );
    pText->setText( "abc" );
    TestPaintWidget* pBox = UIPaintFixture::addBox( *pRow, "box", sw::float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
    (void)fixture.runFrame();
    SW_EXPECT_EQUAL( size_t{ 4 }, fixture._canvas._listQuad.size() ); // 글리프 셋 + 상자
    SW_EXPECT_EQUAL( 0u, fixture.runFrame() );

    fixture._paintContext._atlasGeneration += 1;
    SW_EXPECT_EQUAL( 1u, fixture.runFrame() );
    SW_EXPECT_EQUAL( 1u, pBox->getPaintCount() );
    SW_EXPECT_EQUAL( size_t{ 4 }, fixture._canvas._listQuad.size() );
}

/**
 * @brief [UIPaintTest] 글 위젯: 글을 바꾸면 레이아웃 더러움(kLayout), 색만 바꾸면 그리기 더러움(kPaint)뿐이다 — 원하는 크기는 글자 배율을 따른다
 * @details 변이: `TextWidget::setColor` 가 kLayout 을 함께 걸면 진다.
 */
SW_TEST_CASE( UIPaintTest, TextWidgetLayoutVsPaintInvalidation )
{
    UIPaintFontFixture fonts;
    SW_ASSERT_TRUE( fonts._bInitialized );
    UIPaintFixture fixture( 400.0f, 100.0f );
    fonts.bind( fixture );
    sw::OverlayPanel*   pRoot = fixture.setRoot<sw::OverlayPanel>();
    sw::TextWidget*     pText = static_cast<sw::TextWidget*>( pRoot->addChild( sw::make_unique<sw::TextWidget>() ) );
    sw::TextLayoutStyle style{};
    style._fontSize = 10.0f;
    pText->setTextStyle( style );
    pText->setText( "abcd" );
    (void)fixture.runFrame();
    SW_EXPECT_NEAR_EQUAL( 20.0f, pText->getDesiredSize()._x, 1e-4f ); // 글자 넷 × 0.5 em × 10
    SW_EXPECT_EQUAL( 0u, pText->getDirtyFlags() );

    pText->setColor( sw::float4{ 1.0f, 0.0f, 0.0f, 1.0f } );
    SW_EXPECT_TRUE( ( pText->getDirtyFlags() & sw::WidgetDirty::kPaint ) != 0 );
    SW_EXPECT_TRUE( ( pText->getDirtyFlags() & sw::WidgetDirty::kLayout ) == 0 );
    (void)fixture.runFrame();
    SW_EXPECT_NEAR_EQUAL( 1.0f, fixture._canvas._listQuad[0]._color._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, fixture._canvas._listQuad[0]._color._y, 1e-6f );

    pText->setText( "abcdef" );
    SW_EXPECT_TRUE( ( pText->getDirtyFlags() & sw::WidgetDirty::kLayout ) != 0 );
    fixture._layoutContext._textScale = 2.0f;
    fixture._paintContext._textScale  = 2.0f;
    (void)fixture.runFrame();
    SW_EXPECT_NEAR_EQUAL( 60.0f, pText->getDesiredSize()._x, 1e-4f ); // 여섯 × 0.5 × 10 × 2
    SW_EXPECT_EQUAL( size_t{ 6 }, fixture._canvas._listQuad.size() );
}

/**
 * @brief [UIPaintTest] 사각형은 그리기 순서로 쌓인다 — 형제는 자식 순서, 캔버스 패널은 z 순서(같으면 자식 순서), 자식은 부모 위
 * @details 변이: 그리기 걷기가 `collectPaintOrder` 를 무시하면 red · green · blue 순서가 되어 진다.
 */
SW_TEST_CASE( UIPaintTest, CanvasOrderFollowsTreeOrder )
{
    UIPaintFixture   fixture( 400.0f, 100.0f );
    sw::CanvasPanel* pCanvas = fixture.setRoot<sw::CanvasPanel>();
    TestPaintWidget* pRed    = UIPaintFixture::addBox( *pCanvas, "red", sw::float4{ 1.0f, 0.0f, 0.0f, 1.0f } );
    UIPaintFixture::addBox( *pCanvas, "green", sw::float4{ 0.0f, 1.0f, 0.0f, 1.0f } );
    TestPaintWidget*     pBlue = UIPaintFixture::addBox( *pCanvas, "blue", sw::float4{ 0.0f, 0.0f, 1.0f, 1.0f } );
    sw::WidgetLayoutSlot slot  = pRed->getLayoutSlot();
    slot._zOrder               = 2;
    pRed->setLayoutSlot( slot );
    slot         = pBlue->getLayoutSlot();
    slot._zOrder = -1;
    pBlue->setLayoutSlot( slot );
    (void)fixture.runFrame();
    SW_ASSERT_EQUAL( size_t{ 3 }, fixture._canvas._listQuad.size() );
    SW_EXPECT_NEAR_EQUAL( 1.0f, fixture._canvas._listQuad[0]._color._z, 1e-6f ); // blue(z -1)
    SW_EXPECT_NEAR_EQUAL( 1.0f, fixture._canvas._listQuad[1]._color._y, 1e-6f ); // green(z 0)
    SW_EXPECT_NEAR_EQUAL( 1.0f, fixture._canvas._listQuad[2]._color._x, 1e-6f ); // red(z 2)

    // z 순서만 바꿔도(기하는 그대로) 순서가 따라온다.
    slot         = pRed->getLayoutSlot();
    slot._zOrder = -2;
    pRed->setLayoutSlot( slot );
    (void)fixture.runFrame();
    SW_EXPECT_NEAR_EQUAL( 1.0f, fixture._canvas._listQuad[0]._color._x, 1e-6f );
}

/**
 * @brief [UIPaintTest] 테두리 패널은 배경 · 그림자를 칠하고 자식을 안쪽 여백만큼 들인다 · 이미지 위젯은 RTL 에서 `_bMirrorInRtl` 이면 UV 를 좌우로 뒤집는다
 * @details 변이: `ImageWidget::paint` 의 거울 분기를 빼면 u0 < u1 로 남아 진다.
 */
SW_TEST_CASE( UIPaintTest, BorderPaddingAndImageMirror )
{
    UIPaintFixture   fixture( 200.0f, 100.0f );
    sw::BorderPanel* pBorder = fixture.setRoot<sw::BorderPanel>();
    pBorder->setBackground( sw::UIBrush::makeSolid( sw::float4{ 0.1f, 0.1f, 0.1f, 1.0f }, 4.0f ) );
    pBorder->setContentPadding( sw::float4{ 10.0f, 5.0f, 10.0f, 5.0f } );
    sw::ImageWidget* pImage = static_cast<sw::ImageWidget*>( pBorder->addChild( sw::make_unique<sw::ImageWidget>() ) );
    pImage->setImage( sw::make_shared<sw::Texture2D>() );
    pImage->setMirrorInRtl( true );
    (void)fixture.runFrame();
    SW_EXPECT_NEAR_EQUAL( 10.0f, pImage->getGeometry()._position._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, pImage->getGeometry()._position._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 180.0f, pImage->getGeometry()._size._x, 1e-4f );
    SW_ASSERT_EQUAL( size_t{ 2 }, fixture._canvas._listQuad.size() ); // 배경 + 그림
    SW_EXPECT_TRUE( fixture._canvas._listQuad[1]._uvRect._x < fixture._canvas._listQuad[1]._uvRect._z );

    fixture._layoutContext._bRightToLeft = true;
    (void)fixture.runFrame();
    SW_EXPECT_TRUE( pImage->isRightToLeft() );
    SW_EXPECT_TRUE( fixture._canvas._listQuad[1]._uvRect._x > fixture._canvas._listQuad[1]._uvRect._z );
}

/**
 * @brief [UIPaintTest] 이미지 위젯의 그림 경로(`_imagePath` — 문서 · `setImagePath`)는 그림 사각형으로 칠해지고 그 일괄이 경로를 든다 · 텍스처 객체가 있으면 객체가 이긴다
 * @details 게임 스레드의 위젯은 디바이스가 없어 텍스처를 풀 수 없다 — 경로를 그리기 목록에 싣고 렌더 스레드가 푼다. 변이: `ImageWidget::paint` 가 경로를 브러시에
 *          넣지 않으면 그림 사각형이 나오지 않아 진다.
 */
SW_TEST_CASE( UIPaintTest, ImagePathPaintsAnImageQuad )
{
    UIPaintFixture   fixture( 200.0f, 100.0f );
    sw::ImageWidget* pImage = fixture.setRoot<sw::ImageWidget>();
    pImage->setImagePath( "game/x/textures/crosshair.dds" );
    (void)fixture.runFrame();
    SW_ASSERT_EQUAL( size_t{ 1 }, fixture._canvas._listQuad.size() );
    SW_EXPECT_EQUAL( static_cast<uint32>( sw::CanvasQuadKind::Image ), fixture._canvas._listQuad[0]._kind );
    SW_ASSERT_EQUAL( size_t{ 1 }, fixture._canvas._listBatch.size() );
    SW_EXPECT_TRUE( fixture._canvas._listBatch[0]._arrTexture[0]._texturePath == sw::hashed_string( "game/x/textures/crosshair.dds" ) );

    pImage->setImage( sw::make_shared<sw::Texture2D>() );
    (void)fixture.runFrame();
    SW_ASSERT_EQUAL( size_t{ 1 }, fixture._canvas._listBatch.size() );
    SW_EXPECT_TRUE( fixture._canvas._listBatch[0]._arrTexture[0]._texture != nullptr );
    SW_EXPECT_TRUE( fixture._canvas._listBatch[0]._arrTexture[0]._texturePath.empty() );
}

/**
 * @brief [UIPaintTest] 화면 그리기를 막으면(에디터 멈춤 · Simulate — 게임 update 가 돌지 않는다) 그리기 목록이 비고, 화면 상태(로딩 화면이 떠 있음)는 그대로다
 * @details 게임이 연 로딩 화면은 게임 update 가 닫는다. 에디터가 멈춰 있으면 그 update 가 돌지 않아 첫 Play 전까지 로딩 화면이 게임 뷰를 덮었다.
 */
SW_TEST_CASE( UIPaintTest, OnScreenSuppressionHidesScreensButKeepsTheirState )
{
    sw::UISystem                     ui;
    sw::unique_ptr<sw::OverlayPanel> root = sw::make_unique<sw::OverlayPanel>();
    (void)UIPaintFixture::addBox( *root, "spinner", sw::float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
    sw::UIScreenDesc desc{};
    desc._layer = sw::UILayer::Loading;
    (void)ui.pushScreen( sw::make_unique<sw::UIScreen>( desc, std::move( root ) ) );
    sw::UIViewport viewport{};
    viewport._size         = sw::float2{ 800.0f, 600.0f };
    viewport._physicalSize = viewport._size;

    ui.update( 1.0f / 60.0f, viewport );
    SW_EXPECT_EQUAL( size_t{ 1 }, ui.getCanvas()._listQuad.size() );
    SW_EXPECT_TRUE( ui.isLoadingScreenShown() );

    ui.setOnScreenSuppressed( true );
    ui.update( 1.0f / 60.0f, viewport );
    SW_EXPECT_TRUE( ui.getCanvas()._listQuad.empty() );
    SW_EXPECT_TRUE( ui.isLoadingScreenShown() ); // 상태는 그대로 — 게임 update 가 다시 돌면 닫힌다

    ui.setOnScreenSuppressed( false );
    ui.update( 1.0f / 60.0f, viewport );
    SW_EXPECT_EQUAL( size_t{ 1 }, ui.getCanvas()._listQuad.size() );
}

/**
 * @brief [UIPaintTest] UISystem 의 그리기 목록 내용 번호는 내용이 바뀔 때만 오르고, 탐색 입력 방식이면 포커스 위젯 둘레에 테두리 사각형이 하나 더 든다
 * @details 변이: `UISystem::paintScreens` 의 같은 내용 확인을 빼면 바뀐 것이 없는 프레임에도 번호가 올라 진다.
 */
SW_TEST_CASE( UIPaintTest, UISystemCanvasRevisionAndFocusRing )
{
    sw::UISystem                     ui;
    sw::unique_ptr<sw::OverlayPanel> root = sw::make_unique<sw::OverlayPanel>();
    TestPaintWidget*                 pBox = UIPaintFixture::addBox( *root, "box", sw::float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
    pBox->setFocusable( true );
    const sw::UIScreenHandle screen = ui.pushScreen( sw::make_unique<sw::UIScreen>( sw::UIScreenDesc{}, std::move( root ) ) );
    sw::UIViewport           viewport{};
    viewport._size         = sw::float2{ 800.0f, 600.0f };
    viewport._physicalSize = viewport._size;

    ui.update( 1.0f / 60.0f, viewport );
    const uint64 firstRevision = ui.getCanvasRevision();
    SW_EXPECT_EQUAL( size_t{ 1 }, ui.getCanvas()._listQuad.size() );
    ui.update( 1.0f / 60.0f, viewport );
    SW_EXPECT_EQUAL( firstRevision, ui.getCanvasRevision() );

    pBox->setColor( sw::float4{ 0.0f, 1.0f, 0.0f, 1.0f } );
    ui.update( 1.0f / 60.0f, viewport );
    SW_EXPECT_EQUAL( firstRevision + 1, ui.getCanvasRevision() );

    SW_ASSERT_TRUE( ui.getFocusManager().setFocus( ui.findScreen( screen )->getTree(), pBox->getID() ) );
    ui.update( 1.0f / 60.0f, viewport );
    SW_EXPECT_EQUAL( size_t{ 1 }, ui.getCanvas()._listQuad.size() ); // 포인터 방식 — 테두리 없음
    ui.setInputMode( sw::UIInputMode::Navigation );
    ui.update( 1.0f / 60.0f, viewport );
    SW_ASSERT_EQUAL( size_t{ 2 }, ui.getCanvas()._listQuad.size() );
    SW_EXPECT_TRUE( ui.getCanvas()._listQuad[1]._params._x > 0.0f ); // 테두리 두께
    ui.getFocusManager().clearFocus();
}

/**
 * @brief [UIPaintTest] 버튼 클릭은 같은 버튼 위에서 누르고 뗀 것이다 — 누르면 포인터를 잡고 포커스를 받고, 밖에서 떼면 클릭이 아니다. 포커스 버튼의 UI.Accept 도 클릭이다
 * @details 변이: `ButtonWidget::onPointerEvent` 의 "뗀 자리가 버튼 안" 조건을 빼면 밖에서 뗀 것도 클릭이 되어 진다.
 */
SW_TEST_CASE( UIPaintTest, ButtonClickRequiresPressAndReleaseOnSameWidget )
{
    UIPaintFixture    fixture( 400.0f, 300.0f );
    sw::CanvasPanel*  pCanvas = fixture.setRoot<sw::CanvasPanel>();
    sw::ButtonWidget* pButton = static_cast<sw::ButtonWidget*>( pCanvas->addChild( sw::make_unique<sw::ButtonWidget>() ) );
    UIWidgetTestUtil::pin( *pButton, 10.0f, 10.0f, 100.0f, 40.0f );
    uint32 notified = 0;
    (void)pButton->getOnClicked().add( [&notified]( sw::WidgetID )
    { ++notified; } );
    (void)fixture.runFrame();

    sw::UIPointerState        pointer;
    const sw::UIPointerResult down = pointer.process( fixture._tree, UIWidgetTestUtil::makePointer( sw::UIPointerEventKind::Down, 50.0f, 30.0f ) );
    SW_EXPECT_EQUAL( pButton->getID(), down._focusRequest );
    SW_EXPECT_EQUAL( pButton->getID(), pointer.getCapturedWidget() );
    SW_EXPECT_TRUE( pButton->isPressed() );
    (void)pointer.process( fixture._tree, UIWidgetTestUtil::makePointer( sw::UIPointerEventKind::Up, 60.0f, 35.0f ) );
    SW_EXPECT_EQUAL( 1u, pButton->getClickCount() );
    SW_EXPECT_EQUAL( 1u, notified );

    (void)pointer.process( fixture._tree, UIWidgetTestUtil::makePointer( sw::UIPointerEventKind::Down, 50.0f, 30.0f ) );
    (void)pointer.process( fixture._tree, UIWidgetTestUtil::makePointer( sw::UIPointerEventKind::Up, 300.0f, 200.0f ) ); // 밖 — 잡고 있어 버튼이 받지만 클릭은 아니다
    SW_EXPECT_EQUAL( 1u, pButton->getClickCount() );
    SW_EXPECT_FALSE( pButton->isPressed() );
    SW_EXPECT_EQUAL( sw::kInvalidWidgetID, pointer.getCapturedWidget() );

    sw::UIWidgetPath path{};
    SW_ASSERT_TRUE( sw::UIEventRouter::makePathTo( fixture._tree, pButton->getID(), path ) );
    sw::UIActionEvent accept{};
    accept._action = sw::hashed_string( sw::UIActionName::kAccept );
    sw::WidgetID handler{ sw::kInvalidWidgetID };
    SW_EXPECT_TRUE( sw::UIEventRouter::routeActionEvent( fixture._tree, path, accept, handler ).isHandled() );
    SW_EXPECT_EQUAL( 2u, pButton->getClickCount() );
}

/**
 * @brief [UIPaintTest] 체크 상자는 클릭마다 켜짐 · 꺼짐이 바뀌고 알림을 부르며, 켜지면 상자 안에 표시 사각형이 하나 더 든다
 * @details 변이: `CheckBoxWidget::handleClick` 의 setChecked 를 빼면 진다.
 */
SW_TEST_CASE( UIPaintTest, CheckBoxTogglesOnClick )
{
    UIPaintFixture      fixture( 400.0f, 300.0f );
    sw::CanvasPanel*    pCanvas = fixture.setRoot<sw::CanvasPanel>();
    sw::CheckBoxWidget* pCheck  = static_cast<sw::CheckBoxWidget*>( pCanvas->addChild( sw::make_unique<sw::CheckBoxWidget>() ) );
    UIWidgetTestUtil::pin( *pCheck, 0.0f, 0.0f, 100.0f, 30.0f );
    bool bLast = false;
    (void)pCheck->getOnCheckedChanged().add( [&bLast]( bool bChecked )
    { bLast = bChecked; } );
    sw::UIPointerState pointer;
    (void)fixture.runFrame();
    (void)pointer.process( fixture._tree, UIWidgetTestUtil::makePointer( sw::UIPointerEventKind::Move, 10.0f, 10.0f ) ); // 호버 바탕을 먼저
    (void)fixture.runFrame();
    const size_t uncheckedQuads = fixture._canvas._listQuad.size();

    (void)pointer.process( fixture._tree, UIWidgetTestUtil::makePointer( sw::UIPointerEventKind::Down, 10.0f, 10.0f ) );
    (void)pointer.process( fixture._tree, UIWidgetTestUtil::makePointer( sw::UIPointerEventKind::Up, 10.0f, 10.0f ) );
    SW_EXPECT_TRUE( pCheck->isChecked() );
    SW_EXPECT_TRUE( bLast );
    (void)fixture.runFrame();
    SW_EXPECT_EQUAL( uncheckedQuads + 1, fixture._canvas._listQuad.size() );
    (void)pointer.process( fixture._tree, UIWidgetTestUtil::makePointer( sw::UIPointerEventKind::Down, 10.0f, 10.0f ) );
    (void)pointer.process( fixture._tree, UIWidgetTestUtil::makePointer( sw::UIPointerEventKind::Up, 10.0f, 10.0f ) );
    SW_EXPECT_FALSE( pCheck->isChecked() );
    SW_EXPECT_FALSE( bLast );
}

/**
 * @brief [UIPaintTest] 포커스 슬라이더는 왼쪽 행동(키 Left)을 먹고 값이 한 칸 준다 — 포커스는 슬라이더에 남는다. 아래 행동은 다음 버튼으로 포커스를 옮긴다
 * @details 변이: `SliderWidget::onActionEvent` 가 처리하지 않으면 포커스가 옮겨지거나(위 · 아래 버튼만 있다 — 왼쪽은 이웃이 없어 그대로) 값이 그대로라 진다.
 */
SW_TEST_CASE( UIPaintTest, SliderTakesLeftRightActions )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::UISystem ui;
    SW_ASSERT_TRUE( ui.initialize( input, nullptr, "engine/input/ui.input.xml" ) );
    sw::unique_ptr<sw::BoxPanel> root = sw::make_unique<sw::BoxPanel>();
    root->setOrientation( sw::UIOrientation::Vertical );
    sw::SliderWidget* pSlider = static_cast<sw::SliderWidget*>( root->addChild( sw::make_unique<sw::SliderWidget>() ) );
    pSlider->setRange( 0.0f, 10.0f, 1.0f );
    pSlider->setValue( 5.0f );
    sw::ButtonWidget*        pBelow = static_cast<sw::ButtonWidget*>( root->addChild( sw::make_unique<sw::ButtonWidget>() ) );
    const sw::UIScreenHandle screen = ui.pushScreen( sw::make_unique<sw::UIScreen>( sw::UIScreenDesc{}, std::move( root ) ) );
    sw::UIViewport           viewport{};
    viewport._size         = sw::float2{ 800.0f, 600.0f };
    viewport._physicalSize = viewport._size;
    const auto runFrame    = [&]()
    {
        input.beginFrame( 1.0f / 60.0f );
        ui.processInput( 1.0f / 60.0f );
        ui.update( 1.0f / 60.0f, viewport );
        input.endFrame();
    };
    runFrame();
    SW_ASSERT_TRUE( ui.getFocusManager().setFocus( ui.findScreen( screen )->getTree(), pSlider->getID() ) );

    SW_EXPECT_TRUE( input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::Left ) ) );
    runFrame();
    SW_EXPECT_NEAR_EQUAL( 4.0f, pSlider->getValue(), 1e-6f );
    SW_EXPECT_EQUAL( pSlider->getID(), ui.getFocusManager().getFocusedWidget() );
    SW_EXPECT_TRUE( input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::Left ) ) );
    runFrame();

    SW_EXPECT_TRUE( input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::Down ) ) );
    runFrame();
    SW_EXPECT_EQUAL( pBelow->getID(), ui.getFocusManager().getFocusedWidget() );
    ui.shutdown();
    input.shutdown();
}

/** @brief [UIPaintTest] 진행 막대는 비율만큼 채우고, 오른쪽에서 왼쪽이면 오른쪽부터 찬다 */
SW_TEST_CASE( UIPaintTest, ProgressBarFillsFromFlowStart )
{
    UIPaintFixture         fixture( 400.0f, 300.0f );
    sw::CanvasPanel*       pCanvas = fixture.setRoot<sw::CanvasPanel>();
    sw::ProgressBarWidget* pBar    = static_cast<sw::ProgressBarWidget*>( pCanvas->addChild( sw::make_unique<sw::ProgressBarWidget>() ) );
    UIWidgetTestUtil::pin( *pBar, 0.0f, 0.0f, 200.0f, 10.0f );
    pBar->setPercent( 0.25f );
    (void)fixture.runFrame();
    SW_ASSERT_EQUAL( size_t{ 2 }, fixture._canvas._listQuad.size() );
    SW_EXPECT_NEAR_EQUAL( 50.0f, fixture._canvas._listQuad[1]._rect._z, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, fixture._canvas._listQuad[1]._rect._x, 1e-4f );
    fixture._layoutContext._bRightToLeft = true;
    (void)fixture.runFrame();
    SW_EXPECT_NEAR_EQUAL( 350.0f, fixture._canvas._listQuad[1]._rect._x, 1e-4f ); // 캔버스 거울(400 − 200) + 막대 안 150
}

/**
 * @brief [UIPaintTest] 가상 목록: 1 만 항목 · 보이는 12 줄이면 줄 위젯은 13 개(≤ 14)만 만들고, 스크롤하면 새로 만들지 않고 다시 묶는다 — 항목 k 는 늘 줄 k % 13
 * @details 변이: `ListViewWidget::arrangeChildren` 이 보이는 줄 수 대신 항목 수만큼 만들면 진다.
 */
SW_TEST_CASE( UIPaintTest, ListViewCreatesOnlyVisibleRows )
{
    UIPaintFixture      fixture( 300.0f, 240.0f );
    sw::ListViewWidget* pList        = fixture.setRoot<sw::ListViewWidget>();
    uint32              createdCount = 0;
    sw::vector<uint32>  listBoundItem;
    pList->setRowHeight( 20.0f );
    pList->setRowFactory( [&createdCount]() -> sw::unique_ptr<sw::Widget>
    {
        ++createdCount;
        return sw::make_unique<TestPaintWidget>( sw::hashed_string{}, sw::float2{ 10.0f, 20.0f }, sw::float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
    } );
    pList->setRowBinder( [&listBoundItem]( sw::Widget& row, uint32 itemIndex )
    {
        (void)row;
        listBoundItem.push_back( itemIndex );
    } );
    pList->setItemCount( 10000 );
    (void)fixture.runFrame();
    SW_EXPECT_EQUAL( 13u, createdCount );
    SW_EXPECT_EQUAL( 13u, pList->getCreatedRowCount() );
    SW_EXPECT_EQUAL( size_t{ 13 }, listBoundItem.size() );

    pList->setScrollOffset( 1000.0f ); // 항목 50 부터
    (void)fixture.runFrame();
    SW_EXPECT_EQUAL( 13u, createdCount );
    SW_EXPECT_EQUAL( 50u, pList->findItemIndex( *pList->getChild( 50 % 13 ) ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pList->getChild( 50 % 13 )->getGeometry()._position._y, 1e-4f );
    const size_t bindsAfterJump = listBoundItem.size();
    pList->setScrollOffset( 1020.0f ); // 한 줄 — 한 위젯만 다시 묶는다
    (void)fixture.runFrame();
    SW_EXPECT_EQUAL( bindsAfterJump + 1, listBoundItem.size() );
    SW_EXPECT_EQUAL( 63u, listBoundItem.back() );
}

/**
 * @brief [UIPaintTest] 콤보 상자는 클릭하면 팝업 화면(항목 버튼 · 상자 아래)을 올리고, 항목을 고르면 값 · 알림을 바꾸고 팝업을 닫는다
 * @details 변이: `ComboBoxPopupScreen::choose` 가 상자를 부르지 않으면 고른 값이 그대로라 진다.
 */
SW_TEST_CASE( UIPaintTest, ComboBoxOpensPopupAndSelects )
{
    sw::UISystem                    ui;
    sw::unique_ptr<sw::CanvasPanel> root   = sw::make_unique<sw::CanvasPanel>();
    sw::ComboBoxWidget*             pCombo = static_cast<sw::ComboBoxWidget*>( root->addChild( sw::make_unique<sw::ComboBoxWidget>() ) );
    UIWidgetTestUtil::pin( *pCombo, 100.0f, 50.0f, 200.0f, 40.0f );
    pCombo->setOptions( sw::vector<sw::string>{ "Low", "Medium", "High" } );
    pCombo->setSelectedIndex( 0 );
    uint32 chosen = sw::invalid_index::kUint32;
    (void)pCombo->getOnSelectionChanged().add( [&chosen]( uint32 index )
    { chosen = index; } );
    const sw::UIScreenHandle screen = ui.pushScreen( sw::make_unique<sw::UIScreen>( sw::UIScreenDesc{}, std::move( root ) ) );
    sw::UIViewport           viewport{};
    viewport._size         = sw::float2{ 800.0f, 600.0f };
    viewport._physicalSize = viewport._size;
    ui.update( 1.0f / 60.0f, viewport );

    sw::UIPointerState pointer;
    sw::WidgetTree&    tree = ui.findScreen( screen )->getTree();
    (void)pointer.process( tree, UIWidgetTestUtil::makePointer( sw::UIPointerEventKind::Down, 150.0f, 70.0f ) );
    (void)pointer.process( tree, UIWidgetTestUtil::makePointer( sw::UIPointerEventKind::Up, 150.0f, 70.0f ) );
    SW_ASSERT_EQUAL( 2u, ui.getScreenCount() );
    sw::UIScreen* pPopup = ui.findScreen( pCombo->getPopupScreen() );
    SW_ASSERT_NOT_NULL( pPopup );
    ui.update( 1.0f / 60.0f, viewport );
    sw::Widget* pHigh = pPopup->getTree().findWidgetByName( "option2" );
    SW_ASSERT_NOT_NULL( pHigh );
    SW_EXPECT_NEAR_EQUAL( 100.0f, pHigh->getGeometry()._position._x, 4.0f ); // 상자 아래 · 같은 너비
    SW_EXPECT_TRUE( pHigh->getGeometry()._position._y > 90.0f );

    const sw::float2 center = pHigh->getGeometry().computeScreenBounds().getCenter();
    (void)pointer.process( pPopup->getTree(), UIWidgetTestUtil::makePointer( sw::UIPointerEventKind::Down, center._x, center._y ) );
    (void)pointer.process( pPopup->getTree(), UIWidgetTestUtil::makePointer( sw::UIPointerEventKind::Up, center._x, center._y ) );
    SW_EXPECT_EQUAL( 2u, pCombo->getSelectedIndex() );
    SW_EXPECT_EQUAL( 2u, chosen );
    pointer.forgetTree( pPopup->getTree() );
    ui.update( 1.0f / 60.0f, viewport ); // 지연 닫기
    SW_EXPECT_EQUAL( 1u, ui.getScreenCount() );
}

/**
 * @brief [UIPaintTest] 글 입력 칸: 포커스를 쥐면 키보드 포커스 UI, 글자 사건은 끝에 붙고(조합 글은 확정 전까지 따로), Backspace(UI.TextBackspace)는 끝 코드 포인트 하나를,
 *        Enter 는 확정 알림을 부른다
 * @details 변이: `UISystem::processActions` 의 UI.TextBackspace 경로를 빼면 "가" 가 남지 않아 진다.
 */
SW_TEST_CASE( UIPaintTest, TextInputTypesAndDeletes )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::UISystem ui;
    SW_ASSERT_TRUE( ui.initialize( input, nullptr, "engine/input/ui.input.xml" ) );
    sw::unique_ptr<sw::BoxPanel> root   = sw::make_unique<sw::BoxPanel>();
    sw::TextInputWidget*         pField = static_cast<sw::TextInputWidget*>( root->addChild( sw::make_unique<sw::TextInputWidget>() ) );
    sw::string                   committed;
    (void)pField->getOnCommitted().add( [&committed]( const sw::string& text )
    { committed = text; } );
    const sw::UIScreenHandle screen = ui.pushScreen( sw::make_unique<sw::UIScreen>( sw::UIScreenDesc{}, std::move( root ) ) );
    sw::UIViewport           viewport{};
    viewport._size         = sw::float2{ 800.0f, 600.0f };
    viewport._physicalSize = viewport._size;
    const auto runFrame    = [&]()
    {
        input.beginFrame( 1.0f / 60.0f );
        ui.processInput( 1.0f / 60.0f );
        ui.update( 1.0f / 60.0f, viewport );
        input.endFrame();
    };
    runFrame();
    SW_ASSERT_TRUE( ui.getFocusManager().setFocus( ui.findScreen( screen )->getTree(), pField->getID() ) );
    runFrame();
    SW_EXPECT_TRUE( input.getKeyboardFocus() == sw::InputKeyboardFocus::UI );

    SW_EXPECT_TRUE( input.postRawEvent( sw::RawInputEvent::makeTextComposition( "\xEB\x82\x98" ) ) ); // 조합 중 "나"
    runFrame();
    SW_EXPECT_STREQ( "\xEB\x82\x98", pField->getComposition().c_str() );
    SW_EXPECT_TRUE( pField->getText().empty() );
    SW_EXPECT_TRUE( input.postRawEvent( sw::RawInputEvent::makeTextInput( "\xEA\xB0\x80"
                                                                          "a" ) ) ); // 확정 "가" "a"
    runFrame();
    SW_EXPECT_STREQ( "\xEA\xB0\x80"
                     "a",
                     pField->getText().c_str() );
    SW_EXPECT_TRUE( pField->getComposition().empty() );

    SW_EXPECT_TRUE( input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::Backspace ) ) );
    runFrame();
    SW_EXPECT_STREQ( "\xEA\xB0\x80", pField->getText().c_str() );
    SW_EXPECT_TRUE( input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::Backspace ) ) );
    runFrame();
    SW_EXPECT_TRUE( input.postRawEvent( sw::RawInputEvent::makeTextInput( "\r" ) ) );
    runFrame();
    SW_EXPECT_STREQ( "\xEA\xB0\x80", committed.c_str() );
    ui.shutdown();
    input.shutdown();
}

/**
 * @brief [UIPaintTest] 원하는 너비에 딱 맞게 놓인 글은 픽셀 맞춤이 너비를 1 픽셀 안쪽으로 줄여도 한 줄이다 — 상자 줄의 단추 글이 끝 글자를 다음 줄로 넘기지 않는다
 * @details 배율 2/3 에서 너비 35 의 글은 픽셀 맞춤으로 34.5 에 놓인다. 변이: `TextWidget::paint` 의 맞춤 여유를 빼면 "abcdef" + "g" 두 줄이 된다.
 */
SW_TEST_CASE( UIPaintTest, SnappedWidthDoesNotWrapFittingText )
{
    UIPaintFontFixture fonts;
    SW_ASSERT_TRUE( fonts._bInitialized );
    UIPaintFixture fixture( 600.0f, 300.0f );
    fonts.bind( fixture );
    fixture._layoutContext._uiScale = 2.0f / 3.0f;
    fixture._paintContext._uiScale  = 2.0f / 3.0f;
    sw::BoxPanel*       pRoot       = fixture.setRoot<sw::BoxPanel>();
    sw::TextWidget*     pText       = static_cast<sw::TextWidget*>( pRoot->addChild( sw::make_unique<sw::TextWidget>() ) );
    sw::TextLayoutStyle style{};
    style._fontSize = 10.0f;
    pText->setTextStyle( style );
    pText->setText( "abcdefg" );
    (void)fixture.runFrame();
    SW_EXPECT_NEAR_EQUAL( 35.0f, pText->getDesiredSize()._x, 1e-4f );
    SW_EXPECT_TRUE( pText->getGeometry()._size._x < 35.0f ); // 픽셀 맞춤이 줄였다
    SW_EXPECT_EQUAL( size_t{ 1 }, pText->getLastLayout()._listLine.size() );
}

/**
 * @brief [UIPaintTest] 글 위젯의 행동 태그는 문맥의 글리프 출처로 풀린다 — 입력 장치가 바뀌면(onInputGlyphsChanged) 다시 풀고 다시 잰다
 * @details 변이: `TextWidget::onInputGlyphsChanged` 를 비우면 패드로 바꿔도 "[ E ] open" 이 남아 진다.
 */
SW_TEST_CASE( UIPaintTest, ActionTagFollowsInputDevice )
{
    UIPaintFontFixture fonts;
    SW_ASSERT_TRUE( fonts._bInitialized );
    UIPaintFixture fixture( 400.0f, 100.0f );
    fonts.bind( fixture );
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    input.getInputMap().bind( "Interact", sw::Key::E );
    input.getInputMap().bind( "Interact", sw::GamepadButton::X );
    sw::UIActionGlyphSource glyphs{};
    glyphs._pInput                        = &input;
    fixture._layoutContext._pActionGlyphs = &glyphs;
    fixture._paintContext._pActionGlyphs  = &glyphs;
    sw::OverlayPanel* pRoot               = fixture.setRoot<sw::OverlayPanel>();
    sw::TextWidget*   pText               = static_cast<sw::TextWidget*>( pRoot->addChild( sw::make_unique<sw::TextWidget>() ) );
    pText->setRichText( true );
    pText->setText( "[action=Interact] open" );
    (void)fixture.runFrame();
    SW_EXPECT_STREQ( "[ E ] open", pText->getDisplayText().c_str() );

    input.setActiveGlyphStyle( sw::InputGlyphStyle::GamepadXbox );
    pText->onInputGlyphsChanged(); // UISystem::refreshInputGlyphs 가 장치가 바뀐 프레임에 부른다
    SW_EXPECT_TRUE( ( pText->getDirtyFlags() & sw::WidgetDirty::kLayout ) != 0 );
    (void)fixture.runFrame();
    SW_EXPECT_STREQ( ( input.getInputMap().getGlyphForAction( "Interact", sw::InputGlyphStyle::GamepadXbox ) + " open" ).c_str(), pText->getDisplayText().c_str() );
    input.shutdown();
}

/**
 * @brief [UIPaintTest] 자르는 패널 밖에 통째로 있는 자식은 걷지도 칠하지도 않고, 밖에 있는 동안 배율이 바뀌었으면 다시 보일 때 새 배율로 다시 칠한다
 * @details 위젯 1 만 칸 스크롤 목록의 그리기가 보이는 칸만큼만 들게 하는 컬링(Slate 의 자식 컬링). 변이: `UIPaintPass::paintChild` 가 컬링 때 비트를 남기지 않으면
 *          배율을 바꾼 뒤 다시 보인 상자가 옛 배율 캐시로 남아(칠한 수 1 · 높이 10) 진다. 컬링을 끄면 밖의 상자도 칠해 진다.
 */
SW_TEST_CASE( UIPaintTest, ClippedOutChildrenAreNotPainted )
{
    UIPaintFixture   fixture( 400.0f, 400.0f );
    sw::CanvasPanel* pRoot = fixture.setRoot<sw::CanvasPanel>();
    sw::BoxPanel*    pList = static_cast<sw::BoxPanel*>( pRoot->addChild( sw::make_unique<sw::BoxPanel>() ) );
    pList->setOrientation( sw::UIOrientation::Vertical );
    pList->setClipChildren( true );
    UIWidgetTestUtil::pin( *pList, 0.0f, 0.0f, 100.0f, 200.0f ); // 처음은 모두 보인다(상자 높이 10 × 10)
    sw::vector<TestPaintWidget*> listBox;
    for ( uint32 index = 0; index < 10; ++index )
    {
        listBox.push_back( UIPaintFixture::addBox( *pList, sw::hashed_string( "box" + sw::to_string( index ) ), sw::float4{ 0.0f, 0.0f, 1.0f, 1.0f } ) );
    }
    (void)fixture.runFrame();
    SW_EXPECT_EQUAL( 1u, listBox[9]->getPaintCount() );
    SW_EXPECT_EQUAL( size_t{ 10 }, fixture._canvas._listQuad.size() );

    UIWidgetTestUtil::pin( *pList, 0.0f, 0.0f, 100.0f, 50.0f ); // 다섯만 보인다
    (void)fixture.runFrame();
    SW_EXPECT_EQUAL( size_t{ 5 }, fixture._canvas._listQuad.size() );

    fixture._paintContext._uiScale = 2.0f; // 보이는 다섯은 다시 칠하고, 밖의 다섯은 이 강제 칠하기를 비트로 받는다
    (void)fixture.runFrame();
    SW_EXPECT_EQUAL( 2u, listBox[0]->getPaintCount() );
    SW_EXPECT_EQUAL( 1u, listBox[9]->getPaintCount() );

    UIWidgetTestUtil::pin( *pList, 0.0f, 0.0f, 100.0f, 200.0f ); // 다시 모두 보인다
    (void)fixture.runFrame();
    SW_EXPECT_EQUAL( 2u, listBox[9]->getPaintCount() );
    SW_ASSERT_EQUAL( size_t{ 10 }, fixture._canvas._listQuad.size() );
    SW_EXPECT_NEAR_EQUAL( 20.0f, fixture._canvas._listQuad[9]._rect._w, 1e-3f ); // 새 배율(높이 10 × 2)
}

/**
 * @brief [UIPaintTest] 바뀐 것이 없는 트리는 걷지 않고 지난 목록을 내고, 자식을 떼기만 해도(다른 위젯의 기하가 그대로여도) 그 그림이 목록에서 빠진다
 * @details 트리 출력 재사용의 조건은 "지난 걷기 뒤 무효화가 하나도 없음" 이다. 변이: `WidgetTree::notifyDirty` 가 출력을 낡음으로 적지 않으면 뗀 상자의 사각형이 남아 진다.
 */
SW_TEST_CASE( UIPaintTest, UnchangedTreeReusesOutputAndRemovalClearsIt )
{
    UIPaintFixture   fixture( 400.0f, 400.0f );
    sw::CanvasPanel* pRoot  = fixture.setRoot<sw::CanvasPanel>();
    TestPaintWidget* pFirst = UIPaintFixture::addBox( *pRoot, sw::hashed_string( "first" ), sw::float4{ 1.0f, 0.0f, 0.0f, 1.0f } );
    TestPaintWidget* pLast  = UIPaintFixture::addBox( *pRoot, sw::hashed_string( "last" ), sw::float4{ 0.0f, 1.0f, 0.0f, 1.0f } );
    UIWidgetTestUtil::pin( *pFirst, 0.0f, 0.0f, 20.0f, 10.0f );
    UIWidgetTestUtil::pin( *pLast, 100.0f, 0.0f, 20.0f, 10.0f );
    (void)fixture.runFrame();
    SW_EXPECT_EQUAL( size_t{ 2 }, fixture._canvas._listQuad.size() );
    SW_EXPECT_EQUAL( 0u, fixture.runFrame() ); // 바뀐 것 없음 — 같은 목록
    SW_EXPECT_EQUAL( size_t{ 2 }, fixture._canvas._listQuad.size() );

    (void)pRoot->removeChild( pLast ); // 캔버스 패널의 자리는 그대로 — 레이아웃만 무효화된다
    (void)fixture.runFrame();
    SW_ASSERT_EQUAL( size_t{ 1 }, fixture._canvas._listQuad.size() );
    SW_EXPECT_NEAR_EQUAL( 1.0f, fixture._canvas._listQuad[0]._color._x, 1e-4f );
}
