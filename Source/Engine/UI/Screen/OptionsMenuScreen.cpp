#include "pch.h"

#include "Engine/UI/Screen/OptionsMenuScreen.h"

#include "Core/Container/string.h"
#include "Core/Log/Logger.h"

#include "Engine/Input/InputManager.h"
#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Base/UiEvents.h"
#include "Engine/UI/Document/UiDocumentCache.h"
#include "Engine/UI/Document/UiDocumentLoader.h"
#include "Engine/UI/Screen/KeyRebindScreen.h"
#include "Engine/UI/Screen/SettingsConfirmScreen.h"
#include "Engine/UI/Screen/UiNotificationService.h"
#include "Engine/UI/UiSystem.h"
#include "Engine/UI/Widgets/ButtonWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"
#include "Engine/UserSettings/UserSettingsManager.h"

SW_TEST_GLOBAL_VARIABLE_SHIPPED( bool, gv_uiOptionsMenu, false, "옵션 메뉴를 띄운다 — 사용자 설정 스키마에서 만든 탭 · 행(개발 확인 · 스크린샷)" );

namespace sw
{
    SW_LOG_CALLER( "OptionsMenuScreen" );

    namespace
    {
        struct OptionsMenuScreenInternal
        {
            static constexpr utf8 kTabsName[]          = "Tabs";
            static constexpr utf8 kRowsName[]          = "Rows";
            static constexpr utf8 kRestartNoticeName[] = "RestartNotice";
            static constexpr utf8 kValueName[]         = "Value";
            static constexpr utf8 kValueTextName[]     = "ValueText";
            static constexpr utf8 kLabelName[]         = "Label";
            static constexpr utf8 kGlyphName[]         = "Glyph";
            static constexpr utf8 kTabClass[]          = "tab";
            static constexpr utf8 kSelectedTabClass[]  = "tab selected";

            /** @brief @p widget 과 그 자손의 이름을 `<접두>.<이름>` 으로 감쌉니다(같은 견본을 행마다 써도 이름이 겹치지 않게). */
            static void prefixNames( Widget& widget, const string& prefix )
            {
                if ( widget.getName().empty() == false )
                    widget.setName( hashed_string( prefix + "." + widget.getName().c_str() ) );
                PanelWidget* pPanel = castTo<PanelWidget>( &widget );
                if ( pPanel == nullptr )
                    return;
                for ( uint32 index = 0; index < pPanel->getChildCount(); ++index )
                {
                    prefixNames( *pPanel->getChild( index ), prefix );
                }
            }

            /** @brief 값 위젯에 거는 칸 — 형식마다 위젯의 값 칸(6-3 설정 바인딩의 규약)입니다. 키 바인딩은 단추라 칸이 없습니다. */
            static const utf8* findValueProperty( UserSettingType type )
            {
                switch ( type )
                {
                    case UserSettingType::Bool:
                        return "_bChecked";
                    case UserSettingType::Int:
                    case UserSettingType::Float:
                        return "_value";
                    case UserSettingType::Enum:
                        return "_selectedIndex";
                    case UserSettingType::String:
                        return "_text";
                    case UserSettingType::KeyBinding:
                        return nullptr;
                }
                return nullptr;
            }
        };
    } // namespace

    OptionsMenuScreen::OptionsMenuScreen( const UiScreenDesc& desc, unique_ptr<Widget> root )
        : UiScreen{ desc, std::move( root ) }
        , _listCategory{}
        , _listTabButton{}
        , _listRow{}
        , _pSettings{ nullptr }
        , _promptScreen{ kInvalidUiScreenHandle }
        , _selectedTab{ 0 }
    {
    }

    OptionsMenuScreen::~OptionsMenuScreen() = default;

    UiScreenHandle OptionsMenuScreen::open( UiSystem& ui )
    {
        UserSettingsManager* pSettings = ui.findUserSettings();
        if ( pSettings == nullptr )
        {
            SW_LOG_ERROR( "[Ui] Options menu needs the user settings service" );
            return kInvalidUiScreenHandle;
        }
        const UiScreenHandle handle  = ui.openScreen<OptionsMenuScreen>( ui.getOptionsMenuDocument() );
        UiScreen*            pScreen = ui.findScreen( handle );
        if ( pScreen == nullptr )
            return kInvalidUiScreenHandle;
        OptionsMenuScreen& screen = static_cast<OptionsMenuScreen&>( *pScreen );
        screen._pSettings         = pSettings;
        screen.rebuild();
        return handle;
    }

