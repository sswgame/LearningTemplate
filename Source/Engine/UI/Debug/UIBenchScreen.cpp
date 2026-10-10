#include "pch.h"

#include "Engine/UI/Debug/UIBenchScreen.h"

#include "Core/Container/string.h"

#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Layout/ScrollPanel.h"
#include "Engine/UI/Layout/WrapPanel.h"
#include "Engine/UI/Widgets/BorderPanel.h"
#include "Engine/UI/Widgets/ImageWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_benchUiWidgets, 0, "UI 벤치 — 격자 셀 수(셀마다 테두리 · 아이콘 · 글, 대부분 스크롤 밖) (0=사용 안 함)" );
SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_benchUiChurn, 0, "UI 벤치 — 프레임마다 글을 바꾸는 셀 수(앞쪽 보이는 셀 안에서 돈다)" );

namespace sw
{
    namespace
    {
        struct UIBenchScreenInternal
        {
            static void setFixedSize( Widget& widget, float32 width, float32 height )
            {
                WidgetLayoutSlot slot   = widget.getLayoutSlot();
                slot._widthOverride     = width;
                slot._heightOverride    = height;
                slot._verticalAlignment = UIAlignment::Center;
                widget.setLayoutSlot( slot );
            }

            static unique_ptr<BorderPanel> makeCell( uint32 index, WidgetId& outTextId )
            {
                unique_ptr<BorderPanel> cell = make_unique<BorderPanel>();
                cell->setBackground( UIBrush::makeSolid( float4{ 0.12f + 0.02f * static_cast<float32>( index % 4 ), 0.14f, 0.2f, 0.9f }, 6.0f ) );
                cell->setContentPadding( float4{ 6.0f, 4.0f, 6.0f, 4.0f } );
                setFixedSize( *cell, UIBenchScreen::kCellWidth, UIBenchScreen::kCellHeight );
                unique_ptr<BoxPanel> row = make_unique<BoxPanel>();
                row->setSpacing( 6.0f );
                unique_ptr<ImageWidget> icon = make_unique<ImageWidget>();
                icon->setBrush( UIBrush::makeSolid( float4{ 0.3f + 0.1f * static_cast<float32>( index % 5 ), 0.6f, 0.9f, 1.0f }, 4.0f ) );
                setFixedSize( *icon, 24.0f, 24.0f );
                (void)row->addChild( std::move( icon ) );
                unique_ptr<TextWidget> text = make_unique<TextWidget>();
                text->setText( "Cell " + to_string( index ) );
                setFixedSize( *text, UIBenchScreen::kCellWidth - 48.0f, 24.0f );
                outTextId = text->getId();
                (void)row->addChild( std::move( text ) );
                (void)cell->addChild( std::move( row ) );
                return cell;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    unique_ptr<UIBenchScreen> UIBenchScreen::create( uint32 cellCount )
    {
        using Internal                 = UIBenchScreenInternal;
        unique_ptr<CanvasPanel> root   = make_unique<CanvasPanel>();
        unique_ptr<ScrollPanel> scroll = make_unique<ScrollPanel>();
        WidgetLayoutSlot        slot   = scroll->getLayoutSlot();
        slot._anchorMin                = float2{ 0.0f, 0.0f };
        slot._anchorMax                = float2{ 1.0f, 1.0f };
        slot._offsetMin                = float2{ 16.0f, 16.0f };
        slot._offsetMax                = float2{ -16.0f, -16.0f };
        scroll->setLayoutSlot( slot );
        scroll->setScrollAxes( false, true );
        unique_ptr<WrapPanel> grid = make_unique<WrapPanel>();
        grid->setItemSpacing( 4.0f );
        grid->setLineSpacing( 4.0f );
        vector<WidgetId> listCellText;
        listCellText.reserve( cellCount );
        for ( uint32 index = 0; index < cellCount; ++index )
        {
            WidgetId textId = kInvalidWidgetId;
            (void)grid->addChild( Internal::makeCell( index, textId ) );
            listCellText.push_back( textId );
        }
        (void)scroll->addChild( std::move( grid ) );
        (void)root->addChild( std::move( scroll ) );
        UIScreenDesc desc{};
        desc._layer       = UILayer::HUD;
        desc._bTakesFocus = false;
        desc._bShowCursor = false;
        return sw::make_unique<UIBenchScreen>( desc, std::move( root ), std::move( listCellText ) );
    }

    UIBenchScreen::UIBenchScreen( const UIScreenDesc& desc, unique_ptr<Widget> root, vector<WidgetId> listCellText )
        : UIScreen{ desc, std::move( root ) }
        , _listCellText{ std::move( listCellText ) }
        , _churnCursor{ 0 }
        , _frameIndex{ 0 }
    {
    }

    void UIBenchScreen::onTick( float32 deltaSeconds )
    {
        (void)deltaSeconds;
        ++_frameIndex;
        const uint32 window = _listCellText.size() < kChurnWindow ? static_cast<uint32>( _listCellText.size() ) : kChurnWindow;
        const uint32 churn  = gv_benchUiChurn > 0 ? static_cast<uint32>( gv_benchUiChurn ) : 0u;
        if ( window == 0 || churn == 0 )
            return;
        WidgetTree& tree = getTree();
        for ( uint32 count = 0; count < churn; ++count )
        {
            const uint32 cell = _churnCursor;
            _churnCursor      = ( _churnCursor + 1 ) % window;
            if ( TextWidget* pText = castTo<TextWidget>( tree.findWidgetById( _listCellText[cell] ) ); pText != nullptr )
                pText->setText( "Cell " + to_string( cell ) + " #" + to_string( _frameIndex ) );
        }
    }
} // namespace sw
