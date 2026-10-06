#include "pch.h"

#include "Engine/UI/Debug/UiDemoScreen.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UI/Widgets/BorderPanel.h"
#include "Engine/UI/Widgets/ButtonWidget.h"
#include "Engine/UI/Widgets/CheckBoxWidget.h"
#include "Engine/UI/Widgets/ComboBoxWidget.h"
#include "Engine/UI/Widgets/SliderWidget.h"
#include "Engine/UI/Widgets/TextInputWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

SW_TEST_GLOBAL_VARIABLE( bool, gv_uiDemo, false, "UI 시험 화면을 띄운다 — 글 · 버튼 다섯 · 슬라이더 · 체크 · 진행 · 콤보 · 입력 칸(코드로 지은 위젯 트리)" );

namespace sw
{
    namespace
    {
        struct UiDemoScreenInternal
        {
            /** @brief 패널 크기(UI 단위)입니다. */
            static constexpr float32 kPanelWidth  = 560.0f;
            static constexpr float32 kPanelHeight = 640.0f;

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

            static void addLabeled( BoxPanel& column, string_view label, unique_ptr<Widget> control )
            {
                unique_ptr<BoxPanel> row = make_unique<BoxPanel>();
                row->setSpacing( 12.0f );
                unique_ptr<TextWidget> text     = makeText( label, 18.0f, float4{ 0.8f, 0.83f, 0.9f, 1.0f } );
                WidgetLayoutSlot       textSlot = text->getLayoutSlot();
                textSlot._widthOverride         = 140.0f;
                textSlot._verticalAlignment     = UiAlignment::Center;
                text->setLayoutSlot( textSlot );
                (void)row->addChild( std::move( text ) );
                WidgetLayoutSlot controlSlot   = control->getLayoutSlot();
                controlSlot._sizeRule          = UiSizeRule::Fill;
                controlSlot._verticalAlignment = UiAlignment::Center;
                control->setLayoutSlot( controlSlot );
                (void)row->addChild( std::move( control ) );
                (void)column.addChild( std::move( row ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    unique_ptr<UiScreen> UiDemoScreen::create()
    {
        using Internal                = UiDemoScreenInternal;
        unique_ptr<CanvasPanel> root  = make_unique<CanvasPanel>();
        unique_ptr<BorderPanel> panel = make_unique<BorderPanel>();
        UiBrush                 brush = UiBrush::makeSolid( float4{ 0.07f, 0.08f, 0.11f, 0.92f }, 16.0f );
        brush._borderColor            = float4{ 0.95f, 0.75f, 0.2f, 1.0f };
        brush._borderWidth            = 2.0f;
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
        column->setOrientation( UiOrientation::Vertical );
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
            Internal::setHeight( *button, 44.0f );
            unique_ptr<TextWidget> label     = Internal::makeText( pLabel, 20.0f, float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
            WidgetLayoutSlot       labelSlot = label->getLayoutSlot();
            labelSlot._verticalAlignment     = UiAlignment::Center;
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

        (void)panel->addChild( std::move( column ) );
        (void)root->addChild( std::move( panel ) );

        UiScreenDesc desc{};
        desc._layer        = UiLayer::Menu;
        desc._defaultFocus = hashed_string( "Start" );
        return sw::make_unique<UiScreen>( desc, std::move( root ) );
    }
} // namespace sw
