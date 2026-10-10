#include "pch.h"

#include "Engine/UI/Widgets/ComboBoxWidget.h"

#include "Core/Common/Defines.h"
#include "Core/Container/string.h"

#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/TextWidget.h"

namespace sw
{
    namespace
    {
        /** @brief 팝업 화면의 루트 — 뷰포트 전체를 덮어, 항목 밖 클릭을 받으면 팝업을 닫는다. */
        class ComboBoxPopupRoot final : public CanvasPanel
        {
        public:
            UIReply onPointerEvent( const UIPointerEvent& event, UIRoutePhase phase ) override
            {
                if ( phase != UIRoutePhase::Bubble || event._kind != UIPointerEventKind::Down || getTree() == nullptr || getTree()->getScreen() == nullptr )
                    return UIReply::makeUnhandled();
                getTree()->getScreen()->close();
                return UIReply::makeHandled();
            }
        };

        /** @brief 콤보 상자의 팝업 화면 — 상자를 화면 핸들 · 위젯 번호로 다시 찾아 고른 값을 넘긴다. */
        class ComboBoxPopupScreen final : public UIScreen
        {
        public:
            ComboBoxPopupScreen( const UIScreenDesc& desc, unique_ptr<Widget> root, UIScreenHandle ownerScreen, WidgetID combo )
                : UIScreen{ desc, std::move( root ) }
                , _ownerScreen{ ownerScreen }
                , _combo{ combo }
            {
            }

            void choose( uint32 index )
            {
                UISystem* const pUI    = getUISystem();
                UIScreen* const pOwner = pUI != nullptr ? pUI->findScreen( _ownerScreen ) : nullptr;
                Widget* const   pFound = pOwner != nullptr ? pOwner->getTree().findWidgetByID( _combo ) : nullptr;
                ComboBoxWidget* pCombo = pFound != nullptr ? castTo<ComboBoxWidget>( pFound ) : nullptr;
                if ( pCombo != nullptr )
                    pCombo->choosePopupOption( index );
                close();
            }

        private:
            UIScreenHandle _ownerScreen;
            WidgetID       _combo;
        };

        struct ComboBoxWidgetInternal
        {
            /** @brief 항목 버튼 하나의 높이 · 목록과 상자 사이 틈(UI 단위)입니다. */
            static constexpr float32 kOptionHeight = 32.0f;
            static constexpr float32 kPopupGap     = 2.0f;
            /** @brief 오른쪽 화살표 자리(안쪽 여백)입니다. */
            static constexpr float32 kArrowSpace = 32.0f;

