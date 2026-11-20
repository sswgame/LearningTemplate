#include "pch.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Ability/AbilityDebugPanel.h"

#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorCommandGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct AbilityDebugPanelInternal
        {
            /** @brief 열린 어빌리티 디버거가 지난 그리기에서 모은 상태입니다. 창이 닫혔거나 모으지 않았으면 nullptr 입니다. */
            [[nodiscard]] static const AbilityDebugSnapshot* findOpenSnapshot()
            {
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                if ( pPanels == nullptr )
                    return nullptr;
                const AbilityDebugPanel* pPanel = pPanels->findPanel<AbilityDebugPanel>();
                return pPanel != nullptr && pPanel->isOpen() ? pPanel->findSnapshot() : nullptr;
            }

            [[nodiscard]] static bool readAttributeCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const AbilityDebugSnapshot* pSnapshot = findOpenSnapshot();
                if ( pSnapshot == nullptr )
                    return false;
                outValue = static_cast<float64>( pSnapshot->_listAttribute.size() );
                return true;
            }

            [[nodiscard]] static bool readLogCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const AbilityDebugSnapshot* pSnapshot = findOpenSnapshot();
                if ( pSnapshot == nullptr )
                    return false;
                outValue = static_cast<float64>( pSnapshot->_listLog.size() );
                return true;
            }

            [[nodiscard]] static bool readAbilityCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const AbilityDebugSnapshot* pSnapshot = findOpenSnapshot();
                if ( pSnapshot == nullptr )
                    return false;
                outValue = static_cast<float64>( pSnapshot->_listAbility.size() );
                return true;
            }
        };
    } // namespace

    void AbilityDebugPanel::drawTargetPicker()
    {
        if ( _state._bHasScene == SW_FALSE )
            return;
        const utf8* pPreview = _state._targetName.empty() == false ? _state._targetName.c_str() : SW_LOCKEY( "EdAbilityDebug", "PickTarget" );
        const bool  bOpen    = EditorWidgets::beginCombo( "##AbilityTarget", pPreview );
        EditorSelfTestMarks::record( "abilityDebug.target" );
        if ( bOpen == false )
            return;

        _viewModel.gatherCandidates( _listCandidate ); // 펼칠 때만 씬을 훑는다
        if ( _listCandidate.empty() )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdAbilityDebug", "NoComponentsInScene" ) );
        for ( const AbilityDebugCandidate& candidate : _listCandidate )
        {
            ImGui::PushID( static_cast<int32>( candidate._objectID ) );
            if ( ImGui::Selectable( candidate._name.c_str(), candidate._bCurrent != SW_FALSE ) )
                _viewModel.pickTarget( candidate._handle );
            ImGui::PopID();
            EditorSelfTestMarks::recordFormat( "abilityDebug.candidate.%#", candidate._name ); // 시나리오가 대상을 고른다(펼칠 때만 그려진다)
        }
        ImGui::EndCombo();
    }

    void AbilityDebugPanel::drawContent()
    {
        _viewModel.gather( _state );
        drawTargetPicker();

        if ( _state._bPlayStopped != SW_FALSE )
        {
            // 안내 아래에 그 다음 할 일(Play) 단추를 둔다 — 도구줄까지 눈을 옮기지 않는다(Godot 빈 독의 할 일 단추).
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdAbilityDebug", "HintPlayOnly" ) );
            (void)EditorCommandGUI::drawCommandButton( "play.start" );
            return;
        }
        if ( _state._bHasSnapshot == SW_FALSE )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdAbilityDebug", "HintNoTarget" ) );
            return;
        }

        ImGui::TextDisabled( "t = %.2f", static_cast<float64>( _state._snapshot._time ) );
        drawSnapshot();
    }

    void AbilityDebugPanel::drawSnapshot()
    {
        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;

        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdAbilityDebug", "SectionAttributes" ), ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            if ( ImGui::BeginTable( "##Attributes", 3, kTableFlags ) )
            {
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnName" ) );
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnBase" ) );
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnCurrent" ) );
                ImGui::TableHeadersRow();
                for ( const AbilityDebugAttributeRow& row : _state._snapshot._listAttribute )
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( row._name.c_str() );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%.2f", static_cast<float64>( row._baseValue ) );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%.2f", static_cast<float64>( row._currentValue ) );
                }
                ImGui::EndTable();
            }
        }

        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdAbilityDebug", "SectionEffects" ), ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            if ( ImGui::BeginTable( "##Effects", 4, kTableFlags ) )
            {
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnName" ) );
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnRemaining" ) );
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnStacks" ) );
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnSource" ) );
                ImGui::TableHeadersRow();
                for ( const AbilityDebugEffectRow& row : _state._snapshot._listEffect )
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( row._name.c_str() );
                    ImGui::TableNextColumn();
                    if ( row._bInfinite )
                        ImGui::TextUnformatted( SW_LOCKEY( "EdAbilityDebug", "Infinite" ) );
                    else
                        ImGui::Text( "%.2f", static_cast<float64>( row._remainingTime ) );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%d", row._stackCount );
                    ImGui::TableNextColumn();
                    drawSource( row._source );
                }
                ImGui::EndTable();
            }
        }

        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdAbilityDebug", "SectionTags" ), ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            if ( ImGui::BeginTable( "##Tags", 2, kTableFlags ) )
            {
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnTag" ) );
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnCount" ) );
                ImGui::TableHeadersRow();
                for ( const AbilityDebugTagRow& row : _state._snapshot._listTag )
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( row._tag.c_str() );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%d", row._count );
                }
                ImGui::EndTable();
            }
        }

        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdAbilityDebug", "SectionAbilities" ), ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            if ( ImGui::BeginTable( "##Abilities", 3, kTableFlags ) )
            {
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnName" ) );
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnActive" ) );
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnCooldown" ) );
                ImGui::TableHeadersRow();
                for ( const AbilityDebugAbilityRow& row : _state._snapshot._listAbility )
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( row._name.c_str() );
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( row._bActive ? "*" : "" );
                    ImGui::TableNextColumn();
                    if ( row._cooldownRemaining > 0.0f )
                        ImGui::Text( "%.2f", static_cast<float64>( row._cooldownRemaining ) );
                    else
                        ImGui::TextDisabled( "-" );
                }
                ImGui::EndTable();
            }
        }

        // 최근 적용 · 거부(까닭) · 제거(원인) — 늦은 것부터. 거부만 상태 색이다(까닭이 글에 있어 색만으로 말하지 않는다).
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdAbilityDebug", "SectionLog" ), ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            EditorSelfTestMarks::record( "abilityDebug.log" );
            if ( _state._snapshot._listLog.empty() )
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdAbilityDebug", "HintNoLog" ) );
            else if ( ImGui::BeginTable( "##EffectLog", 4, kTableFlags ) )
            {
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnTime" ) );
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnEvent" ) );
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnName" ) );
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnSource" ) );
                ImGui::TableHeadersRow();
                for ( const AbilityDebugLogRow& row : _state._snapshot._listLog )
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text( "%.2f", static_cast<float64>( row._time ) );
                    ImGui::TableNextColumn();
                    if ( row._bRejected )
                        ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Warning" ) ), "%s", row._event.c_str() );
                    else
                        ImGui::TextUnformatted( row._event.c_str() );
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( row._effect.c_str() );
                    ImGui::TableNextColumn();
                    drawSource( row._source );
                }
                ImGui::EndTable();
            }
        }
    }

    void AbilityDebugPanel::drawSource( const string& source )
    {
        // 건 길이 없으면 어빌리티 · 코드가 건 것이다 — 없는 값은 회색 대시(app-screens 규칙).
        if ( source.empty() )
            ImGui::TextDisabled( "-" );
        else
            ImGui::TextUnformatted( source.c_str() );
    }

    SW_EDITOR_PANEL( AbilityDebugPanel, AbilityDebugPanel::kPanelID, EditorPanelCategory::Custom, 9310 );
    SW_AUTOMATION_WATCH( editorAbilityDebugAttributeCount, "Editor.AbilityDebugAttributeCount",
                         "Attributes of the Ability Debugger's target as it last drew (fails while closed, stopped or without a target)",
                         &AbilityDebugPanelInternal::readAttributeCount );
    SW_AUTOMATION_WATCH( editorAbilityDebugLogCount, "Editor.AbilityDebugLogCount",
                         "Recent effect log rows (applied, rejected with the reason, removed with the cause) of the Ability Debugger's target as it last drew",
                         &AbilityDebugPanelInternal::readLogCount );
    SW_AUTOMATION_WATCH( editorAbilityDebugAbilityCount, "Editor.AbilityDebugAbilityCount", "Granted abilities of the Ability Debugger's target as it last drew",
                         &AbilityDebugPanelInternal::readAbilityCount );
} // namespace sw::editor
