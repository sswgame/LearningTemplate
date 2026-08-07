/**
 * @file GameToolbarPanel.cpp
 */
#include "Panels/GameToolbarPanel.h"
#include "Core/Game/GameState.h"
#include <imgui.h>

namespace sw
{
	void GameToolbarPanel::draw( const EditorUIContext& /*ctx*/ )
	{
		if ( ImGui::Begin( getWindowTitle() ) == false )
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
			setGameState( GameState::Stopped );

		ImGui::End();
	}
}
