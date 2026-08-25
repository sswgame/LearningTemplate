#include "pch.h"

#include "Editor/Workspace/EditorWorkspace.h"
#include "Editor/Workspace/SelectionManager.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/ComponentPtr.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw
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

	uint64 EditorWorkspace::selectedObjectId()
	{
		return SelectionManager::getPrimaryObjectId();
	}

	GameObjectPtr EditorWorkspace::selectedObject()
	{
		return SelectionManager::getPrimaryObject();
	}

	uint64& EditorWorkspace::selectedComponentId()
	{
		static uint64 s_id{ 0 };
		return s_id;
	}

	string EditorWorkspace::selectedObjectName()
	{
		GameObject* pObj = SelectionManager::getPrimaryObject().get();
		if ( pObj != nullptr )
			return string{ pObj->getName().c_str() };
		return {};
	}

	string& EditorWorkspace::selectedComponentKey()
	{
		static string s_key;
		return s_key;
	}

	void EditorWorkspace::clearSelection()
	{
		SelectionManager::clearAll();
		selectedComponentId() = 0;
		selectedComponentKey().clear();
	}

	void EditorWorkspace::selectGameObject( GameObjectPtr pObj, SelectionMode mode )
	{
		SelectionManager::selectObject( pObj, mode );
		selectedComponentId() = 0;
		selectedComponentKey().clear();
		inspectMode() = InspectMode::GameObject;
	}

	void EditorWorkspace::selectComponent( GameObjectPtr pObj, ComponentPtr pComp )
	{
		SelectionManager::selectObject( pObj, SelectionMode::Replace );
		Component* pRawComp = pComp.get();
		if ( pRawComp != nullptr )
			selectedComponentId() = pRawComp->getComponentId();
		else
			selectedComponentId() = 0;

		GameObject* pRawObj = pObj.get();
		if ( pRawObj != nullptr && pRawComp != nullptr )
			selectedComponentKey() = computeStableComponentKey( pRawObj, pRawComp );
		else
			selectedComponentKey().clear();

		scrollToComponentId() = selectedComponentId();
		inspectMode()		  = InspectMode::GameObject;
	}

	void EditorWorkspace::remapSelectionByObjectName( GameObjectManager* pGameObjectManager )
	{
		if ( pGameObjectManager == nullptr )
			return;

		const string name = selectedObjectName();
		if ( name.empty() )
		{
			selectedComponentId() = 0;
			selectedComponentKey().clear();
			return;
		}

		GameObject* pObj = pGameObjectManager->findGameObjectByName( hashed_string( name.c_str() ) );
		if ( pObj == nullptr )
		{
			clearSelection();
			return;
		}

		SelectionManager::selectObject( GameObjectPtr{ pObj }, SelectionMode::Replace );

		const string& key = selectedComponentKey();
		if ( key.empty() )
		{
			selectedComponentId() = 0;
			return;
		}

		Component* pRematerialized = findComponentByStableKey( pObj, key );
		if ( pRematerialized != nullptr )
			selectedComponentId() = pRematerialized->getComponentId();
		else
			selectedComponentId() = 0;
	}

	string& EditorWorkspace::focusedAssetPath()
	{
		static string s_path;
		return s_path;
	}

	void EditorWorkspace::setFocusedAssetPath( const utf8* pPath )
	{
		focusedAssetPath() = ( pPath != nullptr ) ? pPath : "";
		if ( pPath != nullptr && pPath[0] != '\0' )
			SelectionManager::selectAsset( pPath, SelectionMode::Replace );
	}

	InspectMode& EditorWorkspace::inspectMode()
	{
		static InspectMode s_mode = InspectMode::GameObject;
		return s_mode;
	}

	void EditorWorkspace::setInspectMode( InspectMode mode )
	{
		inspectMode() = mode;
	}

	int32& EditorWorkspace::gizmoOperation()
	{
		static int32 s_op{ 0 };
		return s_op;
	}

	bool& EditorWorkspace::gizmoLocalSpace()
	{
		static bool s_local{ true };
		return s_local;
	}

	string& EditorWorkspace::pendingOpenWindowTitle()
	{
		static string s_title;
		return s_title;
	}

	void EditorWorkspace::requestOpenWindow( const utf8* pTitle )
	{
		pendingOpenWindowTitle() = ( pTitle != nullptr ) ? pTitle : "";
	}

	bool EditorWorkspace::consumeOpenWindow( string& outTitle )
	{
		if ( pendingOpenWindowTitle().empty() )
			return false;
		outTitle = pendingOpenWindowTitle();
		pendingOpenWindowTitle().clear();
		return true;
	}

	uint64& EditorWorkspace::scrollToComponentId()
	{
		static uint64 s_id{ 0 };
		return s_id;
	}

	uint64& EditorWorkspace::scrollToObjectId()
	{
		static uint64 s_id{ 0 };
		return s_id;
	}

	bool& EditorWorkspace::boneHierarchyPopupOpen()
	{
		static bool s_open{ false };
		return s_open;
	}
} // namespace sw
