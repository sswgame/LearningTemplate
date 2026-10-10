#include "pch.h"

#include "Engine/UI/Debug/UIDemoScreen.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/Widgets/BorderPanel.h"
#include "Engine/UI/Widgets/ButtonWidget.h"
#include "Engine/UI/Widgets/CheckBoxWidget.h"
#include "Engine/UI/Widgets/ComboBoxWidget.h"
#include "Engine/UI/Widgets/ImageWidget.h"
#include "Engine/UI/Widgets/SliderWidget.h"
#include "Engine/UI/Widgets/TextInputWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

SW_TEST_GLOBAL_VARIABLE_SHIPPED( bool, gv_uiDemo, false,
                                 "UI 시험 화면을 띄운다 — 글 · 버튼 다섯 · 슬라이더 · 체크 · 진행 · 콤보 · 입력 필드 · 그리기 견본(둥근 상자 · 자르기 · 9-슬라이스 · 오른쪽에서 왼쪽 글)" );

namespace sw
{
    namespace
    {
        struct UIDemoScreenInternal
        {
            /** @brief 패널 크기(UI 단위)입니다. */
            static constexpr float32 kPanelWidth  = 560.0f;
            static constexpr float32 kPanelHeight = 720.0f;
            /** @brief 그리기 견본 칸 하나의 크기(UI 단위)입니다. */
            static constexpr float32 kSampleWidth  = 96.0f;
            static constexpr float32 kSampleHeight = 48.0f;

            static unique_ptr<TextWidget> makeText( string_view text, float32 fontSize, const float4& color, bool bRichText = false )
            {
                unique_ptr<TextWidget> widget = make_unique<TextWidget>();
                TextLayoutStyle        style  = widget->getTextStyle();
                style._fontSize               = fontSize;
                widget->setTextStyle( style );
                widget->setColor( color );
                widget->setRichText( bRichText );
                widget->setText( text );
                return widget;
            }

            static void setHeight( Widget& widget, float32 height )
            {
                WidgetLayoutSlot slot = widget.getLayoutSlot();
                slot._heightOverride  = height;
                widget.setLayoutSlot( slot );
            }

            static void setSampleSize( Widget& widget )
            {
                WidgetLayoutSlot slot   = widget.getLayoutSlot();
                slot._widthOverride     = kSampleWidth;
                slot._heightOverride    = kSampleHeight;
                slot._verticalAlignment = UIAlignment::Center;
                widget.setLayoutSlot( slot );
            }

