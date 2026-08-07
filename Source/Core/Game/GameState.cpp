/**
 * @file GameState.cpp
 */
#include "GameState.h"
#include "Core/Common/CoreServices.h"
#include "Core/Game/Scene/SceneManager.h"
#include "Core/Game/Scene/Scene.h"
#include "Core/Object/GameObjectManager.h"
#include "Core/Object/GameObject.h"

namespace sw
{
	namespace
	{
		GameState s_gameState = GameState::Stopped;

		void beginPlayActiveScene()
		{
			Scene* scene = getSceneManager().getActiveScene();
			if ( scene == nullptr )
				return;

			GameObjectManager* objects = scene->getObjectManager();
			if ( objects == nullptr )
				return;

			for ( GameObject* obj : objects->getAllGameObjects() )
			{
				if ( obj != nullptr && obj->isActiveInHierarchy() )
					obj->beginPlay();
			}
		}
	} // namespace

	GameState getGameState()
	{
		return s_gameState;
	}

	void setGameState( GameState state )
	{
		if ( s_gameState == state )
			return;

		const GameState previous = s_gameState;
		s_gameState				 = state;

		// Stopped → Playing: fire beginPlay once for the active scene.
		// Pause/resume does not re-enter beginPlay. Stop has no snapshot restore yet.
		if ( previous == GameState::Stopped && state == GameState::Playing )
			beginPlayActiveScene();
	}
} // namespace sw
