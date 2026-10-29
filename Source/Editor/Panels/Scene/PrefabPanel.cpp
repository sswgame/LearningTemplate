#include "pch.h"

#include "Editor/Panels/Scene/PrefabPanel.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/Panels/Prefab/PrefabEditorPanel.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct PrefabPanelInternal
        {
            static constexpr Color kIsolationText{ 1.0f, 0.75f, 0.2f, 1.0f };
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( PrefabPanel, PrefabPanel::kPanelID, EditorPanelCategory::Tool, 1400 );

    // 도구 패널이지만 열린 채 시작한다 — 기본 도킹이 가운데 탭(Scene · Game 옆)에 붙여 떠 있는 창으로 화면을 덮지 않는다(`EditorDockLayout::applyDefaultDockLayout`).
    PrefabPanel::PrefabPanel()
        : _model{}
    {
    }

    void PrefabPanel::drawContent()
    {
        _model.refreshIfFocusChanged();
        const string& prefabPath   = _model.getPrefabPath();
        const string& instanceName = _model.getInstanceName();

        EditorThemeUtil::textInfo( "Prefab Asset:" );
        ImGui::SameLine();
        ImGui::Text( "%s", prefabPath.empty() ? "(none)" : prefabPath.c_str() );

        EditorThemeUtil::textInfo( "Active Instance:" );
        ImGui::SameLine();
        ImGui::Text( "%s", instanceName.empty() ? "(none)" : instanceName.c_str() );

        PrefabEditorPanel* pWindow = PrefabEditorPanel::findIsolatingWindow(); // 프리팹 창이 열려 문서를 격리 중이면 그 창
        if ( pWindow != nullptr )
        {
            ImGui::TextColored( EditorColorUtil::toImVec4( PrefabPanelInternal::kIsolationText ), "Prefab Window: %s", string{ pWindow->getIsolation().getPrefabPath() }.c_str() );
            if ( ImGui::Button( "Save Prefab & Exit Isolation" ) )
                (void)pWindow->exitIsolation( true ); // 실패는 창이 로그와 상태 줄로 알린다
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "SavePrefabExitIsolation" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Exit Isolation" ) );
            if ( ImGui::Button( "Exit Isolation" ) )
                (void)pWindow->exitIsolation( false );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "ExitIsolation" ) );
        }
        else if ( prefabPath.empty() == false )
        {
            if ( ImGui::Button( "Open Prefab Window" ) )
                (void)PrefabEditorPanel::openWindow( prefabPath ); // 창이 없거나 플레이 중이면 열리지 않는다 — 버튼이 아무 일도 하지 않을 뿐이다
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "OpenPrefabIsolation" ) );
        }

        ImGui::Separator();

        drawNestedPrefabSection();

        drawOverrideTable();

        ImGui::Separator();

        const bool bEditsAllowed = EditorPlaySession::areSceneEditsAllowed();
        if ( bEditsAllowed == false )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorScene", "SceneEditsLocked" ) );
            ImGui::BeginDisabled();
        }

        if ( ImGui::Button( "Apply All Overrides to Template" ) )
            _model.applyAllToTemplate();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "ApplyAllOverrides" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Revert All Overrides" ) );
        if ( ImGui::Button( "Revert All Overrides" ) )
            _model.revertAll();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "RevertAllOverrides" ) );

        if ( bEditsAllowed == false )
            ImGui::EndDisabled();
    }

    void PrefabPanel::drawNestedPrefabSection()
    {
        const vector<string>& listNestedPrefab = _model.getNestedPrefabs();
        if ( ImGui::CollapsingHeader( "Nested Prefabs & Sub-Assets", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            for ( size_t prefabIndex = 0; prefabIndex < listNestedPrefab.size(); ++prefabIndex )
            {
                ImGui::PushID( static_cast<int32>( prefabIndex ) );
                ImGui::BulletText( "[Nested] %s", listNestedPrefab[prefabIndex].c_str() );
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Edit" ) );
                if ( ImGui::SmallButton( "Edit" ) )
                {
                    // 창이 이 프리팹을 열고 있으면 그 안에서 중첩 부분만 보이게 하고, 아니면 중첩 프리팹을 창의 문서로 연다.
                    PrefabEditorPanel* pNestedWindow          = PrefabEditorPanel::findIsolatingWindow();
                    const bool         bWindowShowsThisPrefab = pNestedWindow != nullptr && FileUtil::pathsEqualNormalized( pNestedWindow->getIsolation().getPrefabPath(), _model.getPrefabPath() );
                    if ( bWindowShowsThisPrefab )
                        pNestedWindow->requestNestedIsolation( listNestedPrefab[prefabIndex] );
                    else
                        (void)PrefabEditorPanel::openWindow( listNestedPrefab[prefabIndex] ); // 창이 없거나 플레이 중이면 열리지 않는다 — 버튼이 아무 일도 하지 않을 뿐이다
                }
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "EditNestedPrefab" ) );
                ImGui::PopID();
            }
        }

        ImGui::Separator();

        ImGui::Checkbox( "Show Modified Properties Only", &_model.editShowOnlyModified() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "ShowModifiedOnly" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Refresh Overrides" ) );
        if ( ImGui::SmallButton( "Refresh Overrides" ) )
            _model.rescan();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "RefreshOverrides" ) );
    }

    void PrefabPanel::drawOverrideTable()
    {
        if ( ImGui::BeginTable( "PrefabOverridesTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable ) )
        {
            ImGui::TableSetupColumn( "Component", ImGuiTableColumnFlags_WidthFixed, 140.0f * EditorThemeUtil::getDPIScale() );
            ImGui::TableSetupColumn( "Property", ImGuiTableColumnFlags_WidthFixed, 120.0f * EditorThemeUtil::getDPIScale() );
            ImGui::TableSetupColumn( "Template Default", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableSetupColumn( "Instance Value", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableSetupColumn( "Action", ImGuiTableColumnFlags_WidthFixed, 80.0f * EditorThemeUtil::getDPIScale() );
            ImGui::TableHeadersRow();

            const vector<PrefabOverrideItem>& listOverride = _model.getOverrides();
            for ( size_t overrideIndex = 0; overrideIndex < listOverride.size(); ++overrideIndex )
            {
                const PrefabOverrideItem& item = listOverride[overrideIndex];
                if ( _model.isVisible( item ) == false )
                    continue;

                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text( "%s", item._componentName.c_str() );

                ImGui::TableNextColumn();
                if ( item._bModified )
                {
                    EditorThemeUtil::pushTextColor( EditorThemeUtil::getWarningColor() );
                    ImGui::Text( "* %s", item._propertyLabel.c_str() );
                    EditorThemeUtil::popTextColor();
                }
                else
                {
                    ImGui::Text( "%s", item._propertyLabel.c_str() );
                }

                ImGui::TableNextColumn();
                ImGui::TextDisabled( "%s", item._defaultValue.c_str() );

                ImGui::TableNextColumn();
                if ( item._bModified )
                    EditorThemeUtil::textSuccess( item._overriddenValue.c_str() );
                else
                    ImGui::Text( "%s", item._overriddenValue.c_str() );

                ImGui::TableNextColumn();
                if ( item._bModified )
                {
                    ImGui::PushID( static_cast<int32>( overrideIndex ) );
                    const bool bRevert = ImGui::SmallButton( "Revert" );
                    EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "RevertOverride" ) );
                    ImGui::PopID();
                    if ( bRevert )
                    {
                        _model.revertOverride( overrideIndex ); // 목록을 다시 훑어 item 이 무효가 된다 — 이 프레임의 표는 여기서 끝낸다
                        break;
                    }
                }
            }

            ImGui::EndTable();
        }
    }
} // namespace sw::editor
