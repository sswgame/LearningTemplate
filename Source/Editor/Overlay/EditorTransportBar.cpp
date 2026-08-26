#include "pch.h"

#include "Editor/Overlay/EditorTransportBar.h"

#include "Editor/Widgets/EditorWidgets.h"
#include "Editor/Workspace/EditorWorkspace.h"

#include "Engine/Game/GameState.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "RuntimeAPI/Service/EditorService.h"
#include "RuntimeAPI/ABI/EditorUIContext.h"

#include <imgui.h>

namespace sw
{
	void drawEditorTransportBar( const EditorUIContext& /*ctx*/ )
	{
		const ImGuiViewport* pViewport = ImGui::GetMainViewport();
		constexpr float32	 barH	   = 36.0f;
		ImGui::SetNextWindowPos( ImVec2( pViewport->WorkPos.x + pViewport->WorkSize.x * 0.5f, pViewport->WorkPos.y + 4.0f ),
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
			Scene* pScene = editor::getService<SceneManager>()->getActiveScene();
			if ( pScene != nullptr )
				EditorWorkspace::remapSelectionByObjectName( pScene->getObjectManager() );
		}

		ImGui::End();
	}
} // namespace sw
