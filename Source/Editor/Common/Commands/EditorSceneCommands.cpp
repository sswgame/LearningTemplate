#include "pch.h"

#include "Editor/Common/Commands/EditorSceneCommands.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"

namespace sw::editor
{
	GameObject* EditorSceneCommands::create( GameObjectManager* pManager, GameObject* pParent )
	{
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
		EditorTransaction::recordCreation( GameObjectPtr{ pNewObj }, "Duplicate GameObject" );
		select( pNewObj, SelectionMode::Replace );
		return pNewObj;
	}

	bool EditorSceneCommands::reparent( GameObject* pChild, GameObject* pNewParent, string_view undoLabel )
	{
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
} // namespace sw::editor
