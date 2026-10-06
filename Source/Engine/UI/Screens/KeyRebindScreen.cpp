#include "pch.h"

#include "Engine/UI/Screens/KeyRebindScreen.h"

#include "Core/Log/Logger.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputSlotUtil.h"
#include "Engine/UI/UiSystem.h"
#include "Engine/UI/Widgets/TextWidget.h"
#include "Engine/UserSettings/UserSettingsManager.h"

namespace sw
{
    SW_LOG_CALLER( "KeyRebindScreen" );

    KeyRebindScreen::KeyRebindScreen( const UiScreenDesc& desc, unique_ptr<Widget> root )
        : UiScreen{ desc, std::move( root ) }
        , _capturedSlotText{}
        , _settingId{}
        , _pSettings{ nullptr }
        , _openFrame{ 0 }
        , _escapeHeldSeconds{ 0.0f }
        , _bListening{ true }
        , _bEscapeHeld{ false }
    {
    }

    KeyRebindScreen::~KeyRebindScreen() = default;

    UiScreenHandle KeyRebindScreen::open( UiSystem& ui, UserSettingsManager& settings, const hashed_string& settingId, string_view documentPath )
    {
        const UiScreenHandle handle  = ui.openScreen<KeyRebindScreen>( documentPath );
        UiScreen*            pScreen = ui.findScreen( handle );
        if ( pScreen == nullptr )
            return kInvalidUiScreenHandle;
        KeyRebindScreen& screen = static_cast<KeyRebindScreen&>( *pScreen );
        screen._pSettings       = &settings;
        screen._settingId       = settingId;
        screen._openFrame       = ui.getInputManager() != nullptr ? ui.getInputManager()->getBeginFrameCount() : 0;
        return handle;
    }

    bool KeyRebindScreen::onCommand( const hashed_string& command, Widget& source )
    {
        if ( command == hashed_string( "Swap" ) )
        {
            if ( _pSettings != nullptr && _capturedSlotText.empty() == false )
                (void)_pSettings->setPendingBinding( _settingId, _capturedSlotText, UserSettingBindingPolicy::Swap );
            close();
            return true;
        }
        if ( command == hashed_string( "Cancel" ) )
        {
            close();
            return true;
        }
        return UiScreen::onCommand( command, source );
    }

    bool KeyRebindScreen::onBack()
    {
        close();
        return true;
    }

    void KeyRebindScreen::onTick( float32 deltaSeconds )
    {
        UiSystem*     pUi    = getUiSystem();
        InputManager* pInput = pUi != nullptr ? pUi->getInputManager() : nullptr;
        if ( _bListening == false || pInput == nullptr || _pSettings == nullptr )
            return;
        // 연 프레임의 입력(창을 연 확인 · 클릭)은 받지 않는다.
        if ( pInput->getBeginFrameCount() == _openFrame )
            return;
        const KeyboardDevice* pKeyboard = pInput->getKeyboard();
        if ( _bEscapeHeld )
        {
            if ( pKeyboard != nullptr && pKeyboard->isKeyDown( Key::Escape ) )
            {
                _escapeHeldSeconds += deltaSeconds;
                if ( _escapeHeldSeconds >= kEscapeHoldSeconds )
                    capture( InputSlot::fromKey( Key::Escape ) );
                return;
            }
            close(); // 1 초 전에 뗐다 — 취소
            return;
        }
        InputSlot slot{};
        if ( pInput->findFirstPressedSlot( slot ) == false )
            return;
        if ( slot == InputSlot::fromKey( Key::Escape ) )
        {
            _bEscapeHeld       = true;
            _escapeHeldSeconds = 0.0f;
            pUi->consumeSlot( slot );
            return;
        }
        capture( slot );
    }

    void KeyRebindScreen::capture( const InputSlot& slot )
    {
        UiSystem* pUi = getUiSystem();
        if ( pUi != nullptr )
            pUi->consumeSlot( slot );
        _capturedSlotText = InputSlotUtil::toText( slot );
        _bListening       = false;
        UserSettingBindingConflict conflict{};
        if ( _pSettings->findBindingConflict( _settingId, _capturedSlotText, conflict ) )
        {
            showConflict( conflict._settingId, conflict._action );
            return;
        }
        const UserSettingSetResult result = _pSettings->setPendingBinding( _settingId, _capturedSlotText, UserSettingBindingPolicy::Reject );
        if ( result != UserSettingSetResult::Accepted && result != UserSettingSetResult::Unchanged )
            SW_LOG_WARNING( "[Ui] Key binding '%#' for '%#' was not accepted", _capturedSlotText.c_str(), _settingId.c_str() );
        close();
    }

    void KeyRebindScreen::showConflict( const hashed_string& otherSettingId, const hashed_string& otherAction )
    {
        WidgetTree& tree = getTree();
        // 상대 이름 — 설정이면 그 메뉴 이름(글 키 — 글 위젯이 푼다), 스키마 밖이면 액션 이름.
        const UserSettingDef* pOther = otherSettingId.empty() ? nullptr : _pSettings->findSetting( otherSettingId );
        if ( TextWidget* pTarget = tree.findWidget<TextWidget>( "ConflictTarget" ); pTarget != nullptr )
            pTarget->setText( pOther != nullptr && pOther->_textKey.empty() == false ? string_view( pOther->_textKey ) : string_view( otherAction.c_str() ) );
        if ( Widget* pRow = tree.findWidgetByName( "ConflictRow" ); pRow != nullptr )
            pRow->setVisibility( WidgetVisibility::Visible );
        if ( Widget* pButtons = tree.findWidgetByName( "ConflictButtons" ); pButtons != nullptr )
            pButtons->setVisibility( WidgetVisibility::Visible );
        if ( Widget* pPrompt = tree.findWidgetByName( "Prompt" ); pPrompt != nullptr )
            pPrompt->setVisibility( WidgetVisibility::Collapsed );
        // 스키마 밖 액션과 겹치면 맞바꿀 상대가 없다 — 바꾸기를 숨기고 취소만.
        Widget* pSwap = tree.findWidgetByName( "Swap" );
        if ( pSwap != nullptr && pOther == nullptr )
            pSwap->setVisibility( WidgetVisibility::Collapsed );
        UiSystem* pUi = getUiSystem();
        if ( pUi == nullptr )
            return;
        Widget* pFocus = pOther != nullptr ? pSwap : tree.findWidgetByName( "Cancel" );
        if ( pFocus != nullptr )
            (void)pUi->getFocusManager().setFocus( tree, pFocus->getId() );
    }
} // namespace sw