    const utf8* OptionsMenuScreen::findRowDocument( const UserSettingDef& setting )
    {
        switch ( setting._type )
        {
            case UserSettingType::Bool:
                return "engine/ui/parts/setting_bool.ui.xml";
            case UserSettingType::Int:
                return "engine/ui/parts/setting_int.ui.xml";
            case UserSettingType::Float:
                return "engine/ui/parts/setting_float.ui.xml";
            case UserSettingType::Enum:
                return "engine/ui/parts/setting_enum.ui.xml";
            case UserSettingType::KeyBinding:
                return "engine/ui/parts/setting_keybinding.ui.xml";
            case UserSettingType::String:
                return "engine/ui/parts/setting_string.ui.xml";
        }
        return "engine/ui/parts/setting_string.ui.xml";
    }

    bool OptionsMenuScreen::onCommand( const hashed_string& command, Widget& source )
    {
        if ( command == hashed_string( "Apply" ) )
        {
            apply();
            return true;
        }
        if ( command == hashed_string( "Revert" ) )
        {
            if ( _pSettings != nullptr )
                _pSettings->revertPending();
            return true;
        }
        if ( command == hashed_string( "Defaults" ) )
        {
            if ( _pSettings != nullptr && _selectedTab < _listCategory.size() )
                _pSettings->resetCategoryToDefaults( _listCategory[_selectedTab] );
            return true;
        }
        if ( command == hashed_string( "Close" ) )
        {
            requestClose();
            return true;
        }
        if ( command == hashed_string( "SelectTab" ) )
        {
            for ( uint32 index = 0; index < static_cast<uint32>( _listTabButton.size() ); ++index )
            {
                if ( _listTabButton[index] == source.getId() )
                    selectTab( index );
            }
            return true;
        }
        if ( command == hashed_string( "Rebind" ) )
        {
            UiSystem* pUi = getUiSystem();
            for ( const RowEntry& row : _listRow )
            {
                if ( row._value == source.getId() && pUi != nullptr && _pSettings != nullptr && isPromptOpen() == false )
                    _promptScreen = KeyRebindScreen::open( *pUi, *_pSettings, row._settingId, kRebindDocument );
            }
            return true;
        }
        return UiScreen::onCommand( command, source );
    }

    bool OptionsMenuScreen::onBack()
    {
        requestClose();
        return true;
    }

    bool OptionsMenuScreen::onUnhandledAction( const hashed_string& action )
    {
        const uint32 tabCount = getTabCount();
        if ( tabCount == 0 )
            return false;
        if ( action == hashed_string( UiActionName::kTabNext ) )
        {
            selectTab( ( _selectedTab + 1 ) % tabCount );
            return true;
        }
        if ( action == hashed_string( UiActionName::kTabPrevious ) )
        {
            selectTab( ( _selectedTab + tabCount - 1 ) % tabCount );
            return true;
        }
        return false;
    }

    void OptionsMenuScreen::onTick( float32 deltaSeconds )
    {
        (void)deltaSeconds;
        refreshGlyphs();
        refreshRestartNotice();
    }

    void OptionsMenuScreen::onTreeRebuilt()
    {
        // 문서가 다시 지어졌다 — 탭 · 행은 코드로 붙인 것이라 새 트리에 다시 짓는다.
        rebuild();
    }

    void OptionsMenuScreen::selectTab( uint32 index )
    {
        if ( index >= getTabCount() )
            return;
        _selectedTab     = index;
        WidgetTree& tree = getTree();
        for ( uint32 tabIndex = 0; tabIndex < static_cast<uint32>( _listTabButton.size() ); ++tabIndex )
        {
            Widget* pButton = tree.findWidgetById( _listTabButton[tabIndex] );
            if ( pButton != nullptr )
                pButton->setStyleClass( tabIndex == index ? OptionsMenuScreenInternal::kSelectedTabClass : OptionsMenuScreenInternal::kTabClass );
        }
        buildRows();
        // 탐색 방식이면 첫 행으로 — 탭을 바꾼 패드 사용자가 바로 행을 고른다.
        UiSystem* pUi = getUiSystem();
        if ( pUi == nullptr || pUi->getInputMode() != UiInputMode::Navigation || _listRow.empty() || pUi->getActiveScreen() != this )
            return;
        (void)pUi->getFocusManager().setFocus( tree, _listRow.front()._value );
    }

    Widget* OptionsMenuScreen::findRowValueWidget( const hashed_string& settingId ) const
    {
        for ( const RowEntry& row : _listRow )
        {
            if ( row._settingId == settingId )
                return getTree().findWidgetById( row._value );
        }
        return nullptr;
    }

