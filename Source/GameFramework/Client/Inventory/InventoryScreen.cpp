#include "pch.h"

#include "GameFramework/Client/Inventory/InventoryScreen.h"

#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/UI/Menu/QuantityDialogScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/PanelWidget.h"
#include "Engine/UI/WidgetTree/UIEvents.h"
#include "Engine/UI/WidgetTree/UIFocusManager.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"
#include "Engine/UI/Widgets/ButtonWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "GameFramework/Client/Inventory/InventorySlotWidget.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/GameData/StatBlock.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterEquipmentComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/PickupComponent.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct InventoryScreenInternal
        {
            static constexpr float32     kCellWidth        = 136.0f; ///< 칸 단추 너비(UI 단위)
            static constexpr float32     kCellHeight       = 48.0f;  ///< 칸 단추 높이
            static constexpr const utf8* kSlotCommand      = "Slot";
            static constexpr const utf8* kEquipSlotCommand = "EquipSlot";
            static constexpr const utf8* kEquipNamePrefix  = "Equip.";
            static constexpr const utf8* kSlotNamePrefix   = "Slot";
            /** @brief 버린 조각이 놓이는 자리(캐릭터 기준, m) — 발 앞 한 걸음 */
            static constexpr float32 kDropForward = 1.0f;

            static const utf8* getResultKey( InventoryCommandResult result )
            {
                switch ( result )
                {
                    case InventoryCommandResult::Ok:
                    {
                        return SW_LOCKEYREF( "Inventory", "Done" );
                    }
                    case InventoryCommandResult::InvalidSlot:
                    {
                        return SW_LOCKEYREF( "Inventory", "InvalidSlot" );
                    }
                    case InventoryCommandResult::EmptySlot:
                    {
                        return SW_LOCKEYREF( "Inventory", "EmptySlot" );
                    }
                    case InventoryCommandResult::InvalidCount:
                    {
                        return SW_LOCKEYREF( "Inventory", "InvalidCount" );
                    }
                    case InventoryCommandResult::NoRoom:
                    {
                        return SW_LOCKEYREF( "Inventory", "NoRoom" );
                    }
                    case InventoryCommandResult::Refused:
                    {
                        return SW_LOCKEYREF( "Inventory", "Refused" );
                    }
                    case InventoryCommandResult::NotUsable:
                    {
                        return SW_LOCKEYREF( "Inventory", "NotUsable" );
                    }
                }
                return SW_LOCKEYREF( "Inventory", "Refused" );
            }

            static unique_ptr<TextWidget> makeLabel()
            {
                unique_ptr<TextWidget> label = make_unique<TextWidget>();
                WidgetLayoutSlot       slot  = label->getLayoutSlot();
                slot._verticalAlignment      = UIAlignment::Center;
                label->setLayoutSlot( slot );
                return label;
            }

            /** @brief 툴팁 · 자세히 글입니다: 이름, 설명, 수치(한 줄에 하나). 이름 · 설명은 현지화 키이거나 원문이다. */
            static string makeDetails( const ItemDef& item, int32 count )
            {
                StringBuilder<constant::buffer::kMax512> builder;
                builder.appendFormat( "%#", item._name.empty() ? item._id.c_str() : item._name.c_str() );
                if ( count > 1 )
                    builder.appendFormat( " x%#", count );
                if ( item._description.empty() == false )
                    builder.appendFormat( "\n%#", item._description );
                for ( const StatValue& stat : item._stats.getValues() )
                {
                    builder.appendFormat( "\n%# %#", stat._name.c_str(), stat._value );
                }
                return string( builder.view() );
            }
        };
    } // namespace

    InventoryScreen::InventoryScreen( const UIScreenDef& desc, unique_ptr<Widget> root )
        : UIScreen{ desc, std::move( root ) }
        , _listSlotWidget{}
        , _character{}
        , _filter{}
        , _pressSeconds{ 0.0f }
        , _pressedSlot{ kNoSlot }
        , _heldSlot{ kNoSlot }
        , _menuSlot{ kNoSlot }
        , _shownBagRevision{ 0 }
        , _shownEquipmentRevision{ 0 }
        , _commandCount{}
        , _lastResult{ InventoryCommandResult::Ok }
        , _bLongPressFired{ SW_FALSE }
        , _bBuilt{ SW_FALSE }
    {
    }

    InventoryScreen::~InventoryScreen() = default;

    UIScreenHandle InventoryScreen::open( UISystem& ui, string_view documentPath, CharacterEquipmentComponent& character )
    {
        if ( character.isStarted() == false )
        {
            SW_LOG_ERROR_ID( "InventoryScreen.CharacterNotStarted", "Inventory screen needs a started character equipment - '%#' was not opened", string( documentPath ).c_str() );
            return kInvalidUIScreenHandle;
        }
        // 칸은 올리기 전에 짓는다 — 탐색 방식으로 열면 올리는 순간 기본 포커스(`Slot0`)가 서야 한다.
        return ui.openScreen<InventoryScreen>( documentPath, [&character]( InventoryScreen& screen )
        {
            screen._character = WeakRef<CharacterEquipmentComponent>{ character };
            screen.buildWidgets();
            return true;
        } );
    }

    void InventoryScreen::buildWidgets()
    {
        using Internal                                = InventoryScreenInternal;
        CharacterEquipmentComponent* const pCharacter = _character.get();
        WidgetTree&                        tree       = getTree();
        PanelWidget*                       pGrid      = tree.findWidget<PanelWidget>( hashed_string( kGridName ) );
        PanelWidget*                       pEquipment = tree.findWidget<PanelWidget>( hashed_string( kEquipmentName ) );
        const bool                         bTexts     = tree.findWidget<TextWidget>( hashed_string( kDetailsName ) ) != nullptr && tree.findWidget<TextWidget>( hashed_string( kStatsName ) ) != nullptr &&
                            tree.findWidget<TextWidget>( hashed_string( kWeightName ) ) != nullptr && tree.findWidget<TextWidget>( hashed_string( kStatusName ) ) != nullptr;
        if ( pGrid == nullptr || pEquipment == nullptr || tree.findWidgetByName( hashed_string( kActionsName ) ) == nullptr || bTexts == false || pCharacter == nullptr )
        {
            SW_LOG_ERROR_ID( "InventoryScreen.InventoryDocumentNeeds", "Inventory document %# needs '%#' and '%#' panels, an '%#' panel and the '%#', '%#', '%#' and '%#' texts", getDocumentPath().c_str(),
                             kGridName, kEquipmentName, kActionsName, kDetailsName, kStatsName, kWeightName, kStatusName );
            _bBuilt = SW_TRUE; // 문서가 틀렸다 — 프레임마다 같은 오류를 되풀이하지 않는다
            return;
        }
        pGrid->clearChildren();
        _listSlotWidget.clear();
        for ( int32 index = 0; index < pCharacter->getBag().getSlotCount(); ++index )
        {
            unique_ptr<InventorySlotWidget>         slot = make_unique<InventorySlotWidget>( *this, index );
            StringBuilder<constant::buffer::kMax32> nameBuilder;
            nameBuilder.appendFormat( "%#%#", Internal::kSlotNamePrefix, index );
            slot->setName( hashed_string( nameBuilder.view() ) );
            slot->setCommand( hashed_string( Internal::kSlotCommand ) );
            WidgetLayoutSlot layout = slot->getLayoutSlot();
            layout._widthOverride   = Internal::kCellWidth;
            layout._heightOverride  = Internal::kCellHeight;
            slot->setLayoutSlot( layout );
            (void)slot->addChild( Internal::makeLabel() );
            _listSlotWidget.push_back( slot.get() );
            (void)pGrid->addChild( std::move( slot ) );
        }
        pEquipment->clearChildren();
        for ( const EquipmentSlot& equipSlot : pCharacter->getEquipment().getSlots() )
        {
            unique_ptr<ButtonWidget>                button = make_unique<ButtonWidget>();
            fixed_string<constant::buffer::kMax128> name;
            formatstring( name.data(), name.capacity(), "%#%#", Internal::kEquipNamePrefix, equipSlot._name.c_str() );
            button->setName( hashed_string( name.c_str() ) );
            button->setCommand( hashed_string( Internal::kEquipSlotCommand ) );
            (void)button->addChild( Internal::makeLabel() );
            (void)pEquipment->addChild( std::move( button ) );
        }
        _bBuilt = SW_TRUE;
        refresh();
    }

    void InventoryScreen::refresh()
    {
        CharacterEquipmentComponent* const pCharacter = _character.get();
        if ( pCharacter == nullptr || _listSlotWidget.empty() )
            return;
        const Inventory&   bag      = pCharacter->getBag();
        const ItemCatalog* pCatalog = bag.getCatalog();
        for ( InventorySlotWidget* pWidget : _listSlotWidget )
        {
            const int32                              index = pWidget->getBagSlot();
            const InventorySlot&                     item  = bag.getSlot( index );
            const ItemDef*                           pDef  = pCatalog != nullptr && item.isEmpty() == false ? pCatalog->find( item._itemID ) : nullptr;
            TextWidget*                              pText = castTo<TextWidget>( pWidget->getChild( 0 ) );
            StringBuilder<constant::buffer::kMax128> label;
            if ( pDef != nullptr )
                label.appendFormat( "%#", pDef->_name.empty() ? item._itemID.c_str() : pDef->_name.c_str() );
            if ( pDef != nullptr && item._count > 1 )
                label.appendFormat( " x%#", item._count );
            if ( pText != nullptr )
                pText->setText( string( label.view() ) );
            pWidget->setTooltip( pDef != nullptr ? InventoryScreenInternal::makeDetails( *pDef, item._count ) : string{} );
            // 거르기에 맞지 않는 칸은 끈다(빈 칸은 놓을 자리라 켜 둔다). 집은 칸은 선택 표시(중립 단계).
            const bool bFiltered = _filter.empty() == false && pDef != nullptr && pDef->_category != _filter;
            pWidget->setEnabled( bFiltered == false );
            pWidget->setStyleClass( index == _heldSlot ? "selected" : "" );
        }
        PanelWidget* pEquipment = getTree().findWidget<PanelWidget>( hashed_string( kEquipmentName ) );
        if ( pEquipment != nullptr )
        {
            const vector<EquipmentSlot>& listSlot = pCharacter->getEquipment().getSlots();
            for ( uint32 index = 0; index < pEquipment->getChildCount() && index < listSlot.size(); ++index )
            {
                const EquipmentSlot&                     equipSlot = listSlot[index];
                const ItemDef*                           pDef      = pCatalog != nullptr && equipSlot._item.isEmpty() == false ? pCatalog->find( equipSlot._item._itemID ) : nullptr;
                Widget*                                  pButton   = pEquipment->getChild( index );
                PanelWidget*                             pPanel    = castTo<PanelWidget>( pButton );
                TextWidget*                              pText     = pPanel != nullptr && pPanel->getChildCount() > 0 ? castTo<TextWidget>( pPanel->getChild( 0 ) ) : nullptr;
                StringBuilder<constant::buffer::kMax128> label;
                label.appendFormat( "%#: %#", equipSlot._name.c_str(), pDef != nullptr ? ( pDef->_name.empty() ? pDef->_id.c_str() : pDef->_name.c_str() ) : "-" );
                if ( pText != nullptr )
                    pText->setText( string( label.view() ) );
                pButton->setTooltip( pDef != nullptr ? InventoryScreenInternal::makeDetails( *pDef, 1 ) : string{} );
            }
        }
        StatBlock stats;
        pCharacter->getEquipment().computeStats( stats );
        StringBuilder<constant::buffer::kMax256> statsBuilder;
        for ( const StatValue& stat : stats.getValues() )
        {
            statsBuilder.appendFormat( "%#%# %#", statsBuilder.view().empty() ? "" : "  ", stat._name.c_str(), stat._value );
        }
        TextWidget* pStats = getTree().findWidget<TextWidget>( hashed_string( kStatsName ) );
        if ( pStats != nullptr )
            pStats->setText( string( statsBuilder.view() ) );
        StringBuilder<constant::buffer::kMax128> weightBuilder;
        if ( bag.getMaxWeight() > 0.0f )
            weightBuilder.appendFormat( "%# / %#", bag.computeWeight(), bag.getMaxWeight() );
        else
            weightBuilder.appendFormat( "%#", bag.computeWeight() );
        TextWidget* pWeight = getTree().findWidget<TextWidget>( hashed_string( kWeightName ) );
        if ( pWeight != nullptr )
            pWeight->setText( string( weightBuilder.view() ) );
        _shownBagRevision       = bag.getRevision();
        _shownEquipmentRevision = pCharacter->getEquipment().getRevision();
    }

    void InventoryScreen::refreshDetails()
    {
        TextWidget* pDetails = getTree().findWidget<TextWidget>( hashed_string( kDetailsName ) );
        UISystem*   pUI      = getUISystem();
        if ( pDetails == nullptr || pUI == nullptr )
            return;
        const UIFocusManager& focus    = pUI->getInputRouter().getFocusManager();
        const Widget*         pFocused = focus.getFocusedTree() == &getTree() ? getTree().findWidgetByID( focus.getFocusedWidget() ) : nullptr;
        pDetails->setText( pFocused != nullptr ? pFocused->getTooltip() : string{} );
    }

    void InventoryScreen::setStatus( const string& text )
    {
        TextWidget* pStatus = getTree().findWidget<TextWidget>( hashed_string( kStatusName ) );
        if ( pStatus != nullptr )
            pStatus->setText( text );
    }

    InventoryCommandResult InventoryScreen::run( const InventoryCommand& command )
    {
        CharacterEquipmentComponent* const pCharacter = _character.get();
        if ( pCharacter == nullptr )
            return InventoryCommandResult::InvalidSlot;
        vector<InventorySlot> listDropped;
        _lastResult = pCharacter->applyCommand( command, listDropped );
        _commandCount.add();
        SW_LOG_INFO_ID( "InventoryScreen.CommandApplied", "Inventory %# from %# to %# (count %#, slot '%#'): %#", getEnumName( command._kind ), command._fromSlot, command._toSlot, command._count,
                        command._equipSlot.c_str(), getEnumName( _lastResult ) );
        GameObject*        pOwner   = pCharacter->getOwner();
        GameObjectManager* pManager = engine::findActiveObjectManager();
        if ( listDropped.empty() == false && pOwner != nullptr && pManager != nullptr )
        {
            const SceneComponent* pScene = pOwner->getPrimarySceneComponent();
            float3                place  = pScene != nullptr ? pScene->getWorldPosition() : float3{};
            place._z += InventoryScreenInternal::kDropForward;
            for ( const InventorySlot& dropped : listDropped )
            {
                (void)PickupComponent::spawn( *pManager, dropped, place );
            }
        }
        setStatus( InventoryScreenInternal::getResultKey( _lastResult ) );
        refresh();
        return _lastResult;
    }

    void InventoryScreen::onSlotActivated( int32 bagSlot )
    {
        CharacterEquipmentComponent* const pCharacter = _character.get();
        if ( pCharacter == nullptr || bagSlot < 0 || bagSlot >= pCharacter->getBag().getSlotCount() )
            return;
        if ( _heldSlot == kNoSlot )
        {
            if ( pCharacter->getBag().getSlot( bagSlot ).isEmpty() == false )
            {
                _heldSlot = bagSlot;
                setStatus( SW_LOCKEYREF( "Inventory", "Picked" ) );
                refresh();
            }
            return;
        }
        if ( _heldSlot == bagSlot )
        {
            _heldSlot = kNoSlot; // 같은 칸을 다시 누르면 내려놓는다
            refresh();
            return;
        }
        InventoryCommand command{};
        command._kind     = InventoryCommandKind::Move;
        command._fromSlot = _heldSlot;
        command._toSlot   = bagSlot;
        _heldSlot         = kNoSlot;
        (void)run( command );
    }

    void InventoryScreen::onSlotPressed( int32 bagSlot )
    {
        _pressedSlot     = bagSlot;
        _pressSeconds    = 0.0f;
        _bLongPressFired = SW_FALSE;
    }

    void InventoryScreen::onSlotReleased( int32 bagSlot, const float2& position )
    {
        _pressedSlot      = kNoSlot;
        const int32 under = findSlotAt( position );
        if ( under == kNoSlot || under == bagSlot || _bLongPressFired == SW_TRUE )
            return; // 제자리 뗌은 클릭(단추 명령)이 처리한다
        // 끌어 놓기 — 누른 칸에서 뗀 칸으로 옮긴다(집은 것이 있었으면 그것은 내려놓는다).
        _heldSlot = kNoSlot;
        InventoryCommand command{};
        command._kind     = InventoryCommandKind::Move;
        command._fromSlot = bagSlot;
        command._toSlot   = under;
        (void)run( command );
    }

    void InventoryScreen::onSlotSecondary( int32 bagSlot )
    {
        openActions( bagSlot );
    }

    int32 InventoryScreen::findSlotAt( const float2& position ) const
    {
        for ( const InventorySlotWidget* pWidget : _listSlotWidget )
        {
            float2 local{};
            if ( pWidget->getGeometry().inverseTransformPoint( position, local ) && pWidget->getGeometry().containsLocal( local ) )
                return pWidget->getBagSlot();
        }
        return kNoSlot;
    }

    void InventoryScreen::openActions( int32 bagSlot )
    {
        CharacterEquipmentComponent* const pCharacter = _character.get();
        if ( pCharacter == nullptr || bagSlot < 0 || bagSlot >= pCharacter->getBag().getSlotCount() || pCharacter->getBag().getSlot( bagSlot ).isEmpty() )
            return;
        Widget* pActions = getTree().findWidgetByName( hashed_string( kActionsName ) );
        if ( pActions == nullptr )
            return;
        _menuSlot = bagSlot;
        _heldSlot = kNoSlot;
        pActions->setVisibility( WidgetVisibility::Visible );
        // 나누기는 둘 이상일 때만 켠다.
        Widget* pSplit = getTree().findWidgetByName( hashed_string( "ActSplit" ) );
        if ( pSplit != nullptr )
            pSplit->setEnabled( pCharacter->getBag().getSlot( bagSlot )._count > 1 );
        Widget*   pFirst = getTree().findWidgetByName( hashed_string( "ActUse" ) );
        UISystem* pUI    = getUISystem();
        if ( pFirst != nullptr && pUI != nullptr )
            (void)pUI->getInputRouter().getFocusManager().setFocus( getTree(), pFirst->getID() );
        refresh();
    }

    void InventoryScreen::closeActions()
    {
        Widget* pActions = getTree().findWidgetByName( hashed_string( kActionsName ) );
        if ( pActions != nullptr )
            pActions->setVisibility( WidgetVisibility::Collapsed );
        UISystem* pUI = getUISystem();
        if ( _menuSlot != kNoSlot && _menuSlot < static_cast<int32>( _listSlotWidget.size() ) && pUI != nullptr )
            (void)pUI->getInputRouter().getFocusManager().setFocus( getTree(), _listSlotWidget[static_cast<size_t>( _menuSlot )]->getID() ); // 메뉴를 연 칸으로 돌아간다
        _menuSlot = kNoSlot;
    }

    void InventoryScreen::cycleFilter()
    {
        // 가방에 있는 분류를 칸 순서로 돌고, 끝이면 모두(빈 분류)로 돌아온다.
        CharacterEquipmentComponent* const pCharacter = _character.get();
        if ( pCharacter == nullptr )
            return;
        const Inventory&      bag      = pCharacter->getBag();
        const ItemCatalog*    pCatalog = bag.getCatalog();
        vector<hashed_string> listCategory;
        for ( int32 index = 0; index < bag.getSlotCount() && pCatalog != nullptr; ++index )
        {
            const ItemDef* pDef = bag.getSlot( index ).isEmpty() ? nullptr : pCatalog->find( bag.getSlot( index )._itemID );
            if ( pDef != nullptr && pDef->_category.empty() == false && std::find( listCategory.begin(), listCategory.end(), pDef->_category ) == listCategory.end() )
                listCategory.push_back( pDef->_category );
        }
        auto iter = std::find( listCategory.begin(), listCategory.end(), _filter );
        if ( _filter.empty() )
            _filter = listCategory.empty() ? hashed_string{} : listCategory.front();
        else
            _filter = ( iter == listCategory.end() || iter + 1 == listCategory.end() ) ? hashed_string{} : *( iter + 1 );
        refresh();
    }

    bool InventoryScreen::onCommand( const hashed_string& command, Widget& source )
    {
        using Internal                                = InventoryScreenInternal;
        CharacterEquipmentComponent* const pCharacter = _character.get();
        if ( command == hashed_string( Internal::kSlotCommand ) )
        {
            const InventorySlotWidget* pSlot = castTo<InventorySlotWidget>( &source );
            if ( pSlot != nullptr && _bLongPressFired == SW_FALSE )
                onSlotActivated( pSlot->getBagSlot() );
            _bLongPressFired = SW_FALSE;
            return true;
        }
        if ( command == hashed_string( Internal::kEquipSlotCommand ) )
        {
            const string     name( source.getName().c_str() );
            const size_t     prefix = string( Internal::kEquipNamePrefix ).size();
            InventoryCommand equip{};
            equip._equipSlot = hashed_string( name.size() > prefix ? name.substr( prefix ) : string{} );
            equip._kind      = _heldSlot != kNoSlot ? InventoryCommandKind::Equip : InventoryCommandKind::Unequip;
            equip._fromSlot  = _heldSlot;
            _heldSlot        = kNoSlot;
            (void)run( equip );
            return true;
        }
        if ( command == hashed_string( "Sort" ) )
        {
            _heldSlot = kNoSlot;
            InventoryCommand sort{};
            sort._kind = InventoryCommandKind::Sort;
            (void)run( sort );
            return true;
        }
        if ( command == hashed_string( "Filter" ) )
        {
            cycleFilter();
            return true;
        }
        if ( command == hashed_string( "Close" ) )
        {
            close();
            return true;
        }
        if ( command == hashed_string( "ActClose" ) )
        {
            closeActions();
            return true;
        }
        const bool bAction = command == hashed_string( "ActUse" ) || command == hashed_string( "ActEquip" ) || command == hashed_string( "ActSplit" ) || command == hashed_string( "ActDrop" );
        if ( bAction == false )
            return UIScreen::onCommand( command, source );
        const int32 slot = _menuSlot;
        closeActions();
        if ( pCharacter == nullptr || slot == kNoSlot )
            return true;
        InventoryCommand action{};
        action._fromSlot  = slot;
        const int32 count = pCharacter->getBag().getSlot( slot )._count;
        if ( command == hashed_string( "ActUse" ) )
            action._kind = InventoryCommandKind::Use;
        else if ( command == hashed_string( "ActEquip" ) )
            action._kind = InventoryCommandKind::Equip;
        else
        {
            action._kind         = command == hashed_string( "ActSplit" ) ? InventoryCommandKind::Split : InventoryCommandKind::Drop;
            action._count        = 1;
            const int32 maxCount = action._kind == InventoryCommandKind::Split ? count - 1 : count;
            UISystem*   pUI      = getUISystem();
            if ( maxCount > 1 && pUI != nullptr )
            {
                // 수량은 대화 상자로 고른다 — 답은 화면 핸들로 찾아, 그 사이 화면이 닫혔으면 버린다.
                const UIScreenHandle handle = getHandle();
                const string         title  = action._kind == InventoryCommandKind::Split ? SW_LOCKEYREF( "Inventory", "SplitTitle" ) : SW_LOCKEYREF( "Inventory", "DropTitle" );
                (void)QuantityDialogScreen::open( *pUI, title, maxCount, ( maxCount + 1 ) / 2, QuantityDialogScreen::ResultDelegate::create( [pUI, handle, action]( ConfirmDialogResult result, int32 chosen )
                { onQuantityChosen( *pUI, handle, action, result, chosen ); } ) );
                return true;
            }
        }
        (void)run( action );
        return true;
    }

    void InventoryScreen::onQuantityChosen( UISystem& ui, UIScreenHandle screen, InventoryCommand command, ConfirmDialogResult result, int32 count )
    {
        UIScreen* pScreen = ui.findScreen( screen );
        if ( result != ConfirmDialogResult::Accepted || pScreen == nullptr || pScreen->isClosing() )
            return;
        command._count = count;
        (void)static_cast<InventoryScreen&>( *pScreen ).run( command );
    }

    bool InventoryScreen::onBack()
    {
        Widget* pActions = getTree().findWidgetByName( hashed_string( kActionsName ) );
        if ( pActions != nullptr && pActions->isVisible() )
        {
            closeActions();
            return true;
        }
        if ( _heldSlot != kNoSlot )
        {
            _heldSlot = kNoSlot;
            refresh();
            return true;
        }
        close();
        return true;
    }

    bool InventoryScreen::onUnhandledAction( const hashed_string& action )
    {
        if ( action != hashed_string( UIActionName::kContextMenu ) )
            return UIScreen::onUnhandledAction( action );
        UISystem* pUI = getUISystem();
        if ( pUI == nullptr )
            return false;
        const UIFocusManager&      focus    = pUI->getInputRouter().getFocusManager();
        const Widget*              pFocused = focus.getFocusedTree() == &getTree() ? getTree().findWidgetByID( focus.getFocusedWidget() ) : nullptr;
        const InventorySlotWidget* pSlot    = castTo<InventorySlotWidget>( pFocused );
        if ( pSlot == nullptr )
            return false;
        openActions( pSlot->getBagSlot() );
        return true;
    }

    void InventoryScreen::onTick( float32 deltaSeconds )
    {
        UIScreen::onTick( deltaSeconds );
        CharacterEquipmentComponent* const pCharacter = _character.get();
        if ( pCharacter == nullptr || pCharacter->isStarted() == false )
        {
            close(); // 캐릭터(씬 · 게임 인스턴스 소유)가 먼저 사라지거나 끝났다 — 화면도 함께 닫는다(언리얼: 위젯이 약한 포인터로 대상을 잃으면 스스로 내린다)
            return;
        }
        if ( _bBuilt == SW_FALSE )
            return;
        if ( pCharacter->getBag().getRevision() != _shownBagRevision || pCharacter->getEquipment().getRevision() != _shownEquipmentRevision )
            refresh();
        refreshDetails();
        // 길게 누르기(터치 · 마우스): 입력 맵의 hold 문턱을 넘도록 누르고 있으면 동작 메뉴를 연다.
        UISystem*       pUI  = getUISystem();
        const InputMap* pMap = pUI != nullptr ? pUI->getInputRouter().getUIInputMap() : nullptr;
        if ( _pressedSlot == kNoSlot || _bLongPressFired == SW_TRUE || pMap == nullptr )
            return;
        _pressSeconds += deltaSeconds;
        if ( _pressSeconds >= pMap->getHoldThreshold() )
        {
            _bLongPressFired = SW_TRUE;
            openActions( _pressedSlot );
        }
    }
} // namespace sw

