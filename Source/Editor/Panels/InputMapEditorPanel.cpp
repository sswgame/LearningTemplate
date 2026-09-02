#include "pch.h"

#include "Editor/Panels/InputMapEditorPanel.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Input/ActionMap.h"
#include "Engine/Input/KeyCodes.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include <imgui.h>

namespace sw::editor
{
	SW_LOG_CALLER( "InputMapEditorPanel" );

	InputMapEditorPanel::InputMapEditorPanel()
		: _actionMap{}
		, _inputMapPath{ "engine/input/default.input.xml" }
		, _newActionName{ "" }
		, _selectedAction{ "" }
		, _capturingBindIndex{ 0 }
		, _bLoaded{ SW_FALSE }
		, _bCapturingKey{ SW_FALSE }
		, _bDirty{ SW_FALSE }
		, _reserved{ 0 }
	{
	}

	void InputMapEditorPanel::drawContent()
	{
		if ( _bLoaded == SW_FALSE )
		{
			reloadFromFile();
			_bLoaded = SW_TRUE;
		}

		ImGui::Text( "InputMap Resource:" );
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 300.0f );
		utf8 arrPathBuf[128];
		std::snprintf( arrPathBuf, sizeof( arrPathBuf ), "%s", _inputMapPath.c_str() );
		if ( ImGui::InputText( "##InputMapPath", arrPathBuf, sizeof( arrPathBuf ) ) )
		{
			_inputMapPath = arrPathBuf;
		}

		ImGui::SameLine();
		if ( ImGui::Button( "Reload" ) )
		{
			reloadFromFile();
		}

		ImGui::SameLine();
		if ( ImGui::Button( "Save XML" ) )
		{
			saveToFile();
		}

		if ( _bDirty == SW_TRUE )
		{
			ImGui::SameLine();
			ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.2f, 1.0f ), "* Unsaved changes" );
		}

		ImGui::Separator();

		drawLayerList();
		ImGui::Separator();
		drawActionTable();
		drawCaptureModal();
	}

	void InputMapEditorPanel::drawLayerList()
	{
		const vector<hashed_string>& listLayer = _actionMap.getLayerNames();

		if ( ImGui::CollapsingHeader( "Layers", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			if ( ImGui::BeginTable( "LayerTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg ) )
			{
				ImGui::TableSetupColumn( "Layer Name", ImGuiTableColumnFlags_WidthStretch );
				ImGui::TableSetupColumn( "Active", ImGuiTableColumnFlags_WidthFixed, 60.0f );
				ImGui::TableSetupColumn( "Priority", ImGuiTableColumnFlags_WidthFixed, 60.0f );
				ImGui::TableSetupColumn( "Stack Status", ImGuiTableColumnFlags_WidthFixed, 100.0f );
				ImGui::TableHeadersRow();

				for ( const hashed_string& layerName : listLayer )
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted( layerName.c_str() );

					ImGui::TableNextColumn();
					ImGui::PushID( layerName.c_str() );
					bool bEnabled = _actionMap.isLayerEnabled( layerName );
					if ( ImGui::Checkbox( "##Enabled", &bEnabled ) )
					{
						_actionMap.setLayerEnabled( layerName.view(), bEnabled );
						_bDirty = SW_TRUE;
					}
					ImGui::PopID();

					ImGui::TableNextColumn();
					ImGui::Text( "%d", _actionMap.getLayerPriority( layerName ) );

					ImGui::TableNextColumn();
					if ( _actionMap.getCurrentTopLayer() == layerName.view() )
						ImGui::TextColored( ImVec4( 0.2f, 1.0f, 0.2f, 1.0f ), "Top (Active)" );
					else if ( bEnabled )
						ImGui::Text( "Active" );
					else
						ImGui::TextDisabled( "Disabled" );
				}
				ImGui::EndTable();
			}
		}
	}

	void InputMapEditorPanel::drawActionTable()
	{
		const vector<hashed_string>& listAction = _actionMap.getActionNames();

		if ( ImGui::BeginTable( "ActionTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable ) )
		{
			ImGui::TableSetupColumn( "Action", ImGuiTableColumnFlags_WidthStretch );
			ImGui::TableSetupColumn( "Trigger", ImGuiTableColumnFlags_WidthFixed, 120.0f );
			ImGui::TableSetupColumn( "Glyph", ImGuiTableColumnFlags_WidthFixed, 90.0f );
			ImGui::TableSetupColumn( "Hold Time", ImGuiTableColumnFlags_WidthFixed, 80.0f );
			ImGui::TableSetupColumn( "Rebind", ImGuiTableColumnFlags_WidthFixed, 80.0f );
			ImGui::TableHeadersRow();

			for ( const hashed_string& actionName : listAction )
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted( actionName.c_str() );

				ImGui::TableNextColumn();
				const ActionTrigger trig	  = _actionMap.getBindingTrigger( actionName, 0 );
				const utf8*			pTrigName = ActionMap::actionTriggerToName( trig );
				ImGui::TextUnformatted( pTrigName != nullptr ? pTrigName : "Unknown" );

				ImGui::TableNextColumn();
				const string glyph = _actionMap.getGlyphForAction( actionName.view() );
				ImGui::TextColored( ImVec4( 0.3f, 0.8f, 1.0f, 1.0f ), "%s", glyph.c_str() );

				ImGui::TableNextColumn();
				ImGui::Text( "%.2f s", static_cast<float64>( _actionMap.getActionHoldDuration( actionName ) ) );

				ImGui::TableNextColumn();
				ImGui::PushID( actionName.c_str() );
				if ( ImGui::Button( "Rebind" ) )
				{
					_selectedAction		= actionName.c_str();
					_capturingBindIndex = 0;
					_bCapturingKey		= SW_TRUE;
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
	}

	void InputMapEditorPanel::drawCaptureModal()
	{
		if ( _bCapturingKey == SW_FALSE )
			return;

		ImGui::OpenPopup( "Press Key To Bind" );
		if ( ImGui::BeginPopupModal( "Press Key To Bind", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			ImGui::Text( "Binding for Action: %s", _selectedAction.c_str() );
			ImGui::Text( "Press any keyboard key, or click Cancel..." );
			ImGui::Separator();

			for ( int32 keyIndex = 1; keyIndex < static_cast<int32>( Key::Count ); ++keyIndex )
			{
				const Key	k		 = static_cast<Key>( keyIndex );
				const utf8* pKeyName = KeyCodes::toName( k );
				if ( pKeyName != nullptr && ImGui::Button( pKeyName ) )
				{
					_actionMap.rebindKey( _selectedAction.c_str(), k, _capturingBindIndex );
					_bDirty		   = SW_TRUE;
					_bCapturingKey = SW_FALSE;
					ImGui::CloseCurrentPopup();
					break;
				}
			}

			if ( ImGui::Button( "Cancel", ImVec2( 120, 0 ) ) )
			{
				_bCapturingKey = SW_FALSE;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void InputMapEditorPanel::reloadFromFile()
	{
		_actionMap.loadFromResource( _inputMapPath.c_str() );
		_bDirty = SW_FALSE;
		SW_LOG_INFO( "Reloaded InputMap from %#", _inputMapPath.c_str() );
	}

	void InputMapEditorPanel::saveToFile()
	{
		_actionMap.saveUserBindings( _inputMapPath.c_str() );
		_bDirty = SW_FALSE;
		SW_LOG_INFO( "Saved InputMap to %#", _inputMapPath.c_str() );
	}
} // namespace sw::editor
