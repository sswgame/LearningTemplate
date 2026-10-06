#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Text/FontSystem.h"
#include "Engine/Text/GlyphCache.h"
#include "Engine/Text/TextLayout.h"
#include "Engine/UI/Core/PanelWidget.h"
#include "Engine/UI/Core/UiFocusManager.h"
#include "Engine/UI/Core/WidgetTree.h"
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Layout/OverlayPanel.h"
#include "Engine/UI/Layout/UiLayoutPass.h"
#include "Engine/UI/Render/UiPaintPass.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UI/UiSystem.h"
#include "Engine/UI/Widgets/BorderPanel.h"
#include "Engine/UI/Widgets/ImageWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "EngineTest/Text/FakeFontRasterizer.h"

#include "TestFramework/TestFramework.h"

// UiPaintTest — 위젯 그리기: 그림 캐시(더러운 위젯만 다시 칠하기) · 조상 불투명도 번짐 · 아틀라스 세대 · 글 위젯 무효화 · 그리기 순서(z 순서) ·
// UiSystem 의 그리기 목록 내용 번호 · 포커스 테두리. 가짜 래스터라이저(nogpu).

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
        sw::float2 computeDesiredSize( const sw::UiLayoutContext& context, const sw::float2& availableSize ) const override
        {
            (void)context;
            (void)availableSize;
            return _size;
        }

        void paint( sw::CanvasPainter& painter, const sw::UiPaintContext& context ) const override
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
    struct UiPaintFixture
    {
        sw::WidgetTree      _tree;
        sw::UiLayoutContext _layoutContext;
        sw::UiPaintContext  _paintContext;
        sw::CanvasDrawList  _canvas;

        UiPaintFixture( float32 width, float32 height )
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
            (void)sw::UiLayoutPass::update( _tree, _layoutContext );
            _canvas.clear();
            _canvas._targetSize = _layoutContext._viewportSize;
            sw::CanvasPainter painter( _canvas, _paintContext._uiScale );
            return sw::UiPaintPass::paint( _tree, _paintContext, painter, _canvas );
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
    struct UiPaintFontFixture
    {
        sw::test::FakeFontSystemFixture      _fonts;
        sw::unique_ptr<sw::TextLayoutEngine> _layout;
        bool                                 _bInitialized;

        UiPaintFontFixture()
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

        void bind( UiPaintFixture& fixture )
        {
            fixture._layoutContext._pTextLayout = _layout.get();
            fixture._paintContext._pTextLayout  = _layout.get();
            fixture._paintContext._pGlyphCache  = &_fonts._fontSystem->getGlyphCache();
        }
    };
} // namespace

/**
 * @brief [UiPaintTest] 위젯 100 개 중 하나의 색만 바꾸면 그 하나만 다시 칠하고(paint 호출 1), 프레임 목록의 사각형 수는 그대로다
 * @details 변이: `UiPaintPass::paintWidget` 의 더러움 판정을 늘 참으로 두면 101 을 다시 칠해 진다.
 */
