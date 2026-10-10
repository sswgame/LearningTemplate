#include "pch.h"

#include "Editor/Panels/PrefabPanel.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Gui/EditorThemeUtil.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/EditorPanelManager.h"

#include "Engine/Object/GameObject/GameObject.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct PrefabPanelInternal
        {
            static GameObject* getPrefabTargetInstance()
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return nullptr;
                return pContext->getEditorSelection().getPrimaryObject();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "PrefabTool" );
    SW_EDITOR_PANEL( PrefabPanel, "prefab_editor", EditorPanelCategory::Tool, 1400 );

    // 도구 패널이지만 열린 채 시작한다 — 기본 도킹이 가운데 탭(Scene · Game 옆)에 붙여 떠 있는 창으로 화면을 덮지 않는다(`EditorDockLayout::applyDefaultDockLayout`).
    PrefabPanel::PrefabPanel()
        : _selectedPrefabPath{}
        , _selectedInstanceName{}
        , _lastScanKey{}
        , _listOverride{}
        , _listNestedPrefab{}
        , _bShowOnlyModified{ false }
    {
        scanPrefabOverrides( nullptr );
    }

    void PrefabPanel::scanPrefabOverrides( const utf8* pPrefabPath )
    {
        const string_view path = ( pPrefabPath != nullptr ) ? string_view{ pPrefabPath } : string_view{};
        EditorToolAssetCommands::collectPrefabOverrides( PrefabPanelInternal::getPrefabTargetInstance(), path, _selectedPrefabPath,
                                                         _selectedInstanceName, _listOverride, _listNestedPrefab );
    }

    void PrefabPanel::drawContent()
    {
        EditorContext* pContext = EditorContext::get();
        const utf8*    pScanPath{ nullptr };
        uint64         objectID{ 0 };
        if ( pContext != nullptr )
        {
            GameObject* pPrimary       = pContext->getEditorSelection().getPrimaryObject();
            objectID                   = pPrimary != nullptr ? pPrimary->getObjectID() : 0;
            const string_view matching = EditorAssetTypeRegistry::matchingFocusedPath( EditorAssetType::Prefab );
            if ( matching.empty() == false )
                pScanPath = matching.data();
        }
        if ( EditorAssetTypeRegistry::consumeWorkspaceFocusKey( _lastScanKey, objectID ) )
            scanPrefabOverrides( pScanPath );

        EditorThemeUtil::textInfo( "Prefab Asset:" );
        ImGui::SameLine();
        ImGui::Text( "%s", _selectedPrefabPath.empty() ? "(none)" : _selectedPrefabPath.c_str() );

        EditorThemeUtil::textInfo( "Active Instance:" );
        ImGui::SameLine();
        ImGui::Text( "%s", _selectedInstanceName.empty() ? "(none)" : _selectedInstanceName.c_str() );

        if ( pContext != nullptr && pContext->getWorkspace().isPrefabIsolationActive() )
        {
            ImGui::TextColored( ImVec4( 1.0f, 0.75f, 0.2f, 1.0f ), "Isolation: %s",
                                pContext->getWorkspace().getPrefabIsolationPrefabPath().c_str() );
            if ( ImGui::Button( "Save Prefab & Exit Isolation" ) )
                EditorAssetCommands::exitPrefabIsolation( true );
            ImGui::SameLine();
            if ( ImGui::Button( "Exit Isolation" ) )
                EditorAssetCommands::exitPrefabIsolation( false );
        }
        else if ( _selectedPrefabPath.empty() == false )
        {
            if ( ImGui::Button( "Open Prefab Isolation" ) )
                EditorAssetCommands::enterPrefabIsolation( _selectedPrefabPath );
        }

        ImGui::Separator();

        drawNestedPrefabSection();

        drawOverrideTable();

        ImGui::Separator();

        const bool bEditsAllowed = EditorUtil::areSceneEditsAllowed();
        if ( bEditsAllowed == false )
        {
            ImGui::TextDisabled( "Scene edits locked until Stop." );
            ImGui::BeginDisabled();
        }

        if ( ImGui::Button( "Apply All Overrides to Template", ImVec2( 220.0f * EditorThemeUtil::getDpiScale(), 0.0f ) ) )
        {
            if ( EditorToolAssetCommands::applyPrefabOverridesToTemplate( PrefabPanelInternal::getPrefabTargetInstance(), _selectedPrefabPath ) )
                SW_LOG_TRACE( "Applied all instance overrides back to template %s", _selectedPrefabPath.c_str() );
            else
                SW_LOG_ERROR( "Could not apply overrides to template %s", _selectedPrefabPath.c_str() );
            scanPrefabOverrides( _selectedPrefabPath.c_str() );
        }

        ImGui::SameLine();
        if ( ImGui::Button( "Revert All Overrides", ImVec2( 160.0f * EditorThemeUtil::getDpiScale(), 0.0f ) ) )
        {
            if ( EditorToolAssetCommands::revertAllPrefabOverrides( PrefabPanelInternal::getPrefabTargetInstance(), _selectedPrefabPath ) )
                SW_LOG_TRACE( "Reverted all overrides on %s", _selectedInstanceName.c_str() );
            else
                SW_LOG_ERROR( "Could not revert overrides on %s", _selectedInstanceName.c_str() );
            scanPrefabOverrides( _selectedPrefabPath.c_str() );
        }

        if ( bEditsAllowed == false )
            ImGui::EndDisabled();
    }

    void PrefabPanel::drawNestedPrefabSection()
    {
        if ( ImGui::CollapsingHeader( "Nested Prefabs & Sub-Assets", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            for ( size_t prefabIndex = 0; prefabIndex < _listNestedPrefab.size(); ++prefabIndex )
            {
                ImGui::PushID( static_cast<int32>( prefabIndex ) );
                ImGui::BulletText( "[Nested] %s", _listNestedPrefab[prefabIndex].c_str() );
                ImGui::SameLine();
                if ( ImGui::SmallButton( "Edit" ) )
                    EditorAssetCommands::enterPrefabIsolation( _listNestedPrefab[prefabIndex] );
                ImGui::PopID();
            }
        }

        ImGui::Separator();

        ImGui::Checkbox( "Show Modified Properties Only", &_bShowOnlyModified );
        ImGui::SameLine();
        if ( ImGui::SmallButton( "Refresh Overrides" ) )
            scanPrefabOverrides( _selectedPrefabPath.c_str() );
    }

    void PrefabPanel::drawOverrideTable()
    {
        if ( ImGui::BeginTable( "PrefabOverridesTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable ) )
        {
            ImGui::TableSetupColumn( "Component", ImGuiTableColumnFlags_WidthFixed, 140.0f * EditorThemeUtil::getDpiScale() );
            ImGui::TableSetupColumn( "Property", ImGuiTableColumnFlags_WidthFixed, 120.0f * EditorThemeUtil::getDpiScale() );
            ImGui::TableSetupColumn( "Template Default", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableSetupColumn( "Instance Value", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableSetupColumn( "Action", ImGuiTableColumnFlags_WidthFixed, 80.0f * EditorThemeUtil::getDpiScale() );
            ImGui::TableHeadersRow();

            for ( size_t overrideIndex = 0; overrideIndex < _listOverride.size(); ++overrideIndex )
            {
                PrefabOverrideItem& item = _listOverride[overrideIndex];
                if ( _bShowOnlyModified && item._bModified == false )
                    continue;

                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text( "%s", item._componentName.c_str() );

                ImGui::TableNextColumn();
                if ( item._bModified )
                {
                    EditorThemeUtil::pushTextColor( EditorThemeUtil::getWarningColor() );
                    ImGui::Text( "* %s", item._propertyName.c_str() );
                    EditorThemeUtil::popTextColor();
                }
                else
                {
                    ImGui::Text( "%s", item._propertyName.c_str() );
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
                    if ( ImGui::SmallButton( "Revert" ) )
                    {
                        EditorToolAssetCommands::revertPrefabOverride( PrefabPanelInternal::getPrefabTargetInstance(), item, _selectedPrefabPath );
                        SW_LOG_TRACE( "Reverted %s.%s to %s", item._componentName.c_str(), item._propertyName.c_str(), item._defaultValue.c_str() );
                        scanPrefabOverrides( _selectedPrefabPath.c_str() );
                    }
                    ImGui::PopID();
                }
            }

            ImGui::EndTable();
        }
    }
} // namespace sw::editor
