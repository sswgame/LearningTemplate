#include "pch.h"

#include "Editor/Common/Commands/EditorSceneCommands.h"

#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "RuntimeAPI/Service/EditorService.h"

namespace sw::editor
{
	namespace
	{
		struct EditorSceneCommandsInternal
		{
			static bool canMutateScene()
			{
				return EditorUtil::areSceneEditsAllowed();
			}
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
	GameObject* EditorSceneCommands::create( GameObjectManager* pManager, GameObject* pParent )
	{
		if ( EditorSceneCommandsInternal::canMutateScene() == false )
			return nullptr;
		if ( pManager == nullptr )
			return nullptr;

		GameObject* pCreated = pManager->createGameObject( hashed_string( "GameObject" ) );
		if ( pCreated == nullptr )
			return nullptr;

		pCreated->addComponent<SceneComponent>();
		if ( pParent != nullptr )
			pCreated->attachToParent( pParent );

		EditorTransaction::recordCreation( GameObjectPtr{ pCreated }, "Create GameObject" );
		select( pCreated, SelectionMode::Replace );
		return pCreated;
	}

	GameObject* EditorSceneCommands::duplicate( GameObjectManager* pManager, GameObject* pSrc )
	{
		if ( EditorSceneCommandsInternal::canMutateScene() == false )
			return nullptr;
		if ( pManager == nullptr || pSrc == nullptr )
			return nullptr;

		const string xml = ObjectStateSerializer::saveToXmlString( pSrc );
		utf8		 newName[constant::kMaxBuffer256];
		formatstring( newName, sizeof( newName ), "%#_Copy", pSrc->getName().c_str() );

		GameObject* pNewObj = pManager->createGameObject( hashed_string( newName ) );
		if ( pNewObj == nullptr )
			return nullptr;

		ObjectStateSerializer::loadFromXmlString( pNewObj, xml );
		pNewObj->setName( hashed_string( newName ) );
		if ( pSrc->getParent() != nullptr )
			pNewObj->attachToParent( pSrc->getParent() );
		ObjectStateSerializer::rebindSceneHierarchy( pNewObj, xml );
		const string& prefabPath = pSrc->getPrefabSourcePath();
		if ( prefabPath.empty() == false )
		{
			pNewObj->setPrefabSourcePath( prefabPath );
			EditorContext* pContext = EditorContext::get();
			if ( pContext != nullptr )
				pContext->getWorkspace().setGameObjectPrefabPath( pNewObj->getObjectId(), prefabPath );
		}
		EditorTransaction::recordCreation( GameObjectPtr{ pNewObj }, "Duplicate GameObject" );
		select( pNewObj, SelectionMode::Replace );
		return pNewObj;
	}

	bool EditorSceneCommands::reparent( GameObject* pChild, GameObject* pNewParent, string_view undoLabel )
	{
		if ( EditorSceneCommandsInternal::canMutateScene() == false )
			return false;
		if ( pChild == nullptr || pNewParent == nullptr || pChild == pNewParent )
			return false;
		if ( wouldCreateParentCycle( pChild, pNewParent ) )
			return false;

		const string beforeXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pChild } );
		if ( pChild->attachToParent( pNewParent ) == false )
			return false;