            /**
             * @brief 그리기 견본 줄 — 둥근 상자(`RoundBox`) · 자르기(`ClipBox` — 넓은 빨간 자식을 자른다) · 9-슬라이스(`NineSlice`) · 오른쪽에서 왼쪽 글(`RtlSample`).
             * @details `AppUITest` 가 레이아웃 덤프의 이름으로 사각형을 찾아 픽셀을 본다 — 이름을 바꾸면 그 시험도 바꾼다.
             */
            static unique_ptr<BoxPanel> makeSamples()
            {
                unique_ptr<BoxPanel> row = make_unique<BoxPanel>();
                row->setName( hashed_string( "Samples" ) );
                row->setSpacing( 12.0f );
                unique_ptr<BorderPanel> round = make_unique<BorderPanel>();
                round->setName( hashed_string( "RoundBox" ) );
                round->setBackground( UIBrush::makeSolid( float4{ 0.2f, 0.75f, 0.3f, 1.0f }, 20.0f ) );
                setSampleSize( *round );
                (void)row->addChild( std::move( round ) );

                unique_ptr<CanvasPanel> clip = make_unique<CanvasPanel>();
                clip->setName( hashed_string( "ClipBox" ) );
                clip->setClipChildren( true );
                setSampleSize( *clip );
                unique_ptr<BorderPanel> overflow = make_unique<BorderPanel>();
                overflow->setName( hashed_string( "ClipOverflow" ) );
                overflow->setBackground( UIBrush::makeSolid( float4{ 0.9f, 0.1f, 0.1f, 1.0f } ) );
                WidgetLayoutSlot overflowSlot = overflow->getLayoutSlot();
                overflowSlot._offsetMin       = float2{ 0.0f, 0.0f };
                overflowSlot._offsetMax       = float2{ kSampleWidth * 4.0f, kSampleHeight }; // 상자 밖으로 세 칸 더 — 잘려야 한다
                overflow->setLayoutSlot( overflowSlot );
                (void)clip->addChild( std::move( overflow ) );
                (void)row->addChild( std::move( clip ) );

                unique_ptr<ImageWidget> nineSlice = make_unique<ImageWidget>();
                nineSlice->setName( hashed_string( "NineSlice" ) );
                nineSlice->setImagePath( "engine/textures/test/checker.dds" );
                UIBrush nineSliceBrush          = nineSlice->getBrush();
                nineSliceBrush._nineSliceMargin = float4{ 0.25f, 0.25f, 0.25f, 0.25f };
                nineSlice->setBrush( nineSliceBrush );
                setSampleSize( *nineSlice );
                (void)row->addChild( std::move( nineSlice ) );

                unique_ptr<TextWidget> rtl = makeText( "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D \xD7\xA2\xD7\x95\xD7\x9C\xD7\x9D", 20.0f, float4{ 1.0f, 1.0f, 1.0f, 1.0f } ); // "שלום עולם"
                rtl->setName( hashed_string( "RtlSample" ) );
                WidgetLayoutSlot rtlSlot   = rtl->getLayoutSlot();
                rtlSlot._verticalAlignment = UIAlignment::Center;
                rtl->setLayoutSlot( rtlSlot );
                (void)row->addChild( std::move( rtl ) );
                return row;
            }

