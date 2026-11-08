#include "pch.h"

#include "Editor/Panels/Settings/Model/UserSettingsViewModel.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Localization/LocText.h"
#include "Engine/UserSettings/UserSettingsManager.h"

namespace sw::editor
{
    namespace
    {
        struct UserSettingsViewModelInternal
        {
            /** @brief 스키마의 현지화 키(`text`)가 가리키는 이름입니다. 키가 없는 항목은 id 를 그대로 보인다(게임 옵션 메뉴와 같은 규칙 — `OptionsMenuScreen`). */
            static const utf8* resolveText( const string& textKey, const utf8* pFallback ) { return textKey.empty() ? pFallback : LocalizedText::find( textKey, textKey.c_str() ); }

            /** @brief 적용 · 되돌리기를 기다리는 보류 값이 있으면 1 입니다(감시 값 `Editor.UserSettingsPending`). 설정 매니저가 없으면 값을 내지 않는다. */
            [[nodiscard]] static bool readPending( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const UserSettingsManager* pSettings = editor::findSubsystem<UserSettingsManager>();
                if ( pSettings == nullptr )
                    return false;
                outValue = pSettings->hasPendingChanges() ? 1.0 : 0.0;
                return true;
            }

            static hashed_string toID( const string& id ) { return hashed_string{ string_view{ id } }; }

