#include "pch.h"

#include "Editor/Common/Commands/EditorInspectorCommands.h"

#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Utility/Resource/ResourceManager.h"

#include "RuntimeAPI/Service/EditorService.h"

namespace sw::editor
{
	SW_LOG_CALLER( "EditorInspectorCommands" );

	namespace
	{
		bool isSelectionCurrent( uint64 selectedObjectId )
		{
			if ( selectedObjectId == 0 )
				return true;
			EditorContext* pContext = EditorContext::get();
			if ( pContext == nullptr )
				return false;
			return pContext->getWorkspace().getSelectedObjectId() == selectedObjectId;
		}
	} // namespace

	void EditorInspectorCommands::pushPodEdit( void* pData, size_t size, vector<uint8> listBefore, vector<uint8> listAfter,
											   string_view label, uint64 selectedObjectId )
	{
		if ( pData == nullptr || size == 0 || listBefore.size() != size || listAfter.size() != size )
			return;

		CommandStack* pStack = editor::getService<CommandStack>();
		if ( pStack == nullptr )
			return;

		const string		  cmdLabel = string( "Edit " ) + string{ label };
		CommandStack::Command cmd;
		cmd._label = cmdLabel;
		cmd._undo  = [pData, size, listBefore, selectedObjectId]()
		{
			if ( pData != nullptr && isSelectionCurrent( selectedObjectId ) )
				Memory::copy( pData, listBefore.data(), size );
		};
		cmd._redo = [pData, size, listAfter, selectedObjectId]()
		{
			if ( pData != nullptr && isSelectionCurrent( selectedObjectId ) )
				Memory::copy( pData, listAfter.data(), size );
		};
		pStack->push( std::move( cmd ) );
	}

	void EditorInspectorCommands::pushStringEdit( string* pPtr, string before, string after, string_view label,
												  uint64 selectedObjectId )
	{
		if ( pPtr == nullptr )
			return;

		CommandStack* pStack = editor::getService<CommandStack>();
		if ( pStack == nullptr )
			return;

		const string		  cmdLabel = string( "Edit " ) + string{ label };
		CommandStack::Command cmd;
		cmd._label = cmdLabel;
		cmd._undo  = [pPtr, before, selectedObjectId]()
		{
			if ( pPtr != nullptr && isSelectionCurrent( selectedObjectId ) )
				*pPtr = before;
		};
		cmd._redo = [pPtr, after, selectedObjectId]()
		{
			if ( pPtr != nullptr && isSelectionCurrent( selectedObjectId ) )
				*pPtr = after;
		};
		pStack->push( std::move( cmd ) );
	}

	bool EditorInspectorCommands::applyToPrefab( GameObject* pObj, string_view prefabPath )
	{
		if ( pObj == nullptr || prefabPath.empty() )
			return false;

		PrefabAsset asset;
		asset.setFromGameObject( pObj );
		if ( asset.saveToXmlFile( prefabPath ) == false )
			return false;

		SW_LOG_INFO( "Saved prefab changes to %#", string{ prefabPath }.c_str() );
		return true;
	}

	bool EditorInspectorCommands::revertToPrefab( GameObject* pObj, string_view prefabPath )
	{
		if ( pObj == nullptr || prefabPath.empty() )
			return false;

		ResourceManager* pResources = editor::getService<ResourceManager>();
		if ( pResources == nullptr )
			return false;

		PrefabAsset* pLoaded = pResources->getPrefabManager().loadPrefab( prefabPath );
		if ( pLoaded == nullptr || pLoaded->isValid() == false )
			return false;

		const string beforeXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pObj } );
		ObjectStateSerializer::loadFromXmlString( pObj, pLoaded->getStateData() );
		pObj->applyLoadedHierarchy();
		const string afterXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pObj } );
		EditorTransaction::recordModify( GameObjectPtr{ pObj }, beforeXml, afterXml, "Revert to Prefab" );
		return true;
	}

	void EditorInspectorCommands::unlinkPrefab( GameObject* pObj )
	{
		if ( pObj == nullptr )
			return;

		EditorContext* pContext = EditorContext::get();
		if ( pContext == nullptr )
			return;

		pContext->getWorkspace().setGameObjectPrefabPath( pObj->getObjectId(), "" );
	}
} // namespace sw::editor
