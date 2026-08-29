#include "pch.h"

#include "Editor/Common/EditorPlaySession.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/CommandStack.h"

#include "RuntimeAPI/Service/EditorService.h"

namespace sw::editor
{
	namespace
	{
		struct EditorPlaySessionInternal
		{
			inline static PlaySessionState s_playState = PlaySessionState::Stopped;
			inline static uint8			   s_bStepPending{ SW_FALSE };

			struct ObjectSnapshot
			{
				uint64 _objectId{ 0 };
				string _name;
				string _xml;
			};

			static inline vector<ObjectSnapshot> s_listPlaySnapshots;
			static inline bool					 s_bHasPlaySnapshot{ false };

			static void beginPlayActiveScene()
			{
				SceneManager* pSceneManager = editor::getService<SceneManager>();
				if ( pSceneManager == nullptr )
					return;

				Scene* pScene = pSceneManager->getActiveScene();
				if ( pScene == nullptr )
					return;

				GameObjectManager* pObjects = pScene->getObjectManager();
				if ( pObjects == nullptr )
					return;

				pObjects->beginPlay();
			}

			static void endPlayActiveScene()
			{
				SceneManager* pSceneManager = editor::getService<SceneManager>();
				if ( pSceneManager == nullptr )
					return;

				Scene* pScene = pSceneManager->getActiveScene();
				if ( pScene == nullptr )
					return;

				GameObjectManager* pObjects = pScene->getObjectManager();
				if ( pObjects == nullptr )
					return;

				pObjects->endPlay();
			}

			static void capturePlaySnapshot()
			{
				s_listPlaySnapshots.clear();
				s_bHasPlaySnapshot = false;

				SceneManager* pSceneManager = editor::getService<SceneManager>();
				if ( pSceneManager == nullptr )
					return;

				Scene* pScene = pSceneManager->getActiveScene();
				if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
					return;

				GameObjectManager* pObjects = pScene->getObjectManager();
				s_listPlaySnapshots.reserve( pObjects->getAllGameObjects().size() );

				for ( GameObject* pObj : pObjects->getAllGameObjects() )
				{
					if ( pObj == nullptr )
						continue;

					ObjectSnapshot entry;
					entry._objectId = pObj->getObjectId();
					entry._name		= pObj->getName().c_str();
					entry._xml		= ObjectStateSerializer::saveToXmlString( pObj );
					if ( entry._xml.empty() == false )
						s_listPlaySnapshots.push_back( std::move( entry ) );
				}

				s_bHasPlaySnapshot = true;
				SW_LOG_TRACE( "Play snapshot captured (%# objects).",
							  static_cast<uint32>( s_listPlaySnapshots.size() ) );
			}

