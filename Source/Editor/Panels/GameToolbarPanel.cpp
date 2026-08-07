/**
 * @file GameToolbarPanel.cpp
 */
#include "Panels/GameToolbarPanel.h"
#include "EditorSelection.h"
#include "Core/Game/GameState.h"
#include "Core/Common/CoreServices.h"
#include "Core/Game/Scene/SceneManager.h"
#include "Core/Game/Scene/Scene.h"
#include "Core/Object/GameObjectManager.h"
#include <imgui.h>

namespace sw
{
	void GameToolbarPanel::draw( const EditorUIContext& /*ctx*/ )
	{
		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) == false )
		{
			ImGui::End();
			return;
		}

		const GameState currentState = getGameState();

		if ( currentState == GameState::Playing )
			ImGui::Button( "[ Playing ]" );
		else if ( ImGui::Button( "Play" ) )
			setGameState( GameState::Playing );

		ImGui::SameLine();

		if ( currentState == GameState::Paused )
			ImGui::Button( "[ Paused ]" );
		else if ( ImGui::Button( "Pause" ) )
			setGameState( GameState::Paused );

		ImGui::SameLine();

		if ( ImGui::Button( "Stop" ) )
		{
			setGameState( GameState::Stopped );

			Scene* scene = getSceneManager().getActiveScene();
			if ( scene != nullptr )
				editor::remapSelectionByObjectName( scene->getObjectManager() );
		}

		ImGui::End();
	}
} // namespace sw
