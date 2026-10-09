#include "pch.h"

#include "Editor/Panels/UserSettingsPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Gui/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/EditorPanelManager.h"

#include "Engine/Input/InputManager.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( UserSettingsPanel, "user_settings", EditorPanelCategory::Tool, 1900 );

    UserSettingsPanel::UserSettingsPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _selectedCategory{ 0 }
        , _lastRejectedCount{ 0 }
    {
    }

    void UserSettingsPanel::drawContent()
    {
        UserSettingsManager* pSettings = editor::getService<UserSettingsManager>();
        if ( pSettings == nullptr || pSettings->getCategories().empty() )
        {
            EditorWidgets::drawEmptyHint( "User settings are not loaded." );
            return;
        }
        UserSettingsManager& settings = *pSettings;
        drawToolbar( settings );
        ImGui::Separator();

        const vector<UserSettingCategoryDef>& listCategory = settings.getCategories();
        if ( ImGui::BeginTabBar( "##UserSettingsTabs" ) == false )
            return;
        vector<const UserSettingDef*> listSetting;
        for ( uint32 categoryIndex = 0; categoryIndex < static_cast<uint32>( listCategory.size() ); ++categoryIndex )
        {
            const UserSettingCategoryDef& category = listCategory[categoryIndex];
            if ( ImGui::BeginTabItem( category._id.c_str() ) == false )
                continue;
            _selectedCategory = categoryIndex;
            settings.collectSettings( category._id, listSetting );
            if ( ImGui::BeginTable( "##UserSettingsTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY ) )
            {
                ImGui::TableSetupColumn( "Setting", ImGuiTableColumnFlags_WidthFixed, 240.0f * EditorThemeUtil::getDpiScale() );
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
            if ( ImGui::Button( "Apply" ) )
                _lastRejectedCount = settings.applyPending()._failedCount; // 저장 실패 · 거절된 설정 이름은 매니저가 경고로 남긴다
            ImGui::SameLine();
            if ( ImGui::Button( "Revert" ) )
                settings.revertPending();
            if ( bPending == false )
                ImGui::EndDisabled();

            ImGui::SameLine();
            const vector<UserSettingCategoryDef>& listCategory = settings.getCategories();
            if ( ImGui::Button( "Reset Tab" ) && _selectedCategory < static_cast<uint32>( listCategory.size() ) )
                settings.resetCategoryToDefaults( listCategory[_selectedCategory]._id );

            if ( settings.isRestartRequired() )
            {
                ImGui::SameLine();
                EditorWidgets::drawPanelStatus( "Restart required" );
            }
            if ( _lastRejectedCount > 0 )
            {
                StringBuilder<constant::kMaxBuffer64> status;
                status.appendFormat( "%# setting(s) rejected - see the log", _lastRejectedCount );
                ImGui::SameLine();
                EditorWidgets::drawPanelStatus( status.c_str() );
            }
        }
        EditorChrome::endToolbar();

        // 화면 방식 · 해상도 확인 — 게임 메뉴의 "이 화면을 유지할까요? N 초" 대화상자 자리다.
        if ( settings.isAwaitingConfirm() )
        {
            ImGui::Text( "Keep these display settings? Reverting in %.0f s", static_cast<float64>( settings.getConfirmSecondsLeft() ) );
            ImGui::SameLine();
            if ( ImGui::Button( "Keep" ) )
                settings.confirmChanges();
            ImGui::SameLine();
            if ( ImGui::Button( "Revert Now" ) )
                settings.update( settings.getConfirmSecondsLeft() );
        }
    }

    void UserSettingsPanel::drawSettingRow( UserSettingsManager& settings, const UserSettingDef& def )
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool bPending = settings.isPending( def._id );
        ImGui::TextUnformatted( def._id.c_str() );
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
            case UserSettingType::Bool:
            {
                bool bValue = settings.getBoolValue( def._id );
                if ( ImGui::Checkbox( "##value", &bValue ) )
                    (void)settings.setPendingBoolValue( def._id, bValue );
                break;
            }
            case UserSettingType::Int:
            {
                int32 number = settings.getIntValue( def._id );
                if ( ImGui::SliderInt( "##value", &number, static_cast<int32>( def._minValue ), static_cast<int32>( def._maxValue ) ) )
                    (void)settings.setPendingIntValue( def._id, number );
                break;
            }
            case UserSettingType::Float:
            {
                float32 number = settings.getFloatValue( def._id );
                if ( ImGui::SliderFloat( "##value", &number, static_cast<float32>( def._minValue ), static_cast<float32>( def._maxValue ), "%.2f" ) )
                    (void)settings.setPendingFloatValue( def._id, number );
                break;
            }
            case UserSettingType::Enum:
            {
                vector<UserSettingOption> listOption;
                settings.collectOptions( def._id, listOption );
                if ( ImGui::BeginCombo( "##value", value.empty() ? "(default)" : value.c_str() ) )
                {
                    for ( const UserSettingOption& option : listOption )
                    {
                        if ( ImGui::Selectable( option._value.c_str(), option._value == value ) )
                            (void)settings.setPendingValue( def._id, option._value );
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

        if ( bEnabled == false )
            ImGui::EndDisabled();
        ImGui::PopID();
    }
} // namespace sw::editor