			static void restorePlaySnapshot()
			{
				if ( s_bHasPlaySnapshot == false )
					return;

				SceneManager* pSceneManager = editor::getService<SceneManager>();
				if ( pSceneManager == nullptr )
				{
					s_listPlaySnapshots.clear();
					s_bHasPlaySnapshot = false;
					return;
				}

				Scene* pScene = pSceneManager->getActiveScene();
				if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
				{
					s_listPlaySnapshots.clear();
					s_bHasPlaySnapshot = false;
					return;
				}

				GameObjectManager* pObjects = pScene->getObjectManager();

				// 1. 플레이 도중 생성된 오브젝트 파괴
				{
					unordered_set<uint64> uniqueSnapIds;
					uniqueSnapIds.reserve( s_listPlaySnapshots.size() );
					for ( const ObjectSnapshot& snap : s_listPlaySnapshots )
					{
						uniqueSnapIds.insert( snap._objectId );
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

				// 2. 기존 오브젝트 상태 복구 및 삭제된 오브젝트 재생성
				for ( const ObjectSnapshot& snap : s_listPlaySnapshots )
				{
					GameObject* pObj = pObjects->findGameObjectById( snap._objectId );
					if ( pObj == nullptr )
					{
						pObj = pObjects->createGameObject( hashed_string( snap._name.c_str() ) );
						if ( pObj == nullptr )
						{
							SW_LOG_WARNING( "Failed to recreate '%#' from play snapshot.", snap._name.c_str() );
							continue;
						}
					}

					if ( ObjectStateSerializer::loadFromXmlString( pObj, snap._xml ) == false )
						SW_LOG_WARNING( "Failed to restore '%#' from play snapshot.", snap._name.c_str() );
				}

				// 3. 계층 관계 리바인딩
				for ( const ObjectSnapshot& snap : s_listPlaySnapshots )
				{
					GameObject* pObj = pObjects->findGameObjectByName( hashed_string( snap._name.c_str() ) );
					if ( pObj == nullptr )
						pObj = pObjects->findGameObjectById( snap._objectId );
					if ( pObj == nullptr )
						continue;

					if ( ObjectStateSerializer::rebindSceneHierarchy( pObj, snap._xml ) == false )
						SW_LOG_WARNING( "Failed to rebind scene hierarchy for '%#'.", snap._name.c_str() );
				}

				SW_LOG_TRACE( "Play snapshot restored (%# objects).",
							  static_cast<uint32>( s_listPlaySnapshots.size() ) );

				s_listPlaySnapshots.clear();
				s_bHasPlaySnapshot = false;
			}
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
	SW_LOG_CALLER( "EditorPlaySession" );

	PlaySessionState EditorPlaySession::getState()
	{
		return EditorPlaySessionInternal::s_playState;
	}

	bool EditorPlaySession::isPlaying()
	{
		if ( EditorPlaySessionInternal::s_bStepPending == SW_TRUE )
			return true;
		return EditorPlaySessionInternal::s_playState == PlaySessionState::Playing;
	}

	bool EditorPlaySession::isPaused()
	{
		return EditorPlaySessionInternal::s_playState == PlaySessionState::Paused;
	}

	bool EditorPlaySession::isStopped()
	{
		return EditorPlaySessionInternal::s_playState == PlaySessionState::Stopped;
	}

	bool EditorPlaySession::hasPendingStep()
	{
		return EditorPlaySessionInternal::s_bStepPending == SW_TRUE;
	}

	void EditorPlaySession::stepOnce()
	{
		if ( EditorPlaySessionInternal::s_playState == PlaySessionState::Stopped )
			setState( PlaySessionState::Playing );
		EditorPlaySessionInternal::s_bStepPending = SW_TRUE;
	}

	void EditorPlaySession::consumePendingStep()
	{
		if ( EditorPlaySessionInternal::s_bStepPending == SW_FALSE )
			return;
		EditorPlaySessionInternal::s_bStepPending = SW_FALSE;
		if ( EditorPlaySessionInternal::s_playState == PlaySessionState::Playing )
			EditorPlaySessionInternal::s_playState = PlaySessionState::Paused;
	}

	void EditorPlaySession::setState( PlaySessionState state )
	{
		if ( EditorPlaySessionInternal::s_playState == state )
			return;

		EditorPlaySessionInternal::s_bStepPending = SW_FALSE;
		const PlaySessionState previous			  = EditorPlaySessionInternal::s_playState;
		EditorPlaySessionInternal::s_playState	  = state;

		CommandStack* pCommandStack = editor::getService<CommandStack>();
		if ( pCommandStack != nullptr )
			pCommandStack->clear();

		// Stopped → Playing: 스냅샷 캡처 후 beginPlay
		if ( previous == PlaySessionState::Stopped && state == PlaySessionState::Playing )
		{
			EditorPlaySessionInternal::capturePlaySnapshot();
			EditorPlaySessionInternal::beginPlayActiveScene();
		}

		// Playing/Paused → Stopped: endPlay 호출 후 스냅샷 복구
		if ( ( previous == PlaySessionState::Playing || previous == PlaySessionState::Paused ) && state == PlaySessionState::Stopped )
		{
			EditorPlaySessionInternal::endPlayActiveScene();
			EditorPlaySessionInternal::restorePlaySnapshot();
		}
	}

} // namespace sw::editor
