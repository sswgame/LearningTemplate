#include "pch.h"

#include "Editor/Panels/PrefabEditorPanel.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/SelectionManager.h"

#include "Engine/Object/GameObject/GameObject.h"

#include <imgui.h>

namespace sw::editor
{
	namespace
	{
		struct PrefabEditorPanelInternal
		{
			static GameObject* getPrefabTargetInstance()
			{
				EditorContext* pContext = EditorContext::get();
				if ( pContext == nullptr )
					return nullptr;
				return pContext->getSelectionManager().getPrimaryObject().get();
			}
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
	SW_LOG_CALLER( "PrefabTool" );

	PrefabEditorPanel::PrefabEditorPanel()
		: _selectedPrefabPath{}
		, _selectedInstanceName{}
		, _lastScanKey{}
		, _listOverride{}
		, _listNestedPrefab{}
		, _bShowOnlyModified{ false }
	{
		scanPrefabOverrides( nullptr );
	}

	void PrefabEditorPanel::scanPrefabOverrides( const utf8* pPrefabPath )
	{
		const string_view path = ( pPrefabPath != nullptr ) ? string_view{ pPrefabPath } : string_view{};
		EditorToolAssetCommands::collectPrefabOverrides( PrefabEditorPanelInternal::getPrefabTargetInstance(), path, _selectedPrefabPath,
														 _selectedInstanceName, _listOverride, _listNestedPrefab );
	}

	void PrefabEditorPanel::drawContent()
	{
		EditorContext* pContext = EditorContext::get();
		string		   scanKey;
		const utf8*	   pScanPath{ nullptr };
		if ( pContext != nullptr )
		{
			const string& focused  = pContext->getWorkspace().getFocusedAssetPath();
			GameObject*	  pPrimary = pContext->getSelectionManager().getPrimaryObject().get();
			const uint64  objectId = pPrimary != nullptr ? pPrimary->getObjectId() : 0;
			scanKey				   = focused;
			scanKey += '|';
			scanKey += to_string( objectId );
			if ( EditorUtil::isPrefabAssetPath( focused.c_str() ) )
				pScanPath = focused.c_str();
		}
		if ( scanKey != _lastScanKey )
		{
			_lastScanKey = scanKey;
			scanPrefabOverrides( pScanPath );
		}

		ImGui::TextColored( ImVec4( 0.4f, 0.8f, 1.0f, 1.0f ), "Prefab Asset:" );
		ImGui::SameLine();
		ImGui::Text( "%s", _selectedPrefabPath.empty() ? "(none)" : _selectedPrefabPath.c_str() );

		ImGui::TextColored( ImVec4( 0.4f, 0.8f, 1.0f, 1.0f ), "Active Instance:" );
		ImGui::SameLine();
		ImGui::Text( "%s", _selectedInstanceName.empty() ? "(none)" : _selectedInstanceName.c_str() );

		ImGui::Separator();

		if ( ImGui::CollapsingHeader( "Nested Prefabs & Sub-Assets", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			for ( size_t prefabIndex = 0; prefabIndex < _listNestedPrefab.size(); ++prefabIndex )
			{
				ImGui::BulletText( "[Nested] %s", _listNestedPrefab[prefabIndex].c_str() );
			}
		}

		ImGui::Separator();

		ImGui::Checkbox( "Show Modified Properties Only", &_bShowOnlyModified );
		ImGui::SameLine();
		if ( ImGui::SmallButton( "Refresh Overrides" ) )
			scanPrefabOverrides( _selectedPrefabPath.c_str() );

		if ( ImGui::BeginTable( "PrefabOverridesTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable ) )
		{
			ImGui::TableSetupColumn( "Component", ImGuiTableColumnFlags_WidthFixed, 140.0f );
			ImGui::TableSetupColumn( "Property", ImGuiTableColumnFlags_WidthFixed, 120.0f );
			ImGui::TableSetupColumn( "Template Default", ImGuiTableColumnFlags_WidthStretch );
			ImGui::TableSetupColumn( "Instance Value", ImGuiTableColumnFlags_WidthStretch );
			ImGui::TableSetupColumn( "Action", ImGuiTableColumnFlags_WidthFixed, 80.0f );
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
					ImGui::TextColored( ImVec4( 1.0f, 0.85f, 0.3f, 1.0f ), "* %s", item._propertyName.c_str() );
				else
					ImGui::Text( "%s", item._propertyName.c_str() );

				ImGui::TableNextColumn();
				ImGui::TextDisabled( "%s", item._defaultValue.c_str() );

				ImGui::TableNextColumn();
				if ( item._bModified )
					ImGui::TextColored( ImVec4( 0.4f, 1.0f, 0.4f, 1.0f ), "%s", item._overriddenValue.c_str() );
				else
					ImGui::Text( "%s", item._overriddenValue.c_str() );

				ImGui::TableNextColumn();
				if ( item._bModified )
				{
					ImGui::PushID( static_cast<int32>( overrideIndex ) );
					if ( ImGui::SmallButton( "Revert" ) )
					{
						EditorToolAssetCommands::revertPrefabOverride( PrefabEditorPanelInternal::getPrefabTargetInstance(), item, _selectedPrefabPath );
						SW_LOG_TRACE( "Reverted %s.%s to %s", item._componentName.c_str(), item._propertyName.c_str(), item._defaultValue.c_str() );
					}
					ImGui::PopID();
				}
			}

			ImGui::EndTable();
		}

		ImGui::Separator();

		if ( ImGui::Button( "Apply All Overrides to Template", ImVec2( 220.0f, 0.0f ) ) )
		{
			EditorToolAssetCommands::applyPrefabOverridesToTemplate( PrefabEditorPanelInternal::getPrefabTargetInstance(), _selectedPrefabPath );
			scanPrefabOverrides( _selectedPrefabPath.c_str() );
			SW_LOG_TRACE( "Applied all instance overrides back to template %s", _selectedPrefabPath.c_str() );
		}

		ImGui::SameLine();
		if ( ImGui::Button( "Revert All Overrides", ImVec2( 160.0f, 0.0f ) ) )
		{
			EditorToolAssetCommands::revertAllPrefabOverrides( PrefabEditorPanelInternal::getPrefabTargetInstance(), _selectedPrefabPath );
			scanPrefabOverrides( _selectedPrefabPath.c_str() );
			SW_LOG_TRACE( "Reverted all overrides on %s", _selectedInstanceName.c_str() );
		}
	}
} // namespace sw::editor