		const string afterXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pChild } );
		EditorTransaction::recordModify( GameObjectPtr{ pChild }, beforeXml, afterXml, undoLabel );
		select( pChild, SelectionMode::Replace );
		return true;
	}

	bool EditorSceneCommands::unparent( GameObject* pObj, string_view undoLabel )
	{
		if ( EditorSceneCommandsInternal::canMutateScene() == false )
			return false;
		if ( pObj == nullptr || pObj->getParent() == nullptr )
			return false;

		const string beforeXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pObj } );
		pObj->detachFromParent();
		const string afterXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pObj } );
		EditorTransaction::recordModify( GameObjectPtr{ pObj }, beforeXml, afterXml, undoLabel );
		select( pObj, SelectionMode::Replace );
		return true;
	}

	bool EditorSceneCommands::destroy( GameObjectManager* pManager, GameObject* pObj )
	{
		if ( EditorSceneCommandsInternal::canMutateScene() == false )
			return false;
		if ( pManager == nullptr || pObj == nullptr )
			return false;

		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr )
		{
			SelectionManager& sel = pContext->getSelectionManager();
			GameObjectPtr	  ptrObj{ pObj };
			if ( sel.hasObject( ptrObj ) )
				sel.selectObject( ptrObj, SelectionMode::Remove );
		}

		EditorTransaction::recordDestruction( GameObjectPtr{ pObj }, "Destroy GameObject" );
		pManager->destroyObject( pObj );
		return true;
	}

	bool EditorSceneCommands::rename( GameObject* pObj, const utf8* pNewName )
	{
		if ( EditorSceneCommandsInternal::canMutateScene() == false )
			return false;
		if ( pObj == nullptr || pNewName == nullptr || pNewName[0] == '\0' )
			return false;

		const string beforeXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pObj } );
		pObj->setName( hashed_string( pNewName ) );
		const string afterXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pObj } );
		EditorTransaction::recordModify( GameObjectPtr{ pObj }, beforeXml, afterXml, "Rename GameObject" );
		return true;
	}

	bool EditorSceneCommands::destroyComponent( GameObjectManager* pManager, GameObject* pObj, Component* pComp )
	{
		if ( EditorSceneCommandsInternal::canMutateScene() == false )
			return false;
		if ( pManager == nullptr || pObj == nullptr || pComp == nullptr )
			return false;

		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr )
		{
			EditorWorkspace& ws = pContext->getWorkspace();
			if ( ws.getSelectedObjectId() == pObj->getObjectId() &&
				 ws.getSelectedComponentId() == pComp->getComponentId() )
			{
				ws.setSelectedComponentId( 0 );
				ws.setSelectedComponentKey( "" );
			}
		}

		pManager->destroyComponent( pComp );
		return true;
	}

	void EditorSceneCommands::select( GameObject* pObj, SelectionMode mode )
	{
		if ( pObj == nullptr )
			return;

		EditorContext* pContext = EditorContext::get();
		if ( pContext == nullptr )
			return;

		pContext->getWorkspace().selectGameObject( GameObjectPtr{ pObj }, mode );
	}

	bool EditorSceneCommands::wouldCreateParentCycle( GameObject* pChild, GameObject* pNewParent )
	{
		if ( pChild == nullptr || pNewParent == nullptr || pChild == pNewParent )
			return true;

		GameObject* pAncestor = pNewParent;
		while ( pAncestor != nullptr )
		{
			if ( pAncestor == pChild )
				return true;
			pAncestor = pAncestor->getParent();
		}
		return false;
	}

	string EditorSceneCommands::captureSnapshot( GameObject* pObj )
	{
		if ( pObj == nullptr )
			return {};
		return EditorTransaction::captureSnapshot( GameObjectPtr{ pObj } );
	}

	void EditorSceneCommands::applyLocalTransform( GameObject* pObj, const float3& translation, const float3& rotationRad,
												   const float3& scale )
	{
		if ( EditorSceneCommandsInternal::canMutateScene() == false )
			return;
		if ( pObj == nullptr )
			return;

		SceneComponent* pSceneComp = pObj->getPrimarySceneComponent();
		if ( pSceneComp == nullptr )
			return;

		pSceneComp->setLocalPosition( translation );
		pSceneComp->setLocalRotation( rotationRad );
		pSceneComp->setLocalScale( scale );
	}

	void EditorSceneCommands::snapTranslationToSurface( GameObject* pObj, float3& translation, float32 scaleY )
	{
		if ( pObj == nullptr )
			return;

		float3 rayStart = translation;
		rayStart._y += 1.0f;

		float32 hitY = 0.0f;
		bool	bHit = false;

		SceneManager* pSceneManager = editor::getService<SceneManager>();
		Scene*		  pScene		= ( pSceneManager != nullptr ) ? pSceneManager->getActiveScene() : nullptr;
		if ( pScene != nullptr && pScene->getObjectManager() != nullptr )
		{
			const vector<GameObject*>& listAll = pScene->getObjectManager()->getAllGameObjects();
			for ( const GameObject* pOther : listAll )
			{
				if ( pOther == nullptr || pOther == pObj )
					continue;

				BoxCollider2DComponent* pOtherBox = pOther->getComponent<BoxCollider2DComponent>();
				if ( pOtherBox != nullptr && pOtherBox->isActive() )
				{
					const float3  otherPos = pOtherBox->getWorldPosition();
					const float2  otherScl = pOtherBox->getOffsetScaleVec();
					const float32 topY	   = otherPos._y + otherScl._y * 0.5f;
					if ( topY <= rayStart._y && ( topY > hitY || bHit == false ) )
					{
						const float32 halfW = otherScl._x * 0.5f;
						if ( otherPos._x - halfW <= translation._x && translation._x <= otherPos._x + halfW )
						{
							hitY = topY;
							bHit = true;
						}
					}
				}

				MeshComponent* pOtherMesh = pOther->getComponent<MeshComponent>();
				if ( pOtherMesh != nullptr && pOtherMesh->isActive() )
				{
					const float3  otherPos = pOtherMesh->getWorldPosition();
					const float3  otherScl = pOtherMesh->getLocalScale();
					const float32 topY	   = otherPos._y + otherScl._y * 0.5f;
					if ( topY <= rayStart._y && ( topY > hitY || bHit == false ) )
					{
						const float32 halfW = otherScl._x * 0.5f;
						const float32 halfD = otherScl._z * 0.5f;
						if ( otherPos._x - halfW <= translation._x && translation._x <= otherPos._x + halfW &&
							 otherPos._z - halfD <= translation._z && translation._z <= otherPos._z + halfD )
						{
							hitY = topY;
							bHit = true;
						}
					}
				}
			}
		}

		float32					bottomOffset = 0.0f;
		BoxCollider2DComponent* pMyBox		 = pObj->getComponent<BoxCollider2DComponent>();
		if ( pMyBox != nullptr )
			bottomOffset = pMyBox->getOffsetScaleVec()._y * 0.5f;
		MeshComponent* pMyMesh = pObj->getComponent<MeshComponent>();
		if ( pMyMesh != nullptr )
			bottomOffset = scaleY * 0.5f;

		translation._y = ( bHit ? hitY : 0.0f ) + bottomOffset;
	}

	void EditorSceneCommands::commitModify( GameObject* pObj, string_view beforeXml, string_view undoLabel )
	{
		if ( EditorSceneCommandsInternal::canMutateScene() == false )
			return;
		if ( pObj == nullptr || beforeXml.empty() )
			return;

		const string afterXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pObj } );
		EditorTransaction::recordModify( GameObjectPtr{ pObj }, beforeXml, afterXml, undoLabel );
	}
} // namespace sw::editor
