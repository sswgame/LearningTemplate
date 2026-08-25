#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Game/GameState.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Utility/CommandStack.h"

namespace sw
{

	namespace
	{

		GameState s_gameState = GameState::Stopped;

		struct ObjectSnapshot
		{
			uint64 objectId{ 0 };
			string name;
			string xml;
		};

		vector<ObjectSnapshot> s_listPlaySnapshots;
		bool				   s_bHasPlaySnapshot{ false };
		GameStartMode		   s_gameStartMode = GameStartMode::NewGame;

		void beginPlayActiveScene()
		{
			Scene* pScene = engine::getSceneManager().getActiveScene();
			if ( pScene == nullptr )
				return;

			GameObjectManager* pObjects = pScene->getObjectManager();
			if ( pObjects == nullptr )
				return;

			for ( GameObject* pObj : pObjects->getAllGameObjects() )
			{
				if ( pObj != nullptr && pObj->isActiveInHierarchy() )
					pObj->beginPlay();
			}

			pObjects->beginPlay(); // Call ECS Script Systems
		}

		void endPlayActiveScene()
		{
			Scene* pScene = engine::getSceneManager().getActiveScene();
			if ( pScene == nullptr )
				return;

			GameObjectManager* pObjects = pScene->getObjectManager();
			if ( pObjects == nullptr )
				return;

			for ( GameObject* pObj : pObjects->getAllGameObjects() )
			{
				if ( pObj != nullptr && pObj->isActiveInHierarchy() )
					pObj->endPlay();
			}

			pObjects->endPlay(); // Call ECS Script Systems
		}

		void capturePlaySnapshot()
		{
			s_listPlaySnapshots.clear();
			s_bHasPlaySnapshot = false;

			Scene* pScene = engine::getSceneManager().getActiveScene();
			if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
				return;

			GameObjectManager* pObjects = pScene->getObjectManager();
			s_listPlaySnapshots.reserve( pObjects->getAllGameObjects().size() );

			for ( GameObject* pObj : pObjects->getAllGameObjects() )
			{
				if ( pObj == nullptr )
					continue;

				ObjectSnapshot entry;
				entry.objectId = pObj->getObjectId();
				entry.name	   = pObj->getName().c_str();
				entry.xml	   = ObjectStateSerializer::saveToXmlString( pObj );
				if ( entry.xml.empty() == false )
					s_listPlaySnapshots.push_back( std::move( entry ) );
			}

			s_bHasPlaySnapshot = true;
			SW_LOG_INFO( "[GameState] Play snapshot captured (%# objects).",
						 static_cast<uint32>( s_listPlaySnapshots.size() ) );
		}

		void restorePlaySnapshot()
		{
			if ( s_bHasPlaySnapshot == false )
				return;

			Scene* pScene = engine::getSceneManager().getActiveScene();
			if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
			{
				s_listPlaySnapshots.clear();
				s_bHasPlaySnapshot = false;
				return;
			}

			GameObjectManager* pObjects = pScene->getObjectManager();

			// Destroy objects spawned during play (not in snapshot).
			{
				unordered_set<uint64> uniqueSnapIds;
				uniqueSnapIds.reserve( s_listPlaySnapshots.size() );
				for ( const ObjectSnapshot& snap : s_listPlaySnapshots )
				{
					uniqueSnapIds.insert( snap.objectId );
				}

				vector<GameObject*> listToDestroy;
				for ( GameObject* pObj : pObjects->getAllGameObjects() )
				{
					if ( pObj != nullptr && uniqueSnapIds.find( pObj->getObjectId() ) == uniqueSnapIds.end() )
						listToDestroy.push_back( pObj );
				}
				for ( GameObject* pObj : listToDestroy )
				{
					pObjects->destroyObject( pObj );
				}
			}

			// Restore in-place for objects still present; re_create missing ones.
			for ( const ObjectSnapshot& snap : s_listPlaySnapshots )
			{
				GameObject* pObj = pObjects->findGameObjectById( snap.objectId );
				if ( pObj == nullptr )
				{
					pObj = pObjects->createGameObject( hashed_string( snap.name.c_str() ) );
					if ( pObj == nullptr )
					{
						SW_LOG_WARNING( "[GameState] Failed to re_create '%#' from play snapshot.", snap.name.c_str() );
						continue;
					}
				}

				if ( ObjectStateSerializer::loadFromXmlString( pObj, snap.xml ) == false )
					SW_LOG_WARNING( "[GameState] Failed to restore '%#' from play snapshot.", snap.name.c_str() );
			}

			// Second pass: cross-GO SceneComponent parents + GameObject ParentGO need all GOs rebuilt first.
			for ( const ObjectSnapshot& snap : s_listPlaySnapshots )
			{
				GameObject* pObj = pObjects->findGameObjectByName( hashed_string( snap.name.c_str() ) );
				if ( pObj == nullptr )
					pObj = pObjects->findGameObjectById( snap.objectId );
				if ( pObj == nullptr )
					continue;

				if ( ObjectStateSerializer::rebindSceneHierarchy( pObj, snap.xml ) == false )
					SW_LOG_WARNING( "[GameState] Failed to rebind scene hierarchy for '%#'.", snap.name.c_str() );
			}

			SW_LOG_INFO( "[GameState] Play snapshot restored (%# objects).",
						 static_cast<uint32>( s_listPlaySnapshots.size() ) );

			s_listPlaySnapshots.clear();
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

		engine::getCommandStack().clear();

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

	void setGameStartMode( GameStartMode mode )
	{
		s_gameStartMode = mode;
	}

	GameStartMode consumeGameStartMode()
	{
		const GameStartMode mode = s_gameStartMode;
		s_gameStartMode			 = GameStartMode::NewGame;
		return mode;
	}
} // namespace sw
