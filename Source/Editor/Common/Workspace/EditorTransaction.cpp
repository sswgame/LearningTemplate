#include "pch.h"

#include "Editor/Common/Workspace/EditorTransaction.h"

#include "Core/String/StringUtil.h"

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

			static GameObject* findTargetGameObject( GameObjectManager* pManager, const Uuid& guid, uint64 objId, string_view objName )
			{
				if ( pManager == nullptr )
					return nullptr;

				if ( guid.isNull() == false )
				{
					EditorContext* pContext = EditorContext::get();
					if ( pContext != nullptr )
					{
						GameObject* pByGuid = pContext->getWorkspace().findGameObjectByGuid( guid );
						if ( pByGuid != nullptr && pByGuid->isPendingKill() == false )
							return pByGuid;
					}
				}

				GameObject* pTarget = pManager->findGameObjectById( objId );
				if ( pTarget == nullptr && objName.empty() == false )
					pTarget = pManager->findGameObjectByName( hashed_string( objName.data(), static_cast<uint32>( objName.size() ) ) );
				return pTarget;
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

		EditorContext* pContext	 = EditorContext::get();
		const uint64   objId	 = pRaw->getObjectId();
		const Uuid	   guid		 = ( pContext != nullptr ) ? pContext->getWorkspace().getOrAssignGuid( objId ) : Uuid{};
		const string   objName	 = string{ pRaw->getName().c_str() };
		const string   beforeStr = string{ beforeXml };
		const string   afterStr	 = string{ afterXml };

		CommandStack::Command cmd{};
		cmd._label = string{ label };
		cmd._undo  = [guid, objId, objName, beforeStr]()
		{
			GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
			if ( pManager == nullptr )
				return;

			GameObject* pTarget = EditorTransactionInternal::findTargetGameObject( pManager, guid, objId, objName );
			if ( pTarget != nullptr )
			{
				ObjectStateSerializer::loadFromXmlString( pTarget, beforeStr );
				ObjectStateSerializer::rebindSceneHierarchy( pTarget, beforeStr );
			}
		};

		cmd._redo = [guid, objId, objName, afterStr]()
		{
			GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
			if ( pManager == nullptr )
				return;

			GameObject* pTarget = EditorTransactionInternal::findTargetGameObject( pManager, guid, objId, objName );
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

		EditorContext* pContext	  = EditorContext::get();
		const uint64   objId	  = pRaw->getObjectId();
		const Uuid	   guid		  = ( pContext != nullptr ) ? pContext->getWorkspace().getOrAssignGuid( objId ) : Uuid{};
		const string   objName	  = string{ pRaw->getName().c_str() };
		const string   stateXml	  = ObjectStateSerializer::saveToXmlString( pRaw );
		const string   prefabPath = ( pContext != nullptr ) ? pContext->getWorkspace().getGameObjectPrefabPath( objId ) : string{};

		CommandStack::Command cmd{};
		cmd._label = string{ label };
		cmd._undo  = [guid, objId, objName]()
		{
			GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
			if ( pManager == nullptr )
				return;

			GameObject* pTarget = EditorTransactionInternal::findTargetGameObject( pManager, guid, objId, objName );
			if ( pTarget != nullptr )
			{
				if ( EditorContext::get()->getSelectionManager().hasObject( GameObjectPtr{ pTarget } ) )
					EditorContext::get()->getSelectionManager().selectObject( GameObjectPtr{ pTarget }, SelectionMode::Remove );
				pManager->destroyObject( pTarget );
			}
		};

		cmd._redo = [guid, objName, stateXml, prefabPath]()
		{
			GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
			if ( pManager == nullptr )
				return;

			GameObject* pCreated = pManager->createGameObject( hashed_string( objName.c_str() ) );
			if ( pCreated != nullptr )
			{
				EditorContext* pCurrentContext = EditorContext::get();
				if ( pCurrentContext != nullptr && guid.isNull() == false )
				{
					pCurrentContext->getWorkspace().setGuid( pCreated->getObjectId(), guid );
				}
				ObjectStateSerializer::loadFromXmlString( pCreated, stateXml );
				ObjectStateSerializer::rebindSceneHierarchy( pCreated, stateXml );
				if ( pCurrentContext != nullptr )
					pCurrentContext->getWorkspace().setGameObjectPrefabPath( pCreated->getObjectId(), prefabPath );
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

		EditorContext* pContext	  = EditorContext::get();
		const uint64   objId	  = pRaw->getObjectId();
		const Uuid	   guid		  = ( pContext != nullptr ) ? pContext->getWorkspace().getOrAssignGuid( objId ) : Uuid{};
		const string   objName	  = string{ pRaw->getName().c_str() };
		const string   stateXml	  = ObjectStateSerializer::saveToXmlString( pRaw );
		const string   prefabPath = ( pContext != nullptr ) ? pContext->getWorkspace().getGameObjectPrefabPath( objId ) : string{};

		CommandStack::Command cmd{};
		cmd._label = string{ label };
		cmd._undo  = [guid, objName, stateXml, prefabPath]()
		{
			GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
			if ( pManager == nullptr )
				return;

			GameObject* pCreated = pManager->createGameObject( hashed_string( objName.c_str() ) );
			if ( pCreated != nullptr )
			{
				EditorContext* pCurrentContext = EditorContext::get();
				if ( pCurrentContext != nullptr && guid.isNull() == false )
				{
					pCurrentContext->getWorkspace().setGuid( pCreated->getObjectId(), guid );
				}
				ObjectStateSerializer::loadFromXmlString( pCreated, stateXml );
				ObjectStateSerializer::rebindSceneHierarchy( pCreated, stateXml );
				if ( pCurrentContext != nullptr )
					pCurrentContext->getWorkspace().setGameObjectPrefabPath( pCreated->getObjectId(), prefabPath );
				EditorContext::get()->getSelectionManager().selectObject( GameObjectPtr{ pCreated }, SelectionMode::Replace );
			}
		};

		cmd._redo = [guid, objId, objName]()
		{
			GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
			if ( pManager == nullptr )
				return;

			GameObject* pTarget = EditorTransactionInternal::findTargetGameObject( pManager, guid, objId, objName );
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
												EditorDocumentRestoreDelegate restore, string_view coalesceKey )
	{
		recordDocumentText( beforeText, afterText, label, restore, {}, coalesceKey );
	}

	void EditorTransaction::recordDocumentText( string_view beforeText, string_view afterText, string_view label,
												EditorDocumentRestoreDelegate restore, EditorDocumentCaptureDelegate capture,
												string_view coalesceKey )
	{
		if ( beforeText == afterText || restore.isBound() == false )
			return;

		const StringChangeSpan span = StringUtil::makeChangeSpan( beforeText, afterText );
		if ( capture.isBound() )
		{
			push( SW_DELEGATE_LAMBDA( Delegate<void()>, [restore, capture, span]()
			{
				restore( StringUtil::reconstructBefore( span, capture() ) );
			} ),
				  SW_DELEGATE_LAMBDA( Delegate<void()>, [restore, capture, span]()
			{
				restore( StringUtil::reconstructAfter( span, capture() ) );
			} ),
				  label, coalesceKey );
			return;
		}

		const string beforeStr{ beforeText };
		const string afterStr{ afterText };
		push( SW_DELEGATE_LAMBDA( Delegate<void()>, [restore, beforeStr]()
		{
			restore( beforeStr );
		} ),
			  SW_DELEGATE_LAMBDA( Delegate<void()>, [restore, afterStr]()
		{
			restore( afterStr );
		} ),
			  label, coalesceKey );
	}
} // namespace sw::editor
