#include "pch.h"

#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/SelectionManager.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/ComponentPtr.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw::editor
{
	namespace
	{
		string componentTypeBaseName( const Component* pComp )
		{
			if ( pComp == nullptr )
				return "Component";

			const TypeInfo* pTypeInfo = pComp->getTypeInfo();
			if ( pTypeInfo != nullptr )
			{
				if ( pTypeInfo->_fullyQualifiedName.empty() == false )
					return pTypeInfo->_fullyQualifiedName.c_str();
			}

			if ( pComp->getComponentName().empty() == false )
				return pComp->getComponentName().c_str();

			return "Component";
		}

		string componentBaseKey( const Component* pComp )
		{
			if ( pComp != nullptr && pComp->getComponentName().empty() == false )
				return pComp->getComponentName().c_str();
			return componentTypeBaseName( pComp );
		}

		string makeStableComponentKey( const Component* pComp, int32 occurrenceIndex )
		{
			string key = componentBaseKey( pComp );
			key += '#';
			key += to_string( occurrenceIndex );
			return key;
		}

		string computeStableComponentKey( const GameObject* pGameObject, const Component* pTarget )
		{
			if ( pGameObject == nullptr || pTarget == nullptr )
				return {};

			unordered_map<string, int32> mapOccurrence;
			for ( Component* pComp : pGameObject->getAllComponents() )
			{
				if ( pComp == nullptr )
					continue;

				const string base = componentBaseKey( pComp );
				const int32	 occ  = mapOccurrence[base]++;
				if ( pComp == pTarget )
					return makeStableComponentKey( pComp, occ );
			}
			return {};
		}

		Component* findComponentByStableKey( GameObject* pGameObject, string_view key )
		{
			if ( pGameObject == nullptr || key.empty() )
				return nullptr;

			unordered_map<string, int32> mapOccurrence;
			for ( Component* pComp : pGameObject->getAllComponents() )
			{
				if ( pComp == nullptr )
					continue;

				const string base	   = componentBaseKey( pComp );
				const int32	 occ	   = mapOccurrence[base]++;
				const string stableKey = makeStableComponentKey( pComp, occ );
				if ( stableKey == key )
					return pComp;
			}
			return nullptr;
		}
	} // namespace

	// ------------------------------------------------------------------------------
	// Constructor
	// ------------------------------------------------------------------------------
	EditorWorkspace::EditorWorkspace()
		: _selectedComponentId{ 0 }
		, _selectedComponentKey{}
		, _focusedAssetPath{}
		, _inspectMode{ InspectMode::GameObject }
		, _gizmoOperation{ 0 }
		, _pendingOpenPanelTitle{}
		, _pendingScenePath{}
		, _pendingSceneMutex{}
		, _scrollToComponentId{ 0 }
		, _scrollToObjectId{ 0 }
		, _bGizmoLocalSpace{ true }
		, _bBoneHierarchyPopupOpen{ false }
	{
	}

	// ------------------------------------------------------------------------------
	// Member Methods
	// ------------------------------------------------------------------------------
	uint64 EditorWorkspace::getSelectedObjectId() const
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr )
			return pContext->getSelectionManager().getPrimaryObjectId();
		return 0;
	}

	GameObjectPtr EditorWorkspace::getSelectedObject() const
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr )
			return pContext->getSelectionManager().getPrimaryObject();
		return GameObjectPtr{};
	}

	string EditorWorkspace::getSelectedObjectName() const
	{
		GameObjectPtr pObj = getSelectedObject();
		if ( pObj.isValid() )
			return string{ pObj.get()->getName().c_str() };
		return {};
	}

	void EditorWorkspace::clearSelection()
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr )
			pContext->getSelectionManager().clearAll();
		_selectedComponentId = 0;
		_selectedComponentKey.clear();
	}

	void EditorWorkspace::selectGameObject( GameObjectPtr pObj, SelectionMode mode )
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr )
			pContext->getSelectionManager().selectObject( pObj, mode );
		_selectedComponentId = 0;
		_selectedComponentKey.clear();
		_inspectMode = InspectMode::GameObject;
	}

	void EditorWorkspace::selectComponent( GameObjectPtr pObj, ComponentPtr pComp )
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr )
			pContext->getSelectionManager().selectObject( pObj, SelectionMode::Replace );

		Component* pRawComp = pComp.get();
		if ( pRawComp != nullptr )
			_selectedComponentId = pRawComp->getComponentId();
		else
			_selectedComponentId = 0;

		GameObject* pRawObj = pObj.get();
		if ( pRawObj != nullptr && pRawComp != nullptr )
			_selectedComponentKey = computeStableComponentKey( pRawObj, pRawComp );
		else
			_selectedComponentKey.clear();

		_scrollToComponentId = _selectedComponentId;
		_inspectMode		 = InspectMode::GameObject;
	}

	void EditorWorkspace::remapSelectionByObjectName( GameObjectManager* pGameObjectManager )
	{
		if ( pGameObjectManager == nullptr )
			return;

		const string name = getSelectedObjectName();
		if ( name.empty() )
		{
			_selectedComponentId = 0;
			_selectedComponentKey.clear();
			return;
		}

		GameObject* pObj = pGameObjectManager->findGameObjectByName( hashed_string( name.c_str() ) );
		if ( pObj == nullptr )
		{
			clearSelection();
			return;
		}

		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr )
			pContext->getSelectionManager().selectObject( GameObjectPtr{ pObj }, SelectionMode::Replace );

		if ( _selectedComponentKey.empty() )
		{
			_selectedComponentId = 0;
			return;
		}

		Component* pRematerialized = findComponentByStableKey( pObj, _selectedComponentKey );
		if ( pRematerialized != nullptr )
			_selectedComponentId = pRematerialized->getComponentId();
		else
			_selectedComponentId = 0;
	}

	void EditorWorkspace::setFocusedAssetPath( const utf8* pPath )
	{
		_focusedAssetPath = ( pPath != nullptr ) ? pPath : "";
		if ( pPath != nullptr && pPath[0] != '\0' )
		{
			EditorContext* pContext = EditorContext::get();
			if ( pContext != nullptr )
				pContext->getSelectionManager().selectAsset( pPath, SelectionMode::Replace );
		}
	}

	void EditorWorkspace::requestOpenPanel( const utf8* pTitle )
	{
		_pendingOpenPanelTitle = ( pTitle != nullptr ) ? pTitle : "";
	}

	bool EditorWorkspace::consumeOpenPanel( string& outTitle )
	{
		if ( _pendingOpenPanelTitle.empty() )
			return false;
		outTitle = _pendingOpenPanelTitle;
		_pendingOpenPanelTitle.clear();
		return true;
	}

	void EditorWorkspace::requestLoadScene( string_view path )
	{
		std::scoped_lock<mutex> lock{ _pendingSceneMutex };
		_pendingScenePath = path;
	}

	bool EditorWorkspace::consumeLoadScene( string& outPath )
	{
		std::scoped_lock<mutex> lock{ _pendingSceneMutex };
		if ( _pendingScenePath.empty() )
			return false;
		outPath = _pendingScenePath;
		_pendingScenePath.clear();
		return true;
	}
} // namespace sw::editor
