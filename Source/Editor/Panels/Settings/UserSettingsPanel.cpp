#include "pch.h"

#include "Editor/Panels/Settings/UserSettingsPanel.h"

#include "Core/Log/LogOnce.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Settings/Model/UserSettingsViewModel.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct UserSettingsPanelInternal
        {
            /** @brief 스키마의 현지화 키(`text`)가 가리키는 이름입니다. 키가 없는 항목은 id 를 그대로 보인다(게임 옵션 메뉴와 같은 규칙 — `OptionsMenuScreen`). */
            static const utf8* resolveText( const string& textKey, const hashed_string& id ) { return textKey.empty() ? id.c_str() : LocalizedText::find( textKey, textKey.c_str() ); }
            static const utf8* resolveText( const string& textKey, const string& value ) { return textKey.empty() ? value.c_str() : LocalizedText::find( textKey, textKey.c_str() ); }

            /** @brief 적용 · 되돌리기를 기다리는 보류 값이 있으면 1 입니다(감시 값 `Editor.UserSettingsPending`). 설정 매니저가 없으면 값을 내지 않는다. */
            [[nodiscard]] static bool readPending( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const UserSettingsManager* pSettings = UserSettingsViewModel::findSettings();
                if ( pSettings == nullptr )
                    return false;
                outValue = pSettings->hasPendingChanges() ? 1.0 : 0.0;
                return true;
            }

            /** @brief @p pPrefix 뒤에 @p id 를 붙인 표식을 남깁니다(시나리오가 탭 · 설정 칸을 누른다). */
            static void recordMark( const utf8* pPrefix, const utf8* pID )
            {
                fixed_string<constant::buffer::kMax128> mark;
                formatstring( mark.data(), mark.capacity(), "%#%#", pPrefix, pID );
                EditorSelfTestMarks::record( mark.c_str() );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( UserSettingsPanel, UserSettingsPanel::kPanelID, EditorPanelCategory::Tool, 1900 );
    SW_AUTOMATION_WATCH( editorUserSettingsPending, "Editor.UserSettingsPending", "1 while user settings hold pending values that Apply or Revert has not settled",
                         &UserSettingsPanelInternal::readPending );

    UserSettingsPanel::UserSettingsPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _selectedCategory{ 0 }
        , _lastRejectedCount{ 0 }
    {
    }

    void UserSettingsPanel::drawContent()
    {
        UserSettingsManager& settings = UserSettingsViewModel::getSettings();
        if ( settings.getCategories().empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSettings", "UserSettingsNotLoaded" ) );
            return;
        }
        drawToolbar( settings );
        ImGui::Separator();

        const vector<UserSettingCategoryDef>& listCategory = settings.getCategories();
        // 범주는 데이터(스키마)라 늘 수 있다 — 폭을 넘긴 탭도 목록 단추로 고를 수 있게 한다(언리얼 탭 넘침 목록과 같다).
        if ( ImGui::BeginTabBar( "##UserSettingsTabs", ImGuiTabBarFlags_TabListPopupButton ) == false )
            return;
        vector<const UserSettingDef*> listSetting;
        for ( uint32 categoryIndex = 0; categoryIndex < static_cast<uint32>( listCategory.size() ); ++categoryIndex )
        {
            const UserSettingCategoryDef& category = listCategory[categoryIndex];
            // 이름은 게임 옵션 메뉴와 같은 데이터(스키마의 `text` 현지화 키)다 — 예전에는 id(`display`)를 그대로 보였다. id 는 탭 ID 로 남는다.
            const string tabLabel = string( getCategoryLabel( category ) ) + "###" + category._id.c_str();
            const bool   bTabOpen = ImGui::BeginTabItem( tabLabel.c_str() );
            UserSettingsPanelInternal::recordMark( "userSettings.tab.", category._id.c_str() );
            if ( bTabOpen == false )
                continue;
            _selectedCategory = categoryIndex;
            settings.collectSettings( category._id, listSetting );
            if ( ImGui::BeginTable( "##UserSettingsTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY ) )
            {
                ImGui::TableSetupColumn( "Setting", ImGuiTableColumnFlags_WidthFixed, 240.0f * EditorThemeUtil::getDPIScale() );
                ImGui::TableSetupColumn( "Value", ImGuiTableColumnFlags_WidthStretch );
                for ( const UserSettingDef* pDef : listSetting )
                {
                    drawSettingRow( settings, *pDef );
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    void UserSettingsPanel::drawToolbar( UserSettingsManager& settings )
    {
        if ( EditorChrome::beginToolbar( "##UserSettingsToolbar" ) )
        {
            const bool bPending = settings.hasPendingChanges();
            if ( bPending == false )
                ImGui::BeginDisabled();
            if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Apply" ) ) )
                _lastRejectedCount = settings.applyPending()._failedCount; // 저장 실패 · 거절된 설정 이름은 매니저가 경고로 남긴다
            EditorSelfTestMarks::record( "userSettings.apply" );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Revert" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Revert" ) ) )
                settings.revertPending();
            EditorSelfTestMarks::record( "userSettings.revert" );
            if ( bPending == false )
                ImGui::EndDisabled();

            ImGui::SameLine();
            const vector<UserSettingCategoryDef>& listCategory = settings.getCategories();
            if ( ImGui::Button( SW_LOCKEY( "EdText", "ResetTab" ) ) && _selectedCategory < static_cast<uint32>( listCategory.size() ) )
                settings.resetCategoryToDefaults( listCategory[_selectedCategory]._id );
            EditorSelfTestMarks::record( "userSettings.resetTab" );

            if ( settings.isRestartRequired() )
            {
                ImGui::SameLine();
                EditorWidgets::drawPanelStatus( SW_LOCKEY( "EdWidget", "RestartRequired" ) );
            }
            if ( _lastRejectedCount > 0 )
            {
                StringBuilder<constant::buffer::kMax64> status;
                status.appendFormat( "%# setting(s) rejected - see the log", _lastRejectedCount );
                ImGui::SameLine();
                EditorWidgets::drawPanelStatus( status.c_str() );
            }
        }
        EditorChrome::endToolbar();

        // 화면 방식 · 해상도 확인 — 게임 메뉴의 "이 화면을 유지할까요? N 초" 대화상자에 해당한다.
        if ( settings.isAwaitingConfirm() )
        {
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "KeepDisplaySettings", TextArgumentList().addInteger( "seconds", static_cast<int64>( settings.getConfirmSecondsLeft() + 0.5f ) ) ).c_str() );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Keep" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "Keep" ) ) )
                settings.confirmChanges();
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "RevertNow" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "RevertNow" ) ) )
                settings.update( settings.getConfirmSecondsLeft() );
        }
    }

    const utf8* UserSettingsPanel::getCategoryLabel( const UserSettingCategoryDef& category )
    {
        return UserSettingsPanelInternal::resolveText( category._textKey, category._id );
    }

    void UserSettingsPanel::drawSettingRow( UserSettingsManager& settings, const UserSettingDef& def )
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool bPending = settings.isPending( def._id );
        ImGui::TextUnformatted( UserSettingsPanelInternal::resolveText( def._textKey, def._id ) ); // 예전에는 키(`display.windowMode`)
        EditorWidgets::drawTooltip( def._id.c_str() );
        if ( bPending )
        {
            ImGui::SameLine();
            ImGui::TextUnformatted( "*" );
        }
        if ( def._descriptionKey.empty() == false )
            EditorWidgets::drawTooltip( def._descriptionKey.c_str() );

        ImGui::TableNextColumn();
        ImGui::PushID( def._id.c_str() );
        const bool bEnabled = settings.isSettingEnabled( def._id );
        if ( bEnabled == false )
            ImGui::BeginDisabled();

        const string value( settings.getValue( def._id ) );
        ImGui::SetNextItemWidth( -1.0f );
        switch ( def._type )
        {
            // 아래 세 칸의 핸들은 스키마에서 꺼낸 정의(`def`)로 풀어 언제나 풀린다 — 못 풀면 `resolveKey` 가 오류를 남기고 칸을 그리지 않는다.
            case UserSettingType::Bool:
            {
                const optional<UserSettingKey<bool>> key = settings.resolveKey<bool>( def._id );
                if ( key.has_value() == false )
                    break;
                bool bValue = settings.getBoolValue( *key );
                if ( ImGui::Checkbox( "##value", &bValue ) )
                    (void)settings.setPendingBoolValue( def._id, bValue );
                break;
            }
            case UserSettingType::Int:
            {
                const optional<UserSettingKey<int32>> key = settings.resolveKey<int32>( def._id );
                if ( key.has_value() == false )
                    break;
                int32 number = settings.getIntValue( *key );
                if ( ImGui::SliderInt( "##value", &number, static_cast<int32>( def._minValue ), static_cast<int32>( def._maxValue ) ) )
                    (void)settings.setPendingIntValue( def._id, number );
                break;
            }
            case UserSettingType::Float:
            {
                const optional<UserSettingKey<float32>> key = settings.resolveKey<float32>( def._id );
                if ( key.has_value() == false )
                    break;
                float32 number = settings.getFloatValue( *key );
                if ( ImGui::SliderFloat( "##value", &number, static_cast<float32>( def._minValue ), static_cast<float32>( def._maxValue ), "%.2f" ) )
                    (void)settings.setPendingFloatValue( def._id, number );
                break;
            }
            case UserSettingType::Enum:
            {
                vector<UserSettingOption> listOption;
                settings.collectOptions( def._id, listOption );
                // 선택지도 스키마의 현지화 이름(`Borderless`)으로 보인다 — 예전에는 저장 값(`borderless`)이었다. 저장 · 비교는 값으로 한다.
                const utf8* pPreview = value.empty() ? "(default)" : value.c_str();
                for ( const UserSettingOption& option : listOption )
                {
                    if ( option._value == value )
                        pPreview = UserSettingsPanelInternal::resolveText( option._textKey, option._value );
                }
                if ( ImGui::BeginCombo( "##value", pPreview ) )
                {
                    for ( const UserSettingOption& option : listOption )
                    {
                        ImGui::PushID( option._value.c_str() );
                        if ( ImGui::Selectable( UserSettingsPanelInternal::resolveText( option._textKey, option._value ), option._value == value ) )
                            (void)settings.setPendingValue( def._id, option._value );
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                break;
            }
            case UserSettingType::KeyBinding:
            {
                // 키를 눌러 받는 창은 게임 메뉴의 일이다. 여기서는 슬롯 글(`Key.Space`)을 적고 겹치면 맞바꾼다.
                const string glyph = settings.getBindingGlyph( def._id, InputGlyphStyle::KeyboardMouse );
                ImGui::TextUnformatted( glyph.c_str() );
                ImGui::SameLine();
                string text = value;
                if ( EditorWidgets::drawTextField( "##slot", text ) )
                    (void)settings.setPendingBinding( def._id, text, UserSettingBindingPolicy::Swap );
                break;
            }
            case UserSettingType::String:
            {
                string text = value;
                if ( EditorWidgets::drawTextField( "##value", text ) )
                    (void)settings.setPendingValue( def._id, text );
                break;
            }
        }

        UserSettingsPanelInternal::recordMark( "userSettings.value.", def._id.c_str() ); // 값 칸(종류별 위젯)의 자리
        if ( bEnabled == false )
            ImGui::EndDisabled();
        ImGui::PopID();
    }
} // namespace sw::editor
