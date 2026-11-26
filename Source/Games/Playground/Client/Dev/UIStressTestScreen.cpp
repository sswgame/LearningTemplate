#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"

#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Layout/ScrollPanel.h"
#include "Engine/UI/Layout/WrapPanel.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/Screen/UIScreenSwitch.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"
#include "Engine/UI/WidgetTree/WidgetTypes.h"
#include "Engine/UI/Widgets/BorderPanel.h"
#include "Engine/UI/Widgets/ImageWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

SW_TEST_GLOBAL_VARIABLE( int32, gv_stressTestUiWidgets, 0, "UI 벤치 — 격자 셀 수(셀마다 테두리 · 아이콘 · 글, 대부분 스크롤 밖) (0=사용 안 함)" );
SW_TEST_GLOBAL_VARIABLE( int32, gv_stressTestUiChurn, 0, "UI 벤치 — 프레임마다 글을 바꾸는 셀 수(앞쪽 보이는 셀 안에서 돈다)" );
SW_TEST_GLOBAL_VARIABLE( bool, gv_stressTestUiOpacityPulse, false, "UI 벤치 — 프레임마다 화면 루트 불투명도를 바꿔 보이는 자손 전체를 다시 칠하게 한다 (조상 kTransform 비용 측정)" );

namespace sw
{
    /**
     * @class UIStressTestScreen
     * @brief 스크롤 패널 안에 줄 바꿈 격자를 두는 벤치 화면입니다. 칸마다 테두리(바탕), 아이콘 상자, 텍스트를 놓고 크기를 고정합니다(칸이 레이아웃 경계). 대부분은 스크롤 밖이라 잘립니다.
     * @details `onTick` 이 프레임마다 앞쪽(보이는) 칸 `gv_stressTestUiChurn` 개의 텍스트를 바꾼다. 무효화와 그리기 캐시가 비용을 그 칸으로 가두는지 측정한다.
     *          측정할 구간은 `GT.UI.*`, `RT.Canvas.*` 이고, 카운터는 `UI.LayoutWidgets`, `UI.PaintWidgets`, `Text.GlyphsRasterized` 다(값은 per_frame 열에 있다).
     *          `gv_stressTestUiOpacityPulse` 는 프레임마다 화면 루트의 불투명도를 바꿔 조상의 `kTransform` 으로 보이는 자손 전체를 다시 칠하게 한다(그림 캐시를 캐시 밖에서 곱하는 구조가
     *          이기는지 판단하는 측정용이다).
     *          .cpp 의 화면 스위치(`SW_UI_SCREEN_SWITCH`)가 `gv_stressTestUiWidgets` 를 보고 열고 닫는다(HUD 레이어이며 포커스와 입력을 받지 않는다).
     */
    REFLECT( Abstract )
    class UIStressTestScreen final : public UIScreen
    {
    public:
        REFLECT_BODY();

        const TypeInfo*          getTypeInfo() const override { return StaticType(); } // 모듈을 내릴 때 UI 가 이 타입으로 닫는다(`UIScreen` 의 타입 규칙)
        static constexpr float32 kCellWidth    = 140.0f;                               ///< 칸 하나의 크기입니다(UI 단위).
        static constexpr float32 kCellHeight   = 44.0f;
        static constexpr uint32  kChurnWindow  = 128;   ///< 텍스트를 바꾸는 칸은 앞쪽 이만큼 안에서 돕니다(보이는 칸이며, 칠하기까지 측정합니다).
        static constexpr float32 kPulseOpacity = 0.95f; ///< `gv_stressTestUiOpacityPulse` 가 번갈아 쓰는 루트 불투명도입니다(1 과 이 값).

        /** @brief 칸 @p cellCount 개의 벤치 화면을 만듭니다. */
        static unique_ptr<UIStressTestScreen> create( uint32 cellCount );

        UIStressTestScreen( const UIScreenDef& desc, unique_ptr<Widget> root, vector<WidgetID> listCellText );

        void   onTick( float32 deltaSeconds ) override;
        uint32 getCellCount() const { return static_cast<uint32>( _listCellText.size() ); }

    private:
        vector<WidgetID> _listCellText; ///< 칸마다 텍스트 위젯 번호입니다(칸 순서).
        uint32           _churnCursor;  ///< 다음에 바꿀 칸입니다.
        uint32           _frameIndex;   ///< 바꾼 텍스트에 넣는 프레임 번호입니다.
    };

    namespace
    {
        struct UIStressTestScreenInternal
        {
            static void setFixedSize( Widget& widget, float32 width, float32 height )
            {
                WidgetLayoutSlot slot   = widget.getLayoutSlot();
                slot._widthOverride     = width;
                slot._heightOverride    = height;
                slot._verticalAlignment = UIAlignment::Center;
                widget.setLayoutSlot( slot );
            }