            static void addLabeled( BoxPanel& column, string_view label, unique_ptr<Widget> control )
            {
                unique_ptr<BoxPanel> row = make_unique<BoxPanel>();
                row->setSpacing( 12.0f );
                unique_ptr<TextWidget> text     = makeText( label, 18.0f, float4{ 0.8f, 0.83f, 0.9f, 1.0f } );
                WidgetLayoutSlot       textSlot = text->getLayoutSlot();
                textSlot._widthOverride         = 140.0f;
                textSlot._verticalAlignment     = UIAlignment::Center;
                text->setLayoutSlot( textSlot );
                (void)row->addChild( std::move( text ) );
                WidgetLayoutSlot controlSlot   = control->getLayoutSlot();
                controlSlot._sizeRule          = UISizeRule::Fill;
                controlSlot._verticalAlignment = UIAlignment::Center;
                control->setLayoutSlot( controlSlot );
                (void)row->addChild( std::move( control ) );
                (void)column.addChild( std::move( row ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    unique_ptr<UIScreen> UIDemoScreen::create()
    {
        using Internal                = UIDemoScreenInternal;
        unique_ptr<CanvasPanel> root  = make_unique<CanvasPanel>();
        unique_ptr<BorderPanel> panel = make_unique<BorderPanel>();
        panel->setName( hashed_string( "DemoPanel" ) );
        UIBrush brush      = UIBrush::makeSolid( float4{ 0.07f, 0.08f, 0.11f, 0.92f }, 16.0f );
        brush._borderColor = float4{ 0.95f, 0.75f, 0.2f, 1.0f };
        brush._borderWidth = 2.0f;
        panel->setBackground( brush );
        panel->setShadow( float4{ 0.0f, 0.0f, 0.0f, 0.6f }, 12.0f, float2{ 6.0f, 8.0f } );
        panel->setContentPadding( float4{ 28.0f, 24.0f, 28.0f, 24.0f } );
        WidgetLayoutSlot panelSlot = panel->getLayoutSlot();
        panelSlot._anchorMin       = float2{ 0.5f, 0.5f };
        panelSlot._anchorMax       = float2{ 0.5f, 0.5f };
        panelSlot._offsetMin       = float2{ -Internal::kPanelWidth * 0.5f, -Internal::kPanelHeight * 0.5f };
        panelSlot._offsetMax       = float2{ Internal::kPanelWidth * 0.5f, Internal::kPanelHeight * 0.5f };
        panel->setLayoutSlot( panelSlot );

        unique_ptr<BoxPanel> column = make_unique<BoxPanel>();
        column->setOrientation( UIOrientation::Vertical );
        column->setSpacing( 10.0f );
        (void)column->addChild( Internal::makeText( "UI Demo", 36.0f, float4{ 1.0f, 0.85f, 0.3f, 1.0f } ) );
        (void)column->addChild( Internal::makeText( "Retained widget tree: layout, paint cache, focus and navigation. Arrow keys or the gamepad move the focus.",
                                                    18.0f, float4{ 0.85f, 0.87f, 0.92f, 1.0f } ) );
        (void)column->addChild( Internal::makeText( "[b]Bold[/b] [i]italic[/i] [color=#ffcc33ff]colored[/color] [size=1.4]bigger[/size]", 20.0f,
                                                    float4{ 1.0f, 1.0f, 1.0f, 1.0f }, true ) );

        const utf8* const arrButton[] = { "Start", "Continue", "Options", "Credits", "Quit" };
        for ( const utf8* pLabel : arrButton )
        {
            unique_ptr<ButtonWidget> button = make_unique<ButtonWidget>();
            button->setName( hashed_string( pLabel ) );
            if ( pLabel == arrButton[0] )
                button->setStyleClass( "primary" ); // 주 단추 — 테마의 강조색(AppUITest 가 가운데 색을 본다)
            Internal::setHeight( *button, 44.0f );
            unique_ptr<TextWidget> label     = Internal::makeText( pLabel, 20.0f, float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
            WidgetLayoutSlot       labelSlot = label->getLayoutSlot();
            labelSlot._verticalAlignment     = UIAlignment::Center;
            label->setLayoutSlot( labelSlot );
            (void)button->addChild( std::move( label ) );
            (void)column->addChild( std::move( button ) );
        }

        unique_ptr<SliderWidget> slider = make_unique<SliderWidget>();
        slider->setRange( 0.0f, 100.0f, 5.0f );
        slider->setValue( 65.0f );
        Internal::addLabeled( *column, "Volume", std::move( slider ) );
        unique_ptr<CheckBoxWidget> check = make_unique<CheckBoxWidget>();
        check->setChecked( true );
        (void)check->addChild( Internal::makeText( "V-Sync", 18.0f, float4{ 1.0f, 1.0f, 1.0f, 1.0f } ) );
        Internal::addLabeled( *column, "Display", std::move( check ) );
        unique_ptr<ProgressBarWidget> progress = make_unique<ProgressBarWidget>();
        progress->setPercent( 0.6f );
        Internal::addLabeled( *column, "Loading", std::move( progress ) );
        unique_ptr<ComboBoxWidget> combo = make_unique<ComboBoxWidget>();
        combo->setOptions( vector<string>{ "Low", "Medium", "High", "Epic" } );
        combo->setSelectedIndex( 2 );
        Internal::addLabeled( *column, "Quality", std::move( combo ) );
        unique_ptr<TextInputWidget> input = make_unique<TextInputWidget>();
        input->setHintText( "Player name" );
        Internal::addLabeled( *column, "Name", std::move( input ) );
        (void)column->addChild( Internal::makeSamples() );

        (void)panel->addChild( std::move( column ) );
        (void)root->addChild( std::move( panel ) );

        UIScreenDesc desc{};
        desc._layer        = UILayer::Menu;
        desc._defaultFocus = hashed_string( "Start" );
        return sw::make_unique<UIScreen>( desc, std::move( root ) );
    }
} // namespace sw
