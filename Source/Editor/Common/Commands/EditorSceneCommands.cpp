#include "pch.h"

#include "Editor/Common/Commands/EditorSceneCommands.h"

#include "Core/String/fixed_string.h"

#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
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

		vector<uint8> buffer;
		const bool	  bSavedBinary = ObjectStateSerializer::saveToBinaryBuffer( pSrc, buffer );
		const string  xml		   = bSavedBinary ? string{} : ObjectStateSerializer::saveToXmlString( pSrc );

		fixed_string<constant::kMaxBuffer256> newName;
		formatstring( newName.data(), newName.capacity(), "%#_Copy", pSrc->getName().c_str() );

		GameObject* pNewObj = pManager->createGameObject( hashed_string( newName.c_str() ) );
		if ( pNewObj == nullptr )
			return nullptr;

		if ( bSavedBinary )
		{
			string parentName;
			ObjectStateSerializer::loadFromBinaryBuffer( pNewObj, buffer.data(), buffer.size(), parentName );
		}
		else
		{
			ObjectStateSerializer::loadFromXmlString( pNewObj, xml );
			ObjectStateSerializer::rebindSceneHierarchy( pNewObj, xml );
		}

		pNewObj->setName( hashed_string( newName.c_str() ) );
		if ( pSrc->getParent() != nullptr )
			pNewObj->attachToParent( pSrc->getParent() );

		EditorContext* pContext	  = EditorContext::get();
		const string   prefabPath = ( pContext != nullptr ) ? pContext->getWorkspace().getGameObjectPrefabPath( pSrc->getObjectId() ) : string{};
		if ( prefabPath.empty() == false && pContext != nullptr )
		{
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
		if ( pChild == nullptr || pNewParent == nullptr )
			return true;
		return pNewParent->isDescendantOf( pChild );
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

		float32					bottomOffset = 0.0f;
		BoxCollider2DComponent* pMyBox		 = pObj->getComponent<BoxCollider2DComponent>();
		if ( pMyBox != nullptr )
			bottomOffset = pMyBox->getOffsetScaleVec()._y * 0.5f;
		MeshComponent* pMyMesh = pObj->getComponent<MeshComponent>();
		if ( pMyMesh != nullptr )
			bottomOffset = scaleY * 0.5f;

		SceneManager* pSceneManager = editor::getService<SceneManager>();
		Scene*		  pScene		= ( pSceneManager != nullptr ) ? pSceneManager->getActiveScene() : nullptr;
		if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
		{
			translation._y = bottomOffset;
			return;
		}

		GameObjectManager* pManager = pScene->getObjectManager();
		float32			   hitY		= 0.0f;
		bool			   bHit		= false;

		const float32 halfExtentX = ( pMyBox != nullptr ) ? pMyBox->getOffsetScaleVec()._x * 0.5f : 0.5f;
		const float32 halfExtentZ = 0.5f;
		const float32 startY	  = translation._y + 10.0f;
		const AABB	  movingBox{
			   float3{translation._x - halfExtentX, startY - bottomOffset, translation._z - halfExtentZ},
			   float3{translation._x + halfExtentX, startY + bottomOffset, translation._z + halfExtentZ}
		   };
		const float3 displacement{ 0.0f, -2000.0f, 0.0f };

		SweepHit sweepHit{};
		if ( pManager->getPhysicsWorld().sweepTest( movingBox, displacement, 0, sweepHit ) )
		{
			if ( sweepHit._hitObjectId != pObj->getObjectId() )
			{
				hitY = movingBox._min._y + displacement._y * sweepHit._time;
				bHit = true;
			}
		}

		const vector<GameObject*>& listAll = pManager->getAllGameObjects();
		for ( const GameObject* pOther : listAll )
		{
			if ( pOther == nullptr || pOther == pObj )
				continue;

			MeshComponent* pOtherMesh = pOther->getComponent<MeshComponent>();
			if ( pOtherMesh != nullptr && pOtherMesh->isActive() )
			{
				const float3  otherPos = pOtherMesh->getWorldPosition();
				const float3  otherScl = pOtherMesh->getLocalScale();
				const float32 topY	   = otherPos._y + otherScl._y * 0.5f;
				if ( topY <= translation._y + 10.0f && ( topY > hitY || bHit == false ) )
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
