#include "pch.h"

#include "Editor/Common/Workspace/EditorTransaction.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/SelectionManager.h"

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
		GameObjectManager* getActiveGameObjectManager()
		{
			SceneManager* pSceneManager = editor::getService<SceneManager>();
			if ( pSceneManager == nullptr )
				return nullptr;

			Scene* pActiveScene = pSceneManager->getActiveScene();
			if ( pActiveScene == nullptr )
				return nullptr;

			return pActiveScene->getObjectManager();
		}

		void markActiveSceneDirty()
		{
			EditorContext* pContext = EditorContext::get();
			if ( pContext == nullptr )
				return;
			pContext->getWorkspace().markSceneDirty();
		}
	} // namespace

	void EditorTransaction::beginTransaction( string_view label )
	{
		editor::getService<CommandStack>()->beginTransaction( label );
	}

	void EditorTransaction::endTransaction()
	{
		editor::getService<CommandStack>()->endTransaction();
	}

	void EditorTransaction::cancelTransaction()
	{
		editor::getService<CommandStack>()->cancelTransaction();
	}

	string EditorTransaction::captureSnapshot( GameObjectPtr pObj )
	{
		GameObject* pRaw = pObj.get();
		if ( pRaw == nullptr )
			return {};
		return ObjectStateSerializer::saveToXmlString( pRaw );
	}

	void EditorTransaction::recordModify( GameObjectPtr pObj, string_view beforeXml, string_view afterXml,
										  string_view label )
	{
		GameObject* pRaw = pObj.get();
		if ( pRaw == nullptr || beforeXml == afterXml )
			return;

		const uint64 objId	   = pRaw->getObjectId();
		const string beforeStr = string{ beforeXml };
		const string afterStr  = string{ afterXml };

		CommandStack::Command cmd{};
		cmd._label = string{ label };
		cmd._undo  = [objId, beforeStr]()
		{
			GameObjectManager* pManager = getActiveGameObjectManager();
			if ( pManager == nullptr )
				return;

			GameObject* pTarget = pManager->findGameObjectById( objId );
			if ( pTarget != nullptr )
			{
				ObjectStateSerializer::loadFromXmlString( pTarget, beforeStr );
				ObjectStateSerializer::rebindSceneHierarchy( pTarget, beforeStr );
			}
		};

		cmd._redo = [objId, afterStr]()
		{
			GameObjectManager* pManager = getActiveGameObjectManager();
			if ( pManager == nullptr )
				return;

			GameObject* pTarget = pManager->findGameObjectById( objId );
			if ( pTarget != nullptr )
			{
				ObjectStateSerializer::loadFromXmlString( pTarget, afterStr );
				ObjectStateSerializer::rebindSceneHierarchy( pTarget, afterStr );
			}
		};

		editor::getService<CommandStack>()->push( std::move( cmd ) );
		markActiveSceneDirty();
	}

	void EditorTransaction::recordCreation( GameObjectPtr pObj, string_view label )
	{
		GameObject* pRaw = pObj.get();
		if ( pRaw == nullptr )
			return;

		const uint64 objId	  = pRaw->getObjectId();
		const string objName  = string{ pRaw->getName().c_str() };
		const string stateXml = ObjectStateSerializer::saveToXmlString( pRaw );

		CommandStack::Command cmd{};
		cmd._label = string{ label };
		cmd._undo  = [objId]()
		{
			GameObjectManager* pManager = getActiveGameObjectManager();
			if ( pManager == nullptr )
				return;

			GameObject* pTarget = pManager->findGameObjectById( objId );
			if ( pTarget != nullptr )
			{
				if ( EditorContext::get()->getSelectionManager().hasObject( GameObjectPtr{ pTarget } ) )
					EditorContext::get()->getSelectionManager().selectObject( GameObjectPtr{ pTarget }, SelectionMode::Remove );
				pManager->destroyObject( pTarget );
			}
		};

		cmd._redo = [objName, stateXml]()
		{
			GameObjectManager* pManager = getActiveGameObjectManager();
			if ( pManager == nullptr )
				return;

			GameObject* pCreated = pManager->createGameObject( hashed_string( objName.c_str() ) );
			if ( pCreated != nullptr )
			{
				ObjectStateSerializer::loadFromXmlString( pCreated, stateXml );
				ObjectStateSerializer::rebindSceneHierarchy( pCreated, stateXml );
				EditorContext::get()->getSelectionManager().selectObject( GameObjectPtr{ pCreated }, SelectionMode::Replace );
			}
		};

		editor::getService<CommandStack>()->push( std::move( cmd ) );
		markActiveSceneDirty();
	}

	void EditorTransaction::recordDestruction( GameObjectPtr pObj, string_view label )
	{
		GameObject* pRaw = pObj.get();
		if ( pRaw == nullptr )
			return;

		const uint64 objId	  = pRaw->getObjectId();
		const string objName  = string{ pRaw->getName().c_str() };
		const string stateXml = ObjectStateSerializer::saveToXmlString( pRaw );

		CommandStack::Command cmd{};
		cmd._label = string{ label };
		cmd._undo  = [objName, stateXml]()
		{
			GameObjectManager* pManager = getActiveGameObjectManager();
			if ( pManager == nullptr )
				return;

			GameObject* pCreated = pManager->createGameObject( hashed_string( objName.c_str() ) );
			if ( pCreated != nullptr )
			{
				ObjectStateSerializer::loadFromXmlString( pCreated, stateXml );
				ObjectStateSerializer::rebindSceneHierarchy( pCreated, stateXml );
				EditorContext::get()->getSelectionManager().selectObject( GameObjectPtr{ pCreated }, SelectionMode::Replace );
			}
		};

		cmd._redo = [objId]()
		{
			GameObjectManager* pManager = getActiveGameObjectManager();
			if ( pManager == nullptr )
				return;

			GameObject* pTarget = pManager->findGameObjectById( objId );
			if ( pTarget != nullptr )
			{
				if ( EditorContext::get()->getSelectionManager().hasObject( GameObjectPtr{ pTarget } ) )
					EditorContext::get()->getSelectionManager().selectObject( GameObjectPtr{ pTarget }, SelectionMode::Remove );
				pManager->destroyObject( pTarget );
			}
		};

		editor::getService<CommandStack>()->push( std::move( cmd ) );
		markActiveSceneDirty();
	}
} // namespace sw::editor
