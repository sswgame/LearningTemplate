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
		struct EditorTransactionInternal
		{
			static GameObjectManager* getActiveGameObjectManager()
			{
				SceneManager* pSceneManager = editor::getService<SceneManager>();
				if ( pSceneManager == nullptr )
					return nullptr;

				Scene* pActiveScene = pSceneManager->getActiveScene();
				if ( pActiveScene == nullptr )
					return nullptr;

				return pActiveScene->getObjectManager();
			}

			static void markActiveSceneDirty()
			{
				EditorContext* pContext = EditorContext::get();
				if ( pContext == nullptr )
					return;
				pContext->getWorkspace().markSceneDirty();
			}
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
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
			GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
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
			GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
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
		EditorTransactionInternal::markActiveSceneDirty();
	}

	void EditorTransaction::recordCreation( GameObjectPtr pObj, string_view label )
	{
		GameObject* pRaw = pObj.get();
		if ( pRaw == nullptr )
			return;

		const uint64 objId		= pRaw->getObjectId();
		const string objName	= string{ pRaw->getName().c_str() };
		const string stateXml	= ObjectStateSerializer::saveToXmlString( pRaw );
		const string prefabPath = pRaw->getPrefabSourcePath();

		CommandStack::Command cmd{};
		cmd._label = string{ label };
		cmd._undo  = [objId]()
		{
			GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
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

		cmd._redo = [objName, stateXml, prefabPath]()
		{
			GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
			if ( pManager == nullptr )
				return;

			GameObject* pCreated = pManager->createGameObject( hashed_string( objName.c_str() ) );
			if ( pCreated != nullptr )
			{
				ObjectStateSerializer::loadFromXmlString( pCreated, stateXml );
				ObjectStateSerializer::rebindSceneHierarchy( pCreated, stateXml );
				pCreated->setPrefabSourcePath( prefabPath );
				EditorContext* pContext = EditorContext::get();
				if ( pContext != nullptr )
					pContext->getWorkspace().setGameObjectPrefabPath( pCreated->getObjectId(), prefabPath );
				EditorContext::get()->getSelectionManager().selectObject( GameObjectPtr{ pCreated }, SelectionMode::Replace );
			}
		};

		editor::getService<CommandStack>()->push( std::move( cmd ) );
		EditorTransactionInternal::markActiveSceneDirty();
	}

	void EditorTransaction::recordDestruction( GameObjectPtr pObj, string_view label )
	{
		GameObject* pRaw = pObj.get();
		if ( pRaw == nullptr )
			return;

		const uint64 objId		= pRaw->getObjectId();
		const string objName	= string{ pRaw->getName().c_str() };
		const string stateXml	= ObjectStateSerializer::saveToXmlString( pRaw );
		const string prefabPath = pRaw->getPrefabSourcePath();

		CommandStack::Command cmd{};
		cmd._label = string{ label };
		cmd._undo  = [objName, stateXml, prefabPath]()
		{
			GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
			if ( pManager == nullptr )
				return;

			GameObject* pCreated = pManager->createGameObject( hashed_string( objName.c_str() ) );
			if ( pCreated != nullptr )
			{
				ObjectStateSerializer::loadFromXmlString( pCreated, stateXml );
				ObjectStateSerializer::rebindSceneHierarchy( pCreated, stateXml );
				pCreated->setPrefabSourcePath( prefabPath );
				EditorContext* pContext = EditorContext::get();
				if ( pContext != nullptr )
					pContext->getWorkspace().setGameObjectPrefabPath( pCreated->getObjectId(), prefabPath );
				EditorContext::get()->getSelectionManager().selectObject( GameObjectPtr{ pCreated }, SelectionMode::Replace );
			}
		};

		cmd._redo = [objId]()
		{
			GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
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
		EditorTransactionInternal::markActiveSceneDirty();
	}

	void EditorTransaction::push( Delegate<void()> undo, Delegate<void()> redo, string_view label,
								  string_view coalesceKey )
	{
		CommandStack* pStack = editor::getService<CommandStack>();
		if ( pStack == nullptr )
			return;

		CommandStack::Command cmd;
		cmd._label = string{ label };
		cmd._undo  = std::move( undo );
		cmd._redo  = std::move( redo );
		if ( coalesceKey.empty() == false )
			pStack->pushCoalesce( coalesceKey, std::move( cmd ) );
		else
			pStack->push( std::move( cmd ) );
	}

	void EditorTransaction::recordDocumentText( string_view beforeText, string_view afterText, string_view label,
												EditorDocumentRestoreDelegate restore )
	{
		if ( beforeText == afterText || restore.isBound() == false )
			return;

		const string beforeStr{ beforeText };
		const string afterStr{ afterText };
		push(
			SW_DELEGATE_LAMBDA( Delegate<void()>, [restore, beforeStr]()
		{
			restore( beforeStr );
		} ),
			SW_DELEGATE_LAMBDA( Delegate<void()>, [restore, afterStr]()
		{
			restore( afterStr );
		} ),
			label );
	}
} // namespace sw::editor
