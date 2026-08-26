#include "pch.h"

#include "Editor/Overlay/EditorTransportBar.h"

#include "Editor/Widgets/EditorWidgets.h"
#include "Editor/Workspace/EditorWorkspace.h"

#include "Engine/Game/GameState.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "RuntimeAPI/Service/EditorService.h"

#include <imgui.h>

namespace sw
{
	void drawEditorTransportControls()
	{
		const GameState currentState = getGameState();

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
			SceneManager* pSceneManager = editor::getService<SceneManager>();
			if ( pSceneManager != nullptr )
			{
				Scene* pScene = pSceneManager->getActiveScene();
				if ( pScene != nullptr )
					EditorWorkspace::remapSelectionByObjectName( pScene->getObjectManager() );
			}
		}
	}
} // namespace sw