            static unique_ptr<BorderPanel> makeCell( uint32 index, WidgetID& outTextID )
            {
                unique_ptr<BorderPanel> cell = make_unique<BorderPanel>();
                cell->setBackground( UIBrush::makeSolid( float4{ 0.12f + 0.02f * static_cast<float32>( index % 4 ), 0.14f, 0.2f, 0.9f }, 6.0f ) );
                cell->setContentPadding( float4{ 6.0f, 4.0f, 6.0f, 4.0f } );
                setFixedSize( *cell, UIStressTestScreen::kCellWidth, UIStressTestScreen::kCellHeight );
                unique_ptr<BoxPanel> row = make_unique<BoxPanel>();
                row->setSpacing( 6.0f );
                unique_ptr<ImageWidget> icon = make_unique<ImageWidget>();
                icon->setBrush( UIBrush::makeSolid( float4{ 0.3f + 0.1f * static_cast<float32>( index % 5 ), 0.6f, 0.9f, 1.0f }, 4.0f ) );
                setFixedSize( *icon, 24.0f, 24.0f );
                (void)row->addChild( std::move( icon ) );
                unique_ptr<TextWidget>                   text = make_unique<TextWidget>();
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "Cell %#", index );
                text->setText( string( textBuilder.view() ) );
                setFixedSize( *text, UIStressTestScreen::kCellWidth - 48.0f, 24.0f );
                outTextID = text->getID();
                (void)row->addChild( std::move( text ) );
                (void)cell->addChild( std::move( row ) );
                return cell;
            }

            /** @brief 화면 스위치: `gv_stressTestUiWidgets` 를 보고 벤치 화면을 열고 닫습니다(개수가 바뀌면 다시 만듭니다). */
            static UIScreenHandle syncSwitch( UISystem& uiSystem, UIScreenHandle current )
            {
                const uint32              wanted    = gv_stressTestUiWidgets > 0 ? static_cast<uint32>( gv_stressTestUiWidgets ) : 0u;
                UIStressTestScreen* const pBench    = static_cast<UIStressTestScreen*>( uiSystem.findScreen( current ) );
                const uint32              cellCount = pBench != nullptr ? pBench->getCellCount() : 0u;
                if ( wanted == cellCount )
                    return current;
                if ( pBench != nullptr )
                    pBench->close();
                return wanted > 0 ? uiSystem.pushScreen( UIStressTestScreen::create( wanted ) ) : kInvalidUIScreenHandle;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_UI_SCREEN_SWITCH( UIBench, "UIBench", 1, &UIStressTestScreenInternal::syncSwitch );

    unique_ptr<UIStressTestScreen> UIStressTestScreen::create( uint32 cellCount )
    {
        using Internal                 = UIStressTestScreenInternal;
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
        vector<WidgetID> listCellText;
        listCellText.reserve( cellCount );
        for ( uint32 index = 0; index < cellCount; ++index )
        {
            WidgetID textID = kInvalidWidgetID;
            (void)grid->addChild( Internal::makeCell( index, textID ) );
            listCellText.push_back( textID );
        }
        (void)scroll->addChild( std::move( grid ) );
        (void)root->addChild( std::move( scroll ) );
        UIScreenDef desc{};
        desc._layer       = UILayer::HUD;
        desc._bTakesFocus = false;
        desc._bShowCursor = false;
        return sw::make_unique<UIStressTestScreen>( desc, std::move( root ), std::move( listCellText ) );
    }

    UIStressTestScreen::UIStressTestScreen( const UIScreenDef& desc, unique_ptr<Widget> root, vector<WidgetID> listCellText )
        : UIScreen{ desc, std::move( root ) }
        , _listCellText{ std::move( listCellText ) }
        , _churnCursor{ 0 }
        , _frameIndex{ 0 }
    {
    }

    void UIStressTestScreen::onTick( float32 deltaSeconds )
    {
        (void)deltaSeconds;
        ++_frameIndex;
        if ( gv_stressTestUiOpacityPulse )
        {
            Widget* pRoot = getTree().getRoot();
            if ( pRoot != nullptr )
                pRoot->setOpacity( ( _frameIndex & 1u ) == 0 ? 1.0f : kPulseOpacity );
        }
        const uint32 window = _listCellText.size() < kChurnWindow ? static_cast<uint32>( _listCellText.size() ) : kChurnWindow;
        const uint32 churn  = gv_stressTestUiChurn > 0 ? static_cast<uint32>( gv_stressTestUiChurn ) : 0u;
        if ( window == 0 || churn == 0 )
            return;
        WidgetTree& tree = getTree();
        for ( uint32 count = 0; count < churn; ++count )
        {
            const uint32 cell = _churnCursor;
            _churnCursor      = ( _churnCursor + 1 ) % window;
            if ( TextWidget* pText = castTo<TextWidget>( tree.findWidgetByID( _listCellText[cell] ) ); pText != nullptr )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "Cell %# #%#", cell, _frameIndex );
                pText->setText( string( textBuilder.view() ) );
            }
        }
    }
} // namespace sw

#include "UIStressTestScreen.cpp.gen.inl"