SW_TEST_CASE( UiPaintTest, OnlyDirtyWidgetsRepaint )
{
    UiPaintFixture fixture( 400.0f, 2000.0f );
    sw::BoxPanel*  pColumn = fixture.setRoot<sw::BoxPanel>();
    pColumn->setOrientation( sw::UiOrientation::Vertical );
    sw::vector<TestPaintWidget*> listBox;
    for ( uint32 index = 0; index < 100; ++index )
        listBox.push_back( UiPaintFixture::addBox( *pColumn, sw::hashed_string( "box" + sw::to_string( index ) ), sw::float4{ 1.0f, 0.0f, 0.0f, 1.0f } ) );
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
 * @brief [UiPaintTest] 부모 불투명도 0.5 는 자식 사각형 알파를 0.5 배로 만들고, 부모 불투명도만 바꿔도 자식 캐시가 다시 칠해진다
 * @details 변이: 자손 강제 다시 칠하기(`kSubtreePaintBits`)를 빼면 자식 알파가 0.5 로 남아 진다.
 */
SW_TEST_CASE( UiPaintTest, ParentOpacityPropagatesToChildren )
{
    UiPaintFixture    fixture( 200.0f, 100.0f );
    sw::OverlayPanel* pRoot   = fixture.setRoot<sw::OverlayPanel>();
    sw::BoxPanel*     pParent = static_cast<sw::BoxPanel*>( pRoot->addChild( sw::make_unique<sw::BoxPanel>() ) );
    TestPaintWidget*  pChild  = UiPaintFixture::addBox( *pParent, "child", sw::float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
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
 * @brief [UiPaintTest] 글리프 아틀라스 세대가 오르면(페이지를 비웠다) 글 위젯만 다시 칠한다 — 상자 위젯은 캐시 그대로
 * @details 변이: `paintWidget` 의 아틀라스 세대 조건을 빼면 글 위젯이 옛 아틀라스 사각형을 든 채 남아 다시 칠한 수가 0 이다.
 */
SW_TEST_CASE( UiPaintTest, AtlasEvictionRepaintsText )
{
    UiPaintFontFixture fonts;
    SW_ASSERT_TRUE( fonts._bInitialized );
    UiPaintFixture fixture( 400.0f, 100.0f );
    fonts.bind( fixture );
    sw::BoxPanel*   pRow  = fixture.setRoot<sw::BoxPanel>();
    sw::TextWidget* pText = static_cast<sw::TextWidget*>( pRow->addChild( sw::make_unique<sw::TextWidget>() ) );
    pText->setText( "abc" );
    TestPaintWidget* pBox = UiPaintFixture::addBox( *pRow, "box", sw::float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
    (void)fixture.runFrame();
    SW_EXPECT_EQUAL( size_t{ 4 }, fixture._canvas._listQuad.size() ); // 글리프 셋 + 상자
    SW_EXPECT_EQUAL( 0u, fixture.runFrame() );

    fixture._paintContext._atlasGeneration += 1;
    SW_EXPECT_EQUAL( 1u, fixture.runFrame() );
    SW_EXPECT_EQUAL( 1u, pBox->getPaintCount() );
    SW_EXPECT_EQUAL( size_t{ 4 }, fixture._canvas._listQuad.size() );
}

/**
 * @brief [UiPaintTest] 글 위젯: 글을 바꾸면 레이아웃 더러움(kLayout), 색만 바꾸면 그리기 더러움(kPaint)뿐이다 — 원하는 크기는 글자 배율을 따른다
 * @details 변이: `TextWidget::setColor` 가 kLayout 을 함께 걸면 진다.
 */
SW_TEST_CASE( UiPaintTest, TextWidgetLayoutVsPaintInvalidation )
{
    UiPaintFontFixture fonts;
    SW_ASSERT_TRUE( fonts._bInitialized );
    UiPaintFixture fixture( 400.0f, 100.0f );
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
 * @brief [UiPaintTest] 사각형은 그리기 순서로 쌓인다 — 형제는 자식 순서, 캔버스 패널은 z 순서(같으면 자식 순서), 자식은 부모 위
 * @details 변이: 그리기 걷기가 `collectPaintOrder` 를 무시하면 red · green · blue 순서가 되어 진다.
 */
SW_TEST_CASE( UiPaintTest, CanvasOrderFollowsTreeOrder )
{
    UiPaintFixture   fixture( 400.0f, 100.0f );
    sw::CanvasPanel* pCanvas = fixture.setRoot<sw::CanvasPanel>();
    TestPaintWidget* pRed    = UiPaintFixture::addBox( *pCanvas, "red", sw::float4{ 1.0f, 0.0f, 0.0f, 1.0f } );
    UiPaintFixture::addBox( *pCanvas, "green", sw::float4{ 0.0f, 1.0f, 0.0f, 1.0f } );
    TestPaintWidget*     pBlue = UiPaintFixture::addBox( *pCanvas, "blue", sw::float4{ 0.0f, 0.0f, 1.0f, 1.0f } );
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
 * @brief [UiPaintTest] 테두리 패널은 배경 · 그림자를 칠하고 자식을 안쪽 여백만큼 들인다 · 이미지 위젯은 RTL 에서 `_bMirrorInRtl` 이면 UV 를 좌우로 뒤집는다
 * @details 변이: `ImageWidget::paint` 의 거울 분기를 빼면 u0 < u1 로 남아 진다.
 */
SW_TEST_CASE( UiPaintTest, BorderPaddingAndImageMirror )
{
    UiPaintFixture   fixture( 200.0f, 100.0f );
    sw::BorderPanel* pBorder = fixture.setRoot<sw::BorderPanel>();
    pBorder->setBackground( sw::UiBrush::makeSolid( sw::float4{ 0.1f, 0.1f, 0.1f, 1.0f }, 4.0f ) );
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
 * @brief [UiPaintTest] UiSystem 의 그리기 목록 내용 번호는 내용이 바뀔 때만 오르고, 탐색 입력 방식이면 포커스 위젯 둘레에 테두리 사각형이 하나 더 든다
 * @details 변이: `UiSystem::paintScreens` 의 같은 내용 확인을 빼면 바뀐 것이 없는 프레임에도 번호가 올라 진다.
 */
SW_TEST_CASE( UiPaintTest, UiSystemCanvasRevisionAndFocusRing )
{
    sw::UiSystem                     ui;
    sw::unique_ptr<sw::OverlayPanel> root = sw::make_unique<sw::OverlayPanel>();
    TestPaintWidget*                 pBox = UiPaintFixture::addBox( *root, "box", sw::float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
    pBox->setFocusable( true );
    const sw::UiScreenHandle screen = ui.pushScreen( sw::make_unique<sw::UiScreen>( sw::UiScreenDesc{}, std::move( root ) ) );
    sw::UiViewport           viewport{};
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

    SW_ASSERT_TRUE( ui.getFocusManager().setFocus( ui.findScreen( screen )->getTree(), pBox->getId() ) );
    ui.update( 1.0f / 60.0f, viewport );
    SW_EXPECT_EQUAL( size_t{ 1 }, ui.getCanvas()._listQuad.size() ); // 포인터 방식 — 테두리 없음
    ui.setInputMode( sw::UiInputMode::Navigation );
    ui.update( 1.0f / 60.0f, viewport );
    SW_ASSERT_EQUAL( size_t{ 2 }, ui.getCanvas()._listQuad.size() );
    SW_EXPECT_TRUE( ui.getCanvas()._listQuad[1]._params._x > 0.0f ); // 테두리 두께
    ui.getFocusManager().clearFocus();
}
