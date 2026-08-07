/**
 * @file GameState.cpp
 */
#include "GameState.h"
#include "Core/Common/CoreServices.h"
#include "Core/Game/Scene/SceneManager.h"
#include "Core/Game/Scene/Scene.h"
#include "Core/Object/GameObjectManager.h"
#include "Core/Object/GameObject.h"
#include "Core/Object/ObjectStateSerializer.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	namespace
	{
		GameState s_gameState = GameState::Stopped;

		struct ObjectSnapshot
		{
			uint64		objectId = 0;
			std::string name;
			std::string xml;
		};

		std::vector<ObjectSnapshot> s_playSnapshots;
		bool						s_bHasPlaySnapshot = false;

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

		void endPlayActiveScene()
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
					obj->endPlay();
			}
		}

		void capturePlaySnapshot()
		{
			s_playSnapshots.clear();
			s_bHasPlaySnapshot = false;

			Scene* scene = getSceneManager().getActiveScene();
			if ( scene == nullptr || scene->getObjectManager() == nullptr )
				return;

			GameObjectManager* objects = scene->getObjectManager();
			s_playSnapshots.reserve( objects->getAllGameObjects().size() );

			for ( GameObject* obj : objects->getAllGameObjects() )
			{
				if ( obj == nullptr )
					continue;

				ObjectSnapshot entry;
				entry.objectId = obj->getObjectId();
				entry.name	   = obj->getName().c_str();
				entry.xml	   = ObjectStateSerializer::saveToXmlString( obj );
				if ( entry.xml.empty() == false )
					s_playSnapshots.push_back( std::move( entry ) );
			}

			s_bHasPlaySnapshot = true;
			SW_LOG_INFO( "[GameState] Play snapshot captured (%# objects).",
						 static_cast<uint32>( s_playSnapshots.size() ) );
		}

		void restorePlaySnapshot()
		{
			if ( s_bHasPlaySnapshot == false )
				return;

			Scene* scene = getSceneManager().getActiveScene();
			if ( scene == nullptr || scene->getObjectManager() == nullptr )
			{
				s_playSnapshots.clear();
				s_bHasPlaySnapshot = false;
				return;
			}

			GameObjectManager* objects = scene->getObjectManager();

			// Destroy objects spawned during play (not in snapshot).
			{
				std::unordered_set<uint64> snapIds;
				snapIds.reserve( s_playSnapshots.size() );
				for ( const ObjectSnapshot& snap : s_playSnapshots )
					snapIds.insert( snap.objectId );

				std::vector<GameObject*> toDestroy;
				for ( GameObject* obj : objects->getAllGameObjects() )
				{
					if ( obj != nullptr && snapIds.find( obj->getObjectId() ) == snapIds.end() )
						toDestroy.push_back( obj );
				}
				for ( GameObject* obj : toDestroy )
					objects->destroyObjectDeferred( obj );
				objects->processDeferredDestruction();
			}

			// Restore in-place for objects still present; recreate missing ones.
			for ( const ObjectSnapshot& snap : s_playSnapshots )
			{
				GameObject* obj = objects->findGameObjectById( snap.objectId );
				if ( obj == nullptr )
				{
					obj = objects->createGameObject( hashed_string( snap.name.c_str() ) );
					if ( obj == nullptr )
					{
						SW_LOG_WARNING( "[GameState] Failed to recreate '%#' from play snapshot.", snap.name.c_str() );
						continue;
					}
				}

				if ( ObjectStateSerializer::loadFromXmlString( obj, snap.xml ) == false )
					SW_LOG_WARNING( "[GameState] Failed to restore '%#' from play snapshot.", snap.name.c_str() );
			}

			// Second pass: cross-GO SceneComponent parents need all GOs rebuilt first.
			for ( const ObjectSnapshot& snap : s_playSnapshots )
			{
				GameObject* obj = objects->findGameObjectByName( hashed_string( snap.name.c_str() ) );
				if ( obj == nullptr )
					obj = objects->findGameObjectById( snap.objectId );
				if ( obj == nullptr )
					continue;

				if ( ObjectStateSerializer::rebindSceneHierarchy( obj, snap.xml ) == false )
					SW_LOG_WARNING( "[GameState] Failed to rebind scene hierarchy for '%#'.", snap.name.c_str() );
			}

			SW_LOG_INFO( "[GameState] Play snapshot restored (%# objects).",
						 static_cast<uint32>( s_playSnapshots.size() ) );

			s_playSnapshots.clear();
			s_bHasPlaySnapshot = false;
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

		// Stopped → Playing: snapshot editor scene, then beginPlay.
		if ( previous == GameState::Stopped && state == GameState::Playing )
		{
			capturePlaySnapshot();
			beginPlayActiveScene();
		}

		// Playing/Paused → Stopped: endPlay first, then restore editor scene snapshot.
		if ( ( previous == GameState::Playing || previous == GameState::Paused ) && state == GameState::Stopped )
		{
			endPlayActiveScene();
			restorePlaySnapshot();
		}
	}
} // namespace sw
