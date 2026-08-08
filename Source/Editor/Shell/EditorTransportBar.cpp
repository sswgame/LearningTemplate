/**
 * @file EditorTransportBar.cpp
 */
#include "Shell/EditorTransportBar.h"
#include "Workspace/EditorWorkspace.h"
#include "Core/Game/GameState.h"
#include "Core/Common/CoreServices.h"
#include "Core/Game/Scene/SceneManager.h"
#include "Core/Game/Scene/Scene.h"
#include "Core/Object/GameObjectManager.h"
#include "Widgets/EditorWidgets.h"
#include <imgui.h>

namespace sw
{
	void drawEditorTransportBar( const EditorUIContext& /*ctx*/ )
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const float32		 barH	  = 36.0f;
		ImGui::SetNextWindowPos( ImVec2( viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + 4.0f ),
								 ImGuiCond_Always, ImVec2( 0.5f, 0.0f ) );
		ImGui::SetNextWindowSize( ImVec2( 420.0f, barH ), ImGuiCond_Always );

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove |
								 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus;

		if ( ImGui::Begin( "##EditorTransportBar", nullptr, flags ) == false )
		{
			ImGui::End();
			return;
		}

		const GameState currentState = getGameState();
		const float32	startX		 = ( ImGui::GetContentRegionAvail().x - 340.0f ) * 0.5f;
		if ( startX > 0.0f )
			ImGui::SetCursorPosX( ImGui::GetCursorPosX() + startX );

		if ( currentState == GameState::Playing )
		{
			editor::drawChip( "Playing", editor::style::kOk );
			ImGui::SameLine();
		}
		else if ( ImGui::Button( "Play" ) )
			setGameState( GameState::Playing );

		ImGui::SameLine();
		if ( ImGui::Button( "Simulate" ) )
			setGameState( GameState::Playing );

		ImGui::SameLine();
		if ( currentState == GameState::Paused )
		{
			editor::drawChip( "Paused", editor::style::kWarn );
			ImGui::SameLine();
		}
		else if ( ImGui::Button( "Pause" ) )
			setGameState( GameState::Paused );

		ImGui::SameLine();
		if ( ImGui::Button( "Step" ) )
		{
			if ( currentState != GameState::Playing )
				setGameState( GameState::Playing );
		}

		ImGui::SameLine();
		if ( ImGui::Button( "Stop" ) )
		{
			setGameState( GameState::Stopped );
			Scene* scene = core::getSceneManager().getActiveScene();
			if ( scene != nullptr )
				editor::remapSelectionByObjectName( scene->getObjectManager() );
		}

		ImGui::End();
	}
} // namespace sw