            /** @brief 설정 하나의 줄을 채웁니다. */
            static void fillRow( const UserSettingsManager& settings, const UserSettingDef& def, UserSettingRow& outRow )
            {
                outRow._id             = def._id.c_str();
                outRow._label          = resolveText( def._textKey, def._id.c_str() ); // 예전에는 키(`display.windowMode`)
                outRow._descriptionKey = def._descriptionKey;
                outRow._value          = string( settings.getValue( def._id ) );
                outRow._type           = def._type;
                outRow._minValue       = def._minValue;
                outRow._maxValue       = def._maxValue;
                outRow._bPending       = settings.isPending( def._id ) ? SW_TRUE : SW_FALSE;
                outRow._bEnabled       = settings.isSettingEnabled( def._id ) ? SW_TRUE : SW_FALSE;
                outRow._bHasKey        = SW_TRUE;
                // 아래 세 칸의 핸들은 스키마에서 꺼낸 정의(`def`)로 풀어 언제나 풀린다 — 못 풀면 `resolveKey` 가 오류를 남기고 칸을 그리지 않는다.
                switch ( def._type )
                {
                    case UserSettingType::Bool:
                    {
                        const optional<UserSettingKey<bool>> key = settings.resolveKey<bool>( def._id );
                        outRow._bHasKey                          = key.has_value() ? SW_TRUE : SW_FALSE;
                        outRow._bBoolValue                       = key.has_value() && settings.getBoolValue( *key ) ? SW_TRUE : SW_FALSE;
                        break;
                    }
                    case UserSettingType::Int:
                    {
                        const optional<UserSettingKey<int32>> key = settings.resolveKey<int32>( def._id );
                        outRow._bHasKey                           = key.has_value() ? SW_TRUE : SW_FALSE;
                        outRow._intValue                          = key.has_value() ? settings.getIntValue( *key ) : 0;
                        break;
                    }
                    case UserSettingType::Float:
                    {
                        const optional<UserSettingKey<float32>> key = settings.resolveKey<float32>( def._id );
                        outRow._bHasKey                             = key.has_value() ? SW_TRUE : SW_FALSE;
                        outRow._floatValue                          = key.has_value() ? settings.getFloatValue( *key ) : 0.0f;
                        break;
                    }
                    case UserSettingType::Enum:
                    {
                        vector<UserSettingOption> listOption;
                        settings.collectOptions( def._id, listOption );
                        // 선택지도 스키마의 현지화 이름(`Borderless`)으로 보인다 — 예전에는 저장 값(`borderless`)이었다. 저장 · 비교는 값으로 한다.
                        outRow._previewLabel = outRow._value.empty() ? "(default)" : outRow._value;
                        for ( const UserSettingOption& option : listOption )
                        {
                            UserSettingOptionRow& optionRow = outRow._listOption.emplace_back();
                            optionRow._value                = option._value;
                            optionRow._label                = resolveText( option._textKey, option._value.c_str() );
                            if ( option._value == outRow._value )
                                outRow._previewLabel = optionRow._label;
                        }
                        break;
                    }
                    case UserSettingType::KeyBinding:
                    {
                        // 키를 눌러 받는 창은 게임 메뉴의 일이다. 여기서는 슬롯 글(`Key.Space`)을 적고 겹치면 맞바꾼다.
                        outRow._glyph = settings.getBindingGlyph( def._id, InputGlyphStyle::KeyboardMouse );
                        break;
                    }
                    case UserSettingType::String:
                    {
                        break;
                    }
                }
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( editorUserSettingsPending, "Editor.UserSettingsPending", "1 while user settings hold pending values that Apply or Revert has not settled",
                         &UserSettingsViewModelInternal::readPending );

    UserSettingsViewModel::UserSettingsViewModel()
        : _lastRejectedCount{ 0 }
    {
    }

    const utf8* UserSettingsViewModel::getCategoryLabel( const UserSettingCategoryDef& category )
    {
        return UserSettingsViewModelInternal::resolveText( category._textKey, category._id.c_str() );
    }

    void UserSettingsViewModel::gather( UserSettingsViewState& outState ) const
    {
        outState                             = UserSettingsViewState{};
        const UserSettingsManager* pSettings = editor::findSubsystem<UserSettingsManager>();
        if ( pSettings == nullptr )
            return;
        const UserSettingsManager& settings = *pSettings;
        outState._bPending                  = settings.hasPendingChanges() ? SW_TRUE : SW_FALSE;
        outState._bRestartRequired          = settings.isRestartRequired() ? SW_TRUE : SW_FALSE;
        outState._bAwaitingConfirm          = settings.isAwaitingConfirm() ? SW_TRUE : SW_FALSE;
        outState._confirmSecondsLeft        = settings.getConfirmSecondsLeft();
        outState._lastRejectedCount         = _lastRejectedCount;
        vector<const UserSettingDef*> listSetting;
        for ( const UserSettingCategoryDef& category : settings.getCategories() )
        {
            UserSettingsCategoryTab& tab = outState._listCategory.emplace_back();
            tab._id                      = category._id.c_str();
            tab._label                   = getCategoryLabel( category );
            settings.collectSettings( category._id, listSetting );
            for ( const UserSettingDef* pDef : listSetting )
            {
                UserSettingsViewModelInternal::fillRow( settings, *pDef, tab._listRow.emplace_back() );
            }
        }
    }

    // 아래 명령은 사용자 설정(사용자 환경 파일)의 보류 값을 바꿀 뿐이라 에디터 되돌리기에 남기지 않는다(머리말).
    void UserSettingsViewModel::setPendingValue( const string& id, const string& value )
    {
        if ( UserSettingsManager* pSettings = editor::findSubsystem<UserSettingsManager>() )
            (void)pSettings->setPendingValue( UserSettingsViewModelInternal::toID( id ), value );
    }

    void UserSettingsViewModel::setPendingBool( const string& id, bool bValue )
    {
        if ( UserSettingsManager* pSettings = editor::findSubsystem<UserSettingsManager>() )
            (void)pSettings->setPendingBoolValue( UserSettingsViewModelInternal::toID( id ), bValue );
    }

    void UserSettingsViewModel::setPendingInt( const string& id, int32 value )
    {
        if ( UserSettingsManager* pSettings = editor::findSubsystem<UserSettingsManager>() )
            (void)pSettings->setPendingIntValue( UserSettingsViewModelInternal::toID( id ), value );
    }

    void UserSettingsViewModel::setPendingFloat( const string& id, float32 value )
    {
        if ( UserSettingsManager* pSettings = editor::findSubsystem<UserSettingsManager>() )
            (void)pSettings->setPendingFloatValue( UserSettingsViewModelInternal::toID( id ), value );
    }

    void UserSettingsViewModel::setPendingBinding( const string& id, const string& slot )
    {
        if ( UserSettingsManager* pSettings = editor::findSubsystem<UserSettingsManager>() )
            (void)pSettings->setPendingBinding( UserSettingsViewModelInternal::toID( id ), slot, UserSettingBindingPolicy::Swap );
    }

    void UserSettingsViewModel::applyPending()
    {
        if ( UserSettingsManager* pSettings = editor::findSubsystem<UserSettingsManager>() )
            _lastRejectedCount = pSettings->applyPending()._failedCount; // 저장 실패 · 거절된 설정 이름은 매니저가 경고로 남긴다
    }

    void UserSettingsViewModel::revertPending()
    {
        if ( UserSettingsManager* pSettings = editor::findSubsystem<UserSettingsManager>() )
            pSettings->revertPending();
    }

    void UserSettingsViewModel::resetCategory( const string& categoryID )
    {
        if ( UserSettingsManager* pSettings = editor::findSubsystem<UserSettingsManager>() )
            pSettings->resetCategoryToDefaults( UserSettingsViewModelInternal::toID( categoryID ) );
    }

    void UserSettingsViewModel::confirmChanges()
    {
        if ( UserSettingsManager* pSettings = editor::findSubsystem<UserSettingsManager>() )
            pSettings->confirmChanges();
    }

    void UserSettingsViewModel::revertConfirmNow()
    {
        if ( UserSettingsManager* pSettings = editor::findSubsystem<UserSettingsManager>() )
            pSettings->update( pSettings->getConfirmSecondsLeft() );
    }
} // namespace sw::editor