namespace sw
{
    namespace
    {
        struct InventoryScreenWatchInternal
        {
            static const InventoryScreen* findTopInventory( AutomationWatchValue& outValue )
            {
                const UISystem* pUI = game::findSubsystem<UISystem>();
                if ( pUI != nullptr )
                {
                    const vector<unique_ptr<UIScreen>>& listScreen = pUI->getScreens();
                    for ( auto iter = listScreen.rbegin(); iter != listScreen.rend(); ++iter )
                    {
                        const InventoryScreen* pScreen = castTo<InventoryScreen>( iter->get() );
                        if ( pScreen != nullptr && pScreen->isClosing() == false )
                            return pScreen;
                    }
                }
                outValue.setText( "no inventory screen is open" );
                return nullptr;
            }

            /** @brief 감시 값 `Inventory.HeldSlot` — 집은 칸 번호(없으면 -1)입니다. */
            [[nodiscard]] static bool readHeldSlot( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const InventoryScreen* pScreen = findTopInventory( outValue );
                if ( pScreen == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pScreen->getHeldSlot() ) );
                return true;
            }

            /** @brief 감시 값 `Inventory.MenuSlot` — 동작 메뉴가 열린 칸 번호(닫혔으면 -1)입니다. */
            [[nodiscard]] static bool readMenuSlot( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const InventoryScreen* pScreen = findTopInventory( outValue );
                if ( pScreen == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pScreen->getMenuSlot() ) );
                return true;
            }

