/**
 * @file NodeEditorPanel.cpp
 */
#include "Panels/NodeEditorPanel.h"
#include "EditorDefines.h"
#include "EditorUtil.h"
#include "Runtime/EditorUIContext.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace sw
{
	NodeEditorPanel::NodeEditorPanel() = default;

	NodeEditorPanel::~NodeEditorPanel()
	{
		destroyEditor();
	}

	void NodeEditorPanel::ensureEditor()
	{
		if ( _editor != nullptr )
			return;

		ed::Config config{};
		const std::filesystem::path settingsPath =
			EditorUtil::resolveEditorConfigFile( editor::path::kNodeEditorSettingsFile );
		if ( settingsPath.empty() == false )
		{
			_settingsPath		  = settingsPath.string();
			config.SettingsFile = _settingsPath.c_str();
		}
		else
		{
			_settingsPath.clear();
			config.SettingsFile = nullptr;
		}

		_editor = ed::CreateEditor( &config );
	}

	void NodeEditorPanel::destroyEditor()
	{
		if ( _editor == nullptr )
			return;
		ed::DestroyEditor( _editor );
		_editor = nullptr;
	}

	void NodeEditorPanel::shutdown( IRHIDevice* /*rhiDevice*/ )
	{
		destroyEditor();
	}

	void NodeEditorPanel::draw( const EditorUIContext& /*ctx*/ )
	{
		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) == false )
		{
			ImGui::End();
			return;
		}

		ensureEditor();
		if ( _editor == nullptr )
		{
			ImGui::TextUnformatted( "Failed to create imgui-node-editor context." );
			ImGui::End();
			return;
		}

		ed::SetCurrentEditor( _editor );
		ed::Begin( "NodeEditorCanvas" );

		const ed::NodeId nodeA( 1 );
		const ed::NodeId nodeB( 2 );
		const ed::PinId	 pinAOut( 10 );
		const ed::PinId	 pinBIn( 20 );

		ed::BeginNode( nodeA );
		ImGui::TextUnformatted( "Source" );
		ed::BeginPin( pinAOut, ed::PinKind::Output );
		ImGui::TextUnformatted( "Out ->" );
		ed::EndPin();
		ed::EndNode();

		ed::BeginNode( nodeB );
		ImGui::TextUnformatted( "Sink" );
		ed::BeginPin( pinBIn, ed::PinKind::Input );
		ImGui::TextUnformatted( "-> In" );
		ed::EndPin();
		ed::EndNode();

		ed::Link( ed::LinkId( 100 ), pinAOut, pinBIn );

		ed::End();
		ed::SetCurrentEditor( nullptr );

		ImGui::End();
	}
} // namespace sw
