/**
 * @file AIGraphPanel.cpp
 */
#include "Panels/AIGraphPanel.h"
#include "EditorDefines.h"
#include "EditorUtil.h"
#include "Runtime/EditorUIContext.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace sw
{
	AIGraphPanel::AIGraphPanel() = default;

	AIGraphPanel::~AIGraphPanel()
	{
		destroyEditor();
	}

	void AIGraphPanel::ensureEditor()
	{
		if ( _editor != nullptr )
			return;

		ed::Config config{};
		const std::filesystem::path settingsPath =
			EditorUtil::resolveEditorConfigFile( "AIGraph.json" );
		if ( settingsPath.empty() == false )
		{
			_settingsPath		= settingsPath.string();
			config.SettingsFile = _settingsPath.c_str();
		}
		_editor = ed::CreateEditor( &config );
	}

	void AIGraphPanel::destroyEditor()
	{
		if ( _editor == nullptr )
			return;
		ed::DestroyEditor( _editor );
		_editor = nullptr;
	}

	void AIGraphPanel::shutdown( IRHIDevice* /*rhiDevice*/ )
	{
		destroyEditor();
	}

	void AIGraphPanel::draw( const EditorUIContext& /*ctx*/ )
	{
		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) == false )
		{
			ImGui::End();
			return;
		}

		ensureEditor();
		if ( _editor == nullptr )
		{
			ImGui::TextUnformatted( "Failed to create AI Graph editor context." );
			ImGui::End();
			return;
		}

		ed::SetCurrentEditor( _editor );
		ed::Begin( "AIGraphCanvas" );

		const ed::NodeId root( 1 );
		const ed::NodeId action( 2 );
		const ed::PinId	 rootOut( 10 );
		const ed::PinId	 actionIn( 20 );

		ed::BeginNode( root );
		ImGui::TextUnformatted( "Selector" );
		ed::BeginPin( rootOut, ed::PinKind::Output );
		ImGui::TextUnformatted( "Out ->" );
		ed::EndPin();
		ed::EndNode();

		ed::BeginNode( action );
		ImGui::TextUnformatted( "Action" );
		ed::BeginPin( actionIn, ed::PinKind::Input );
		ImGui::TextUnformatted( "-> In" );
		ed::EndPin();
		ed::EndNode();

		ed::Link( ed::LinkId( 100 ), rootOut, actionIn );

		ed::End();
		ed::SetCurrentEditor( nullptr );
		ImGui::End();
	}
} // namespace sw