            /** @brief 감시 값 `Inventory.LastResult` — 마지막 명령 결과 이름(`InventoryCommandResult`)이고, 명령 전이면 `none` 입니다. */
            [[nodiscard]] static bool readLastResult( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const InventoryScreen* pScreen = findTopInventory( outValue );
                if ( pScreen == nullptr )
                    return false;
                outValue.setText( pScreen->getCommandCount().get() == 0 ? "none" : getEnumName( pScreen->getLastResult() ) );
                return true;
            }

            /** @brief 감시 값 `Inventory.CommandCount` — 화면이 낸 명령 수입니다(성공 · 실패 모두). */
            [[nodiscard]] static bool readCommandCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const InventoryScreen* pScreen = findTopInventory( outValue );
                if ( pScreen == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pScreen->getCommandCount().get() ) );
                return true;
            }

            /** @brief 감시 값 `Inventory.Filter` — 거르는 분류이고, 모두면 `all` 입니다. */
            [[nodiscard]] static bool readFilter( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const InventoryScreen* pScreen = findTopInventory( outValue );
                if ( pScreen == nullptr )
                    return false;
                outValue.setText( pScreen->getFilter().empty() ? "all" : pScreen->getFilter().c_str() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( inventoryHeldSlot, "Inventory.HeldSlot", "Bag slot the open inventory screen holds (picked, waiting for a place); -1 without one",
                               &InventoryScreenWatchInternal::readHeldSlot, false, false );
    SW_AUTOMATION_QUERY_WATCH( inventoryMenuSlot, "Inventory.MenuSlot", "Bag slot whose action menu the open inventory screen shows; -1 when closed", &InventoryScreenWatchInternal::readMenuSlot, false,
                               false );
    SW_AUTOMATION_QUERY_WATCH( inventoryLastResult, "Inventory.LastResult", "Result name of the last command of the open inventory screen (InventoryCommandResult); none before the first",
                               &InventoryScreenWatchInternal::readLastResult, false, false );
    SW_AUTOMATION_QUERY_WATCH( inventoryCommandCount, "Inventory.CommandCount", "Commands the open inventory screen ran (succeeded or failed)", &InventoryScreenWatchInternal::readCommandCount, false,
                               false );
    SW_AUTOMATION_QUERY_WATCH( inventoryFilter, "Inventory.Filter", "Item category the open inventory screen filters by; all when none", &InventoryScreenWatchInternal::readFilter, false, false );
} // namespace sw