    void OptionsMenuScreen::apply()
    {
        if ( _pSettings == nullptr )
            return;
        const UserSettingsApplyResult result = _pSettings->applyPending();
        UiSystem*                     pUi    = getUiSystem();
        if ( result._bAwaitingConfirm && pUi != nullptr && isPromptOpen() == false )
            _promptScreen = SettingsConfirmScreen::open( *pUi, *_pSettings, kConfirmDocument );
        if ( result._bNeedsRestart && pUi != nullptr )
        {
            UiNotificationDesc notice{};
            notice._text            = "Some changes take effect after a restart.";
            notice._durationSeconds = 6.0f;
            notice._kind            = UiNotificationKind::Warning;
            pUi->getNotifications().post( notice );
        }
        refreshRestartNotice();
    }

    void OptionsMenuScreen::requestClose()
    {
        if ( _pSettings != nullptr && _pSettings->hasPendingChanges() )
        {
            openUnsavedPrompt();
            return;
        }
        close();
    }

    void OptionsMenuScreen::rebuild()
    {
        WidgetTree&  tree  = getTree();
        PanelWidget* pTabs = tree.findWidget<PanelWidget>( OptionsMenuScreenInternal::kTabsName );
        _listCategory.clear();
        _listTabButton.clear();
        if ( _pSettings == nullptr || pTabs == nullptr )
        {
            SW_LOG_ERROR( "[Ui] Options menu document %# has no '%#' panel", getDocumentPath().c_str(), OptionsMenuScreenInternal::kTabsName );
            return;
        }
        pTabs->clearChildren();
        vector<const UserSettingDef*> listSetting;
        for ( const UserSettingCategoryDef& category : _pSettings->getCategories() )
        {
            listSetting.clear();
            _pSettings->collectSettings( category._id, listSetting );
            if ( listSetting.empty() )
                continue; // 이 게임 · 플랫폼에 보일 설정이 없는 탭은 뺀다
            unique_ptr<ButtonWidget> button = make_unique<ButtonWidget>();
            button->setName( hashed_string( string( "Tab." ) + category._id.c_str() ) );
            button->setStyleClass( OptionsMenuScreenInternal::kTabClass );
            button->setCommand( "SelectTab" );
            unique_ptr<TextWidget> label = make_unique<TextWidget>();
            label->setText( category._textKey.empty() ? string_view( category._id.c_str() ) : string_view( category._textKey ) );
            (void)button->addChild( std::move( label ) );
            _listTabButton.push_back( button->getId() );
            _listCategory.push_back( category._id );
            (void)pTabs->addChild( std::move( button ) );
        }
        selectTab( _selectedTab < getTabCount() ? _selectedTab : 0 );
    }

    void OptionsMenuScreen::buildRows()
    {
        PanelWidget* pRows = getTree().findWidget<PanelWidget>( OptionsMenuScreenInternal::kRowsName );
        if ( pRows == nullptr || _pSettings == nullptr )
        {
            SW_LOG_ERROR( "[Ui] Options menu document %# has no '%#' panel", getDocumentPath().c_str(), OptionsMenuScreenInternal::kRowsName );
            return;
        }
        pRows->clearChildren();
        _listRow.clear();
        removeOrphanBindings(); // 지운 행의 설정 바인딩
        if ( _selectedTab >= _listCategory.size() )
            return;
        vector<const UserSettingDef*> listSetting;
        _pSettings->collectSettings( _listCategory[_selectedTab], listSetting );
        for ( const UserSettingDef* pSetting : listSetting )
        {
            (void)buildRow( *pRows, *pSetting );
        }
    }

    bool OptionsMenuScreen::buildRow( PanelWidget& rows, const UserSettingDef& setting )
    {
        using Internal = OptionsMenuScreenInternal;
        UiSystem* pUi  = getUiSystem();
        if ( pUi == nullptr )
            return false;
        const utf8*                             pDocumentPath = findRowDocument( setting );
        string                                  error;
        const shared_ptr<const UiDocumentAsset> document = pUi->getDocumentCache().findOrLoad( pDocumentPath, error );
        vector<UiBindingDesc>                   listBinding;
        unique_ptr<Widget>                      row = document != nullptr ? UiDocumentLoader::instantiate( *document, pUi->getDocumentCache(), listBinding, error ) : nullptr;
        if ( row == nullptr )
        {
            SW_LOG_ERROR( "[Ui] Options row for '%#' failed: %#", setting._id.c_str(), error.c_str() );
            return false;
        }
        const string prefix( setting._id.c_str() );
        Internal::prefixNames( *row, prefix );
        (void)rows.addChild( std::move( row ) );
        WidgetTree& tree = getTree();
        // 견본에 적힌 바인딩(게임이 덮어쓴 행 견본)도 그대로 건다.
        for ( const UiBindingDesc& binding : listBinding )
        {
            addBinding( binding );
        }

        if ( TextWidget* pLabel = tree.findWidget<TextWidget>( hashed_string( prefix + "." + Internal::kLabelName ) ); pLabel != nullptr )
            pLabel->setText( setting._textKey.empty() ? string_view( prefix ) : string_view( setting._textKey ) );
        RowEntry entry{};
        entry._settingId = setting._id;
        Widget* pValue   = tree.findWidgetByName( hashed_string( prefix + "." + Internal::kValueName ) );
        if ( pValue == nullptr )
        {
            SW_LOG_ERROR( "[Ui] Options row document %# has no '%#' widget", pDocumentPath, Internal::kValueName );
            return false;
        }
        entry._value            = pValue->getId();
        const string expression = "{setting:" + prefix + "}";
        if ( const utf8* pProperty = Internal::findValueProperty( setting._type ); pProperty != nullptr )
            addBinding( UiBindingDesc{ pProperty, expression, pValue->getId(), 0 } );
        if ( Widget* pValueText = tree.findWidgetByName( hashed_string( prefix + "." + Internal::kValueTextName ) ); pValueText != nullptr )
            addBinding( UiBindingDesc{ "_text", "{setting:" + prefix + ", mode=OneWay}", pValueText->getId(), 0 } );
        if ( Widget* pGlyph = tree.findWidgetByName( hashed_string( prefix + "." + Internal::kGlyphName ) ); pGlyph != nullptr )
            entry._glyph = pGlyph->getId();
        _listRow.push_back( entry );
        return true;
    }

