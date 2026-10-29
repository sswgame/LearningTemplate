#include "pch.h"

#include "DevTools/Console/DevConsoleOverlay.h"

#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/BorderPanel.h"
#include "Engine/UI/Widgets/TextWidget.h"
#include "Engine/UI/Widgets/UIBrush.h"

namespace sw
{
    namespace
    {
        struct DevConsoleOverlayInternal
        {
            static constexpr float32 kFontSize = 14.0f;
            static constexpr float4  kPanelColor{ 0.07f, 0.08f, 0.1f, 0.92f };
            static constexpr float4  kLineColor{ 0.84f, 0.86f, 0.9f, 1.0f };
            static constexpr float4  kErrorColor{ 1.0f, 0.47f, 0.43f, 1.0f };
            static constexpr float4  kInputColor{ 1.0f, 0.86f, 0.47f, 1.0f };
        };

        /**
         * @class DevConsoleScreen
         * @brief 창 위쪽 가로 전체의 패널 하나와 줄마다 글 위젯 하나입니다. 줄 수가 같으면 글과 색만 바꾼다(같은 글이면 무효화도 없다).
         */
        class DevConsoleScreen final : public UIScreen
        {
        public:
            DevConsoleScreen( const UIScreenDesc& desc, unique_ptr<Widget> root, WidgetID column )
                : UIScreen{ desc, std::move( root ) }
                , _column{ column }
            {
            }

            static unique_ptr<DevConsoleScreen> create()
            {
                unique_ptr<CanvasPanel> root  = make_unique<CanvasPanel>();
                unique_ptr<BorderPanel> panel = make_unique<BorderPanel>();
                panel->setName( hashed_string( "DevConsolePanel" ) );
                panel->setBackground( UIBrush::makeSolid( DevConsoleOverlayInternal::kPanelColor, 0.0f ) );
                panel->setContentPadding( float4{ 8.0f, 6.0f, 8.0f, 6.0f } );
                // 위 가장자리에 붙고 가로로 늘어난다. 높이는 줄 수를 따른다.
                WidgetLayoutSlot slot = panel->getLayoutSlot();
                slot._anchorMin       = float2{ 0.0f, 0.0f };
                slot._anchorMax       = float2{ 1.0f, 0.0f };
                slot._offsetMin       = float2{ 0.0f, 0.0f };
                slot._offsetMax       = float2{ 0.0f, 0.0f };
                slot._bAutoSize       = true;
                slot._growVertical    = UIGrowDirection::End;
                panel->setLayoutSlot( slot );
                unique_ptr<BoxPanel> column = make_unique<BoxPanel>();
                column->setOrientation( UIOrientation::Vertical );
                column->setSpacing( 2.0f );
                const WidgetID columnID = column->getID();
                (void)panel->addChild( std::move( column ) );
                (void)root->addChild( std::move( panel ) );

                UIScreenDesc desc{};
                desc._layer       = UILayer::Overlay;
                desc._bTakesFocus = false;
                desc._bShowCursor = false;
                return sw::make_unique<DevConsoleScreen>( desc, std::move( root ), columnID );
            }

            void present( const vector<string>& listLine, const vector<uint8>& listErrorFlag )
            {
                PanelWidget* pColumn = static_cast<PanelWidget*>( getTree().findWidgetByID( _column ) );
                if ( pColumn == nullptr )
                    return;
                const uint32 lineCount = static_cast<uint32>( listLine.size() );
                if ( pColumn->getChildCount() != lineCount )
                {
                    pColumn->clearChildren();
                    for ( uint32 index = 0; index < lineCount; ++index )
                    {
                        unique_ptr<TextWidget> text  = make_unique<TextWidget>();
                        TextLayoutStyle        style = text->getTextStyle();
                        style._fontSize              = DevConsoleOverlayInternal::kFontSize;
                        text->setTextStyle( style );
                        text->setLocalized( false ); // 개발 도구라 번역하지 않는다(명령 · 값 그대로)
                        (void)pColumn->addChild( std::move( text ) );
                    }
                }
                for ( uint32 index = 0; index < lineCount; ++index )
                {
                    TextWidget*   pText  = static_cast<TextWidget*>( pColumn->getChild( index ) );
                    const bool    bInput = index + 1 == lineCount;
                    const bool    bError = index < listErrorFlag.size() && listErrorFlag[index] != 0;
                    const float4& color  = bInput ? DevConsoleOverlayInternal::kInputColor : ( bError ? DevConsoleOverlayInternal::kErrorColor : DevConsoleOverlayInternal::kLineColor );
                    pText->setColor( color );
                    pText->setText( listLine[index] );
                }
            }

        private:
            WidgetID _column;
        };
    } // namespace

    DevConsoleOverlay::DevConsoleOverlay( UISystem& ui )
        : _pUI{ &ui }
        , _screen{ kInvalidUIScreenHandle }
    {
    }

    DevConsoleOverlay::~DevConsoleOverlay()
    {
        setVisible( false );
    }

    void DevConsoleOverlay::setVisible( bool bVisible )
    {
        if ( bVisible == ( _screen != kInvalidUIScreenHandle ) )
            return;
        if ( bVisible )
        {
            _screen = _pUI->pushScreen( DevConsoleScreen::create() );
            return;
        }
        _pUI->closeScreen( _screen );
        _screen = kInvalidUIScreenHandle;
    }

    void DevConsoleOverlay::present( const vector<string>& listLine, const vector<uint8>& listErrorFlag )
    {
        // 화면은 UI 시스템이 소유한다 — 핸들로 다시 찾는다(닫혔으면 아무것도 하지 않는다).
        if ( UIScreen* pScreen = _pUI->findScreen( _screen ) )
            static_cast<DevConsoleScreen*>( pScreen )->present( listLine, listErrorFlag );
    }
} // namespace sw
