/**
 * @file AnimationGraphPanel.cpp
 */
#include "Panels/AnimationGraphPanel.h"
#include "EditorDefines.h"
#include "EditorUtil.h"
#include "Runtime/EditorUIContext.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace sw
{
	AnimationGraphPanel::AnimationGraphPanel() = default;

	AnimationGraphPanel::~AnimationGraphPanel()
	{
		destroyEditor();
	}

	void AnimationGraphPanel::ensureEditor()
	{
		if ( _editor != nullptr )
			return;

		ed::Config config{};
		const std::filesystem::path settingsPath =
			EditorUtil::resolveEditorConfigFile( "AnimationGraph.json" );
		if ( settingsPath.empty() == false )
		{
			_settingsPath		= settingsPath.string();
			config.SettingsFile = _settingsPath.c_str();
		}
		_editor = ed::CreateEditor( &config );
	}

	void AnimationGraphPanel::destroyEditor()
	{
		if ( _editor == nullptr )
			return;
		ed::DestroyEditor( _editor );
		_editor = nullptr;
	}

	void AnimationGraphPanel::shutdown( IRHIDevice* /*rhiDevice*/ )
	{
		destroyEditor();
	}

	void AnimationGraphPanel::draw( const EditorUIContext& /*ctx*/ )
	{
		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) == false )
		{
			ImGui::End();
			return;
		}

		ensureEditor();
		if ( _editor == nullptr )
		{
			ImGui::TextUnformatted( "Failed to create Animation Graph editor context." );
			ImGui::End();
			return;
		}

		ed::SetCurrentEditor( _editor );
		ed::Begin( "AnimationGraphCanvas" );

		const ed::NodeId idle( 1 );
		const ed::NodeId walk( 2 );
		const ed::PinId	 idleOut( 10 );
		const ed::PinId	 walkIn( 20 );

		ed::BeginNode( idle );
		ImGui::TextUnformatted( "Idle" );
		ed::BeginPin( idleOut, ed::PinKind::Output );
		ImGui::TextUnformatted( "Out ->" );
		ed::EndPin();
		ed::EndNode();

		ed::BeginNode( walk );
		ImGui::TextUnformatted( "Walk" );
		ed::BeginPin( walkIn, ed::PinKind::Input );
		ImGui::TextUnformatted( "-> In" );
		ed::EndPin();
		ed::EndNode();

		ed::Link( ed::LinkId( 100 ), idleOut, walkIn );

		ed::End();
		ed::SetCurrentEditor( nullptr );
		ImGui::End();
	}
} // namespace sw