            static hashed_string makeOptionName( uint32 index ) { return hashed_string( "option" + to_string( index ) ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ComboBoxWidget::ComboBoxWidget()
        : ButtonWidget{}
        , _onSelectionChanged{}
        , _pLabel{ nullptr }
        , _popupScreen{ kInvalidUIScreenHandle }
        , _listOption{}
        , _selectedIndex{ invalid_index::kUint32 }
    {
        setContentPadding( float4{ 12.0f, 8.0f, ComboBoxWidgetInternal::kArrowSpace, 8.0f } );
        unique_ptr<TextWidget> label = make_unique<TextWidget>();
        TextLayoutStyle        style = label->getTextStyle();
        style._bWrap                 = false;
        style._overflow              = TextOverflow::Ellipsis;
        label->setTextStyle( style );
        _pLabel = static_cast<TextWidget*>( addChild( std::move( label ) ) );
    }

    ComboBoxWidget::~ComboBoxWidget() = default;

    const TypeInfo* ComboBoxWidget::getTypeInfo() const
    {
        return StaticType();
    }

    void ComboBoxWidget::setOptions( const vector<string>& listOption )
    {
        _listOption = listOption;
        if ( _selectedIndex != invalid_index::kUint32 && _selectedIndex >= _listOption.size() )
            _selectedIndex = invalid_index::kUint32;
        refreshLabel();
    }

    void ComboBoxWidget::setSelectedIndex( uint32 index )
    {
        _selectedIndex = index < _listOption.size() ? index : invalid_index::kUint32;
        refreshLabel();
    }

    void ComboBoxWidget::onBoundPropertyChanged( const PropertyInfo& property )
    {
        if ( property._name == hashed_string( "_listOption" ) || property._name == hashed_string( "_selectedIndex" ) )
        {
            if ( _selectedIndex != invalid_index::kUint32 && _selectedIndex >= _listOption.size() )
                _selectedIndex = invalid_index::kUint32;
            refreshLabel();
            return;
        }
        ButtonWidget::onBoundPropertyChanged( property );
    }

    void ComboBoxWidget::refreshLabel()
    {
        _pLabel->setText( _selectedIndex != invalid_index::kUint32 ? string_view{ _listOption[_selectedIndex] } : string_view{} );
    }

    void ComboBoxWidget::choosePopupOption( uint32 index )
    {
        _popupScreen = kInvalidUIScreenHandle;
        if ( index >= _listOption.size() )
            return;
        const bool bChanged = index != _selectedIndex;
        setSelectedIndex( index );
        if ( bChanged )
        {
            _onSelectionChanged.broadcast( index );
            notifyValueEdited( "_selectedIndex" );
        }
    }

    void ComboBoxWidget::handleClick()
    {
        ButtonWidget::handleClick();
        openPopup();
    }

    void ComboBoxWidget::openPopup()
    {
        WidgetTree* const pTree   = getTree();
        UIScreen* const   pScreen = pTree != nullptr ? pTree->getScreen() : nullptr;
        UISystem* const   pUI     = pScreen != nullptr ? pScreen->getUISystem() : nullptr;
        if ( pUI == nullptr || _listOption.empty() )
            return;
        if ( _popupScreen != kInvalidUIScreenHandle && pUI->findScreen( _popupScreen ) != nullptr )
            return; // 이미 열려 있다

        using Internal                     = ComboBoxWidgetInternal;
        const UIRect                  rect = getGeometry().computeScreenBounds();
        unique_ptr<ComboBoxPopupRoot> root = make_unique<ComboBoxPopupRoot>(); // 화면 루트 — 뷰포트 전체

        unique_ptr<BorderPanel> list      = make_unique<BorderPanel>();
        UIBrush                 listBrush = UIBrush::makeSolid( float4{ 0.1f, 0.11f, 0.14f, 0.98f }, 6.0f );
        listBrush._borderColor            = float4{ 0.45f, 0.48f, 0.55f, 1.0f };
        listBrush._borderWidth            = 1.0f;
        list->setBackground( listBrush );
        list->setContentPadding( float4{ 2.0f, 2.0f, 2.0f, 2.0f } );
        list->setShadow( float4{ 0.0f, 0.0f, 0.0f, 0.5f }, 8.0f, float2{ 0.0f, 4.0f } );
        WidgetLayoutSlot listSlot = list->getLayoutSlot();
        listSlot._offsetMin       = float2{ rect.getLeft(), rect.getBottom() + Internal::kPopupGap };
        listSlot._bAutoSize       = true;
        listSlot._widthOverride   = rect.getRight() - rect.getLeft();
        list->setLayoutSlot( listSlot );

        unique_ptr<BoxPanel> column = make_unique<BoxPanel>();
        column->setOrientation( UIOrientation::Vertical );
        vector<ButtonWidget*> listButton;
        for ( uint32 index = 0; index < static_cast<uint32>( _listOption.size() ); ++index )
        {
            unique_ptr<ButtonWidget> option = make_unique<ButtonWidget>();
            option->setName( Internal::makeOptionName( index ) );
            option->setBackground( UIBrush::makeSolid( float4{}, 4.0f ) );
            option->setContentPadding( float4{ 10.0f, 6.0f, 10.0f, 6.0f } );
            WidgetLayoutSlot optionSlot = option->getLayoutSlot();
            optionSlot._heightOverride  = Internal::kOptionHeight;
            option->setLayoutSlot( optionSlot );
            unique_ptr<TextWidget> text = make_unique<TextWidget>();
            text->setText( _listOption[index] );
            (void)option->addChild( std::move( text ) );
            listButton.push_back( static_cast<ButtonWidget*>( column->addChild( std::move( option ) ) ) );
        }
        (void)list->addChild( std::move( column ) );
        (void)root->addChild( std::move( list ) );

        UIScreenDesc desc{};
        desc._layer                            = UILayer::Modal; // 메뉴 위 — 막지는 않는다(밖 클릭은 루트가 받아 닫는다)
        desc._bModal                           = false;
        desc._defaultFocus                     = Internal::makeOptionName( _selectedIndex != invalid_index::kUint32 ? _selectedIndex : 0 );
        unique_ptr<ComboBoxPopupScreen> popup  = sw::make_unique<ComboBoxPopupScreen>( desc, std::move( root ), pScreen->getHandle(), getID() );
        ComboBoxPopupScreen* const      pPopup = popup.get();
        for ( uint32 index = 0; index < static_cast<uint32>( listButton.size() ); ++index )
        {
            (void)listButton[index]->getOnClicked().add( [pPopup, index]( WidgetID )
            { pPopup->choose( index ); } );
        }
        _popupScreen = pUI->pushScreen( std::move( popup ) );
    }

    void ComboBoxWidget::paintOverChildren( CanvasPainter& painter, const UIPaintContext& context ) const
    {
        (void)context;
        // 아래 화살표 — 가로 막대 셋을 줄여 쌓은 삼각형(오른쪽 끝, 오른쪽에서 왼쪽이면 왼쪽 끝).
        const float2& size    = getGeometry()._size;
        const float32 centerX = isRightToLeft() ? ComboBoxWidgetInternal::kArrowSpace * 0.5f : size._x - ComboBoxWidgetInternal::kArrowSpace * 0.5f;
        const float32 top     = size._y * 0.5f - 3.0f;
        CanvasBrush   arrow{};
        arrow._color             = float4{ 0.85f, 0.88f, 0.95f, isEnabledInHierarchy() ? 1.0f : 0.4f };
        const float32 arrWidth[] = { 10.0f, 6.0f, 2.0f };
        for ( uint32 step = 0; step < 3; ++step )
        {
            painter.fillRect( float2{ centerX - arrWidth[step] * 0.5f, top + 2.0f * static_cast<float32>( step ) }, float2{ arrWidth[step], 2.0f }, arrow );
        }
    }
} // namespace sw
