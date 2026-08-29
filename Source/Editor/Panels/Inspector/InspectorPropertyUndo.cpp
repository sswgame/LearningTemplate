#include "pch.h"

#include "Editor/Panels/Inspector/InspectorPropertyUndo.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/GameObject/GameObjectPtr.h"

#include <imgui.h>

namespace sw::editor
{
	void InspectorPropertyUndo::trackPod( void* pData, size_t size, const utf8* pLabel )
	{
		if ( pData == nullptr || size == 0 || size > 512 )
			return;

		struct Entry
		{
			string		  _beforeXml;
			GameObjectPtr _pObj;
			string		  _label;
		};
		static unordered_map<ImGuiID, Entry> s_mapPending;

		EditorContext* pContext = EditorContext::get();
		if ( pContext == nullptr )
			return;
		GameObjectPtr pObj = pContext->getWorkspace().getSelectedObject();

		const ImGuiID id = ImGui::GetItemID();
		if ( ImGui::IsItemActivated() )
		{
			Entry e;
			e._pObj			 = pObj;
			e._beforeXml	 = EditorTransaction::captureSnapshot( pObj );
			e._label		 = ( pLabel != nullptr ) ? pLabel : "Property";
			s_mapPending[id] = std::move( e );
		}
		if ( ImGui::IsItemDeactivatedAfterEdit() == false )
			return;

		const auto it = s_mapPending.find( id );
		if ( it == s_mapPending.end() )
			return;

		const string afterXml = EditorTransaction::captureSnapshot( it->second._pObj );
		const string lbl	  = string( "Edit " ) + it->second._label;
		EditorTransaction::recordModify( it->second._pObj, it->second._beforeXml, afterXml, lbl );
		s_mapPending.erase( it );
	}

	void InspectorPropertyUndo::trackString( string* pPtr, const utf8* pLabel )
	{
		if ( pPtr == nullptr )
			return;

		struct Entry
		{
			string		  _beforeXml;
			GameObjectPtr _pObj;
			string		  _label;
		};
		static unordered_map<ImGuiID, Entry> s_mapPending;

		EditorContext* pContext = EditorContext::get();
		if ( pContext == nullptr )
			return;
		GameObjectPtr pObj = pContext->getWorkspace().getSelectedObject();

		const ImGuiID id = ImGui::GetItemID();
		if ( ImGui::IsItemActivated() )
		{
			Entry e;
			e._pObj			 = pObj;
			e._beforeXml	 = EditorTransaction::captureSnapshot( pObj );
			e._label		 = ( pLabel != nullptr ) ? pLabel : "Property";
			s_mapPending[id] = std::move( e );
		}
		if ( ImGui::IsItemDeactivatedAfterEdit() == false )
			return;

		const auto it = s_mapPending.find( id );
		if ( it == s_mapPending.end() )
			return;

		const string afterXml = EditorTransaction::captureSnapshot( it->second._pObj );
		const string lbl	  = string( "Edit " ) + it->second._label;
		EditorTransaction::recordModify( it->second._pObj, it->second._beforeXml, afterXml, lbl );
		s_mapPending.erase( it );
	}
} // namespace sw::editor
