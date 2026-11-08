#include "pch.h"

#include "Editor/Panels/Settings/UserSettingsPanel.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/formatString.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct UserSettingsPanelInternal
        {
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

    UserSettingsPanel::UserSettingsPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _viewModel{}
        , _viewState{}
        , _selectedCategory{ 0 }
    {
    }

    void UserSettingsPanel::drawContent()
    {
        _viewModel.gather( _viewState );
        if ( _viewState._listCategory.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSettings", "UserSettingsNotLoaded" ) );
            return;
        }
        drawToolbar();
        ImGui::Separator();

        // 범주는 데이터(스키마)라 늘 수 있다 — 폭을 넘긴 탭도 목록 단추로 고를 수 있게 한다(언리얼 탭 넘침 목록과 같다).
        if ( ImGui::BeginTabBar( "##UserSettingsTabs", ImGuiTabBarFlags_TabListPopupButton ) == false )
            return;
        for ( uint32 categoryIndex = 0; categoryIndex < static_cast<uint32>( _viewState._listCategory.size() ); ++categoryIndex )
        {
            const UserSettingsCategoryTab& category = _viewState._listCategory[categoryIndex];
            // 이름은 게임 옵션 메뉴와 같은 데이터(스키마의 `text` 현지화 키)다 — 예전에는 id(`display`)를 그대로 보였다. id 는 탭 ID 로 남는다.
            const string tabLabel = category._label + "###" + category._id;
            const bool   bTabOpen = ImGui::BeginTabItem( tabLabel.c_str() );
            UserSettingsPanelInternal::recordMark( "userSettings.tab.", category._id.c_str() );
            if ( bTabOpen == false )
                continue;
            _selectedCategory = categoryIndex;
            if ( ImGui::BeginTable( "##UserSettingsTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY ) )
            {
                ImGui::TableSetupColumn( "Setting", ImGuiTableColumnFlags_WidthFixed, 240.0f * EditorThemeUtil::getDPIScale() );
                ImGui::TableSetupColumn( "Value", ImGuiTableColumnFlags_WidthStretch );
                for ( const UserSettingRow& row : category._listRow )
                {
                    drawSettingRow( row );
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    void UserSettingsPanel::drawToolbar()
    {
        if ( EditorChrome::beginToolbar( "##UserSettingsToolbar" ) )
        {
            const bool bPending = _viewState._bPending != SW_FALSE;
            if ( bPending == false )
                ImGui::BeginDisabled();
            if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Apply" ) ) )
                _viewModel.applyPending();
            EditorSelfTestMarks::record( "userSettings.apply" );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Revert" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Revert" ) ) )
                _viewModel.revertPending();
            EditorSelfTestMarks::record( "userSettings.revert" );
            if ( bPending == false )
                ImGui::EndDisabled();

            ImGui::SameLine();
            if ( ImGui::Button( SW_LOCKEY( "EdText", "ResetTab" ) ) && _selectedCategory < static_cast<uint32>( _viewState._listCategory.size() ) )
                _viewModel.resetCategory( _viewState._listCategory[_selectedCategory]._id );
            EditorSelfTestMarks::record( "userSettings.resetTab" );

            if ( _viewState._bRestartRequired != SW_FALSE )
            {
                ImGui::SameLine();
                EditorWidgets::drawPanelStatus( SW_LOCKEY( "EdWidget", "RestartRequired" ) );
            }
            if ( _viewState._lastRejectedCount > 0 )
            {
                StringBuilder<constant::buffer::kMax64> status;
                status.appendFormat( "%# setting(s) rejected - see the log", _viewState._lastRejectedCount );
                ImGui::SameLine();
                EditorWidgets::drawPanelStatus( status.c_str() );
            }
        }
        EditorChrome::endToolbar();

        // 화면 방식 · 해상도 확인 — 게임 메뉴의 "이 화면을 유지할까요? N 초" 대화상자에 해당한다.
        if ( _viewState._bAwaitingConfirm != SW_FALSE )
        {
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "KeepDisplaySettings", TextArgumentList().addInteger( "seconds", static_cast<int64>( _viewState._confirmSecondsLeft + 0.5f ) ) ).c_str() );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Keep" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "Keep" ) ) )
                _viewModel.confirmChanges();
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "RevertNow" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "RevertNow" ) ) )
                _viewModel.revertConfirmNow();
        }
    }

    void UserSettingsPanel::drawSettingRow( const UserSettingRow& row )
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted( row._label.c_str() );
        EditorWidgets::drawTooltip( row._id.c_str() );
        if ( row._bPending != SW_FALSE )
        {
            ImGui::SameLine();
            ImGui::TextUnformatted( "*" );
        }
        if ( row._descriptionKey.empty() == false )
            EditorWidgets::drawTooltip( row._descriptionKey.c_str() );

        ImGui::TableNextColumn();
        ImGui::PushID( row._id.c_str() );
        const bool bEnabled = row._bEnabled != SW_FALSE;
        if ( bEnabled == false )
            ImGui::BeginDisabled();

        ImGui::SetNextItemWidth( -1.0f );
        switch ( row._type )
        {
            case UserSettingType::Bool:
            {
                if ( row._bHasKey == SW_FALSE )
                    break;
                bool bValue = row._bBoolValue != SW_FALSE;
                if ( ImGui::Checkbox( "##value", &bValue ) )
                    _viewModel.setPendingBool( row._id, bValue );
                break;
            }
            case UserSettingType::Int:
            {
                if ( row._bHasKey == SW_FALSE )
                    break;
                int32 number = row._intValue;
                if ( ImGui::SliderInt( "##value", &number, static_cast<int32>( row._minValue ), static_cast<int32>( row._maxValue ) ) )
                    _viewModel.setPendingInt( row._id, number );
                break;
            }
            case UserSettingType::Float:
            {
                if ( row._bHasKey == SW_FALSE )
                    break;
                float32 number = row._floatValue;
                if ( ImGui::SliderFloat( "##value", &number, static_cast<float32>( row._minValue ), static_cast<float32>( row._maxValue ), "%.2f" ) )
                    _viewModel.setPendingFloat( row._id, number );
                break;
            }
            case UserSettingType::Enum:
            {
                if ( ImGui::BeginCombo( "##value", row._previewLabel.c_str() ) )
                {
                    for ( const UserSettingOptionRow& option : row._listOption )
                    {
                        ImGui::PushID( option._value.c_str() );
                        if ( ImGui::Selectable( option._label.c_str(), option._value == row._value ) )
                            _viewModel.setPendingValue( row._id, option._value );
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                break;
            }
            case UserSettingType::KeyBinding:
            {
                ImGui::TextUnformatted( row._glyph.c_str() );
                ImGui::SameLine();
                string text = row._value;
                if ( EditorWidgets::drawTextField( "##slot", text ) )
                    _viewModel.setPendingBinding( row._id, text );
                break;
            }
            case UserSettingType::String:
            {
                string text = row._value;
                if ( EditorWidgets::drawTextField( "##value", text ) )
                    _viewModel.setPendingValue( row._id, text );
                break;
            }
        }

        UserSettingsPanelInternal::recordMark( "userSettings.value.", row._id.c_str() ); // 값 칸(종류별 위젯)의 자리
        if ( bEnabled == false )
            ImGui::EndDisabled();
        ImGui::PopID();
    }
} // namespace sw::editor