    void OptionsMenuScreen::refreshGlyphs()
    {
        UiSystem* pUi = getUiSystem();
        if ( _pSettings == nullptr || pUi == nullptr )
            return;
        const InputGlyphStyle style = pUi->getInputManager() != nullptr ? pUi->getInputManager()->getActiveGlyphStyle() : InputGlyphStyle::KeyboardMouse;
        for ( const RowEntry& row : _listRow )
        {
            TextWidget* pGlyph = castTo<TextWidget>( getTree().findWidgetById( row._glyph ) );
            if ( pGlyph == nullptr )
                continue;
            const string glyph = _pSettings->getBindingGlyph( row._settingId, style );
            if ( pGlyph->getText() != glyph )
                pGlyph->setText( glyph );
            if ( Widget* pValue = getTree().findWidgetById( row._value ); pValue != nullptr )
                pValue->setEnabled( _pSettings->isSettingEnabled( row._settingId ) );
        }
    }

    void OptionsMenuScreen::refreshRestartNotice()
    {
        Widget* pNotice = getTree().findWidgetByName( OptionsMenuScreenInternal::kRestartNoticeName );
        if ( pNotice != nullptr && _pSettings != nullptr )
            pNotice->setVisibility( _pSettings->isRestartRequired() ? WidgetVisibility::Visible : WidgetVisibility::Collapsed );
    }

    void OptionsMenuScreen::openUnsavedPrompt()
    {
        UiSystem* pUi = getUiSystem();
        if ( pUi == nullptr || isPromptOpen() )
            return;
        _promptScreen     = pUi->openScreen( kUnsavedDocument );
        UiScreen* pPrompt = pUi->findScreen( _promptScreen );
        if ( pPrompt == nullptr )
            return;
        // 창의 단추는 이 메뉴를 번호로 찾는다 — 메뉴가 먼저 닫혀도(핫 리로드) 매달린 포인터가 없다.
        const UiScreenHandle menuHandle = getHandle();
        const auto           findMenu   = [pUi, menuHandle]()
        { return static_cast<OptionsMenuScreen*>( pUi->findScreen( menuHandle ) ); };
        pPrompt->registerCommand( "Apply", SW_DELEGATE_LAMBDA( UiCommandDelegate, [pPrompt, findMenu]( const hashed_string&, Widget& )
        {
            pPrompt->close();
            if ( OptionsMenuScreen* pMenu = findMenu(); pMenu != nullptr )
            {
                pMenu->apply();
                pMenu->close();
            }
        } ) );
        pPrompt->registerCommand( "Discard", SW_DELEGATE_LAMBDA( UiCommandDelegate, [pPrompt, findMenu]( const hashed_string&, Widget& )
        {
            pPrompt->close();
            if ( OptionsMenuScreen* pMenu = findMenu(); pMenu != nullptr )
            {
                if ( pMenu->_pSettings != nullptr )
                    pMenu->_pSettings->revertPending();
                pMenu->close();
            }
        } ) );
        pPrompt->registerCommand( "Cancel", SW_DELEGATE_LAMBDA( UiCommandDelegate, [pPrompt]( const hashed_string&, Widget& )
        { pPrompt->close(); } ) );
    }

    bool OptionsMenuScreen::isPromptOpen() const
    {
        const UiSystem* pUi     = getUiSystem();
        const UiScreen* pPrompt = pUi != nullptr ? pUi->findScreen( _promptScreen ) : nullptr;
        return pPrompt != nullptr && pPrompt->isClosing() == false;
    }
} // namespace sw
