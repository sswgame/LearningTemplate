#include "pch.h"

#include "Editor/Panels/PrefabEditorPanel.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"


#include <imgui.h>

namespace sw::editor
{
	PrefabEditorPanel::PrefabEditorPanel()
		: _selectedPrefabPath{ "Prefabs/Characters/Orc_Warrior.prefab" }
		, _selectedInstanceName{ "Orc_Warrior_Instance_1" }
		, _listOverrides{}
		, _listNestedPrefabs{}
		, _bShowOnlyModified{ false }
	{
		scanPrefabOverrides( _selectedPrefabPath.c_str() );
	}

	void PrefabEditorPanel::scanPrefabOverrides( const utf8* pPrefabPath )
	{
		_listOverrides.clear();
		_listNestedPrefabs.clear();

		_selectedPrefabPath = pPrefabPath != nullptr ? pPrefabPath : "Prefabs/Characters/Orc_Warrior.prefab";

		// Sample data for prefab inspection & override tracking
		_listNestedPrefabs.push_back( "Prefabs/Weapons/BattleAxe_Heavy.prefab" );
		_listNestedPrefabs.push_back( "Prefabs/VFX/RageAura_Fire.prefab" );
		_listNestedPrefabs.push_back( "Prefabs/UI/WorldHealthBar.prefab" );

		_listOverrides.push_back( PrefabOverrideItem{ "UnitStatsComponent", "maxHp", "250", "350", true } );
		_listOverrides.push_back( PrefabOverrideItem{ "UnitStatsComponent", "attack", "35", "50", true } );
		_listOverrides.push_back( PrefabOverrideItem{ "UnitStatsComponent", "defense", "15", "15", false } );
		_listOverrides.push_back( PrefabOverrideItem{ "UnitStatsComponent", "moveSpeed", "4.5", "4.5", false } );
		_listOverrides.push_back( PrefabOverrideItem{ "TransformComponent", "scale", "(1.0, 1.0, 1.0)", "(1.25, 1.25, 1.25)", true } );
		_listOverrides.push_back( PrefabOverrideItem{ "MaterialComponent", "tintColor", "(1.0, 1.0, 1.0, 1.0)", "(1.0, 0.8, 0.8, 1.0)", true } );
	}

	void PrefabEditorPanel::drawContent()
	{
		// Header Information
		ImGui::TextColored( ImVec4( 0.4f, 0.8f, 1.0f, 1.0f ), "Prefab Asset:" );
		ImGui::SameLine();
		ImGui::Text( "%s", _selectedPrefabPath.c_str() );

		ImGui::TextColored( ImVec4( 0.4f, 0.8f, 1.0f, 1.0f ), "Active Instance:" );
		ImGui::SameLine();
		ImGui::Text( "%s", _selectedInstanceName.c_str() );

		ImGui::Separator();

		// Nested Prefabs Hierarchy
		if ( ImGui::CollapsingHeader( "Nested Prefabs & Sub-Assets", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			for ( size_t prefabIndex = 0; prefabIndex < _listNestedPrefabs.size(); ++prefabIndex )
			{
				ImGui::BulletText( "[Nested] %s", _listNestedPrefabs[prefabIndex].c_str() );
			}
		}

		ImGui::Separator();

		// Filter Controls
		ImGui::Checkbox( "Show Modified Properties Only", &_bShowOnlyModified );
		ImGui::SameLine();
		if ( ImGui::SmallButton( "Refresh Overrides" ) )
		{
			scanPrefabOverrides( _selectedPrefabPath.c_str() );
		}

		// Overrides Table
		if ( ImGui::BeginTable( "PrefabOverridesTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable ) )
		{
			ImGui::TableSetupColumn( "Component", ImGuiTableColumnFlags_WidthFixed, 140.0f );
			ImGui::TableSetupColumn( "Property", ImGuiTableColumnFlags_WidthFixed, 120.0f );
			ImGui::TableSetupColumn( "Template Default", ImGuiTableColumnFlags_WidthStretch );
			ImGui::TableSetupColumn( "Instance Value", ImGuiTableColumnFlags_WidthStretch );
			ImGui::TableSetupColumn( "Action", ImGuiTableColumnFlags_WidthFixed, 80.0f );
			ImGui::TableHeadersRow();

			for ( size_t overrideIndex = 0; overrideIndex < _listOverrides.size(); ++overrideIndex )
			{
				PrefabOverrideItem& item = _listOverrides[overrideIndex];
				if ( _bShowOnlyModified && item._bModified == false )
					continue;

				ImGui::TableNextRow();

				// Component Column
				ImGui::TableNextColumn();
				ImGui::Text( "%s", item._componentName.c_str() );

				// Property Column
				ImGui::TableNextColumn();
				if ( item._bModified )
					ImGui::TextColored( ImVec4( 1.0f, 0.85f, 0.3f, 1.0f ), "* %s", item._propertyName.c_str() );
				else
					ImGui::Text( "%s", item._propertyName.c_str() );

				// Template Default Column
				ImGui::TableNextColumn();
				ImGui::TextDisabled( "%s", item._defaultValue.c_str() );

				// Instance Value Column
				ImGui::TableNextColumn();
				if ( item._bModified )
					ImGui::TextColored( ImVec4( 0.4f, 1.0f, 0.4f, 1.0f ), "%s", item._overriddenValue.c_str() );
				else
					ImGui::Text( "%s", item._overriddenValue.c_str() );

				// Action Column
				ImGui::TableNextColumn();
				if ( item._bModified )
				{
					ImGui::PushID( static_cast<int32>( overrideIndex ) );
					if ( ImGui::SmallButton( "Revert" ) )
					{
						item._overriddenValue = item._defaultValue;
						item._bModified		  = false;
						SW_LOG_INFO( "[PrefabTool] Reverted %s.%s to %s", item._componentName.c_str(), item._propertyName.c_str(), item._defaultValue.c_str() );
					}
					ImGui::PopID();
				}
			}

			ImGui::EndTable();
		}

		ImGui::Separator();

		// Action Bar
		if ( ImGui::Button( "Apply All Overrides to Template", ImVec2( 220.0f, 0.0f ) ) )
		{
			for ( auto& item : _listOverrides )
			{
				if ( item._bModified )
				{
					item._defaultValue = item._overriddenValue;
					item._bModified	   = false;
				}
			}
			SW_LOG_INFO( "[PrefabTool] Applied all instance overrides back to template %s", _selectedPrefabPath.c_str() );
		}

		ImGui::SameLine();
		if ( ImGui::Button( "Revert All Overrides", ImVec2( 160.0f, 0.0f ) ) )
		{
			for ( auto& item : _listOverrides )
			{
				item._overriddenValue = item._defaultValue;
				item._bModified		  = false;
			}
			SW_LOG_INFO( "[PrefabTool] Reverted all overrides on %s", _selectedInstanceName.c_str() );
		}
	}
} // namespace sw::editor
