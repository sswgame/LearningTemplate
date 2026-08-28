#include "pch.h"

#include "Editor/Panels/Inspector/InspectorPropertyUndo.h"

#include "Core/Memory/Memory.h"

#include "Editor/Common/Commands/EditorInspectorCommands.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include <imgui.h>

namespace sw::editor
{
	void InspectorPropertyUndo::trackPod( void* pData, size_t size, const utf8* pLabel )
	{
		if ( pData == nullptr || size == 0 || size > 512 )
			return;

		struct Entry
		{
			vector<uint8> _listBefore;
			void*		  _pPtr{ nullptr };
			size_t		  _size{ 0 };
			string		  _label;
		};
		static unordered_map<ImGuiID, Entry> s_mapPending;

		const ImGuiID id = ImGui::GetItemID();
		if ( ImGui::IsItemActivated() )
		{
			Entry e;
			e._pPtr	 = pData;
			e._size	 = size;
			e._label = ( pLabel != nullptr ) ? pLabel : "Property";
			e._listBefore.resize( size );
			Memory::copy( e._listBefore.data(), pData, size );
			s_mapPending[id] = std::move( e );
		}
		if ( ImGui::IsItemDeactivatedAfterEdit() == false )
			return;

		const auto it = s_mapPending.find( id );
		if ( it == s_mapPending.end() )
			return;

		vector<uint8> after( size );
		Memory::copy( after.data(), pData, size );
		if ( after == it->second._listBefore )
		{
			s_mapPending.erase( it );
			return;
		}

		void*		  pPtr		 = it->second._pPtr;
		const size_t  sz		 = it->second._size;
		vector<uint8> listBefore = std::move( it->second._listBefore );
		const string  lbl		 = it->second._label;
		s_mapPending.erase( it );

		const uint64 selectedId = EditorContext::get()->getWorkspace().getSelectedObjectId();
		EditorInspectorCommands::pushPodEdit( pPtr, sz, std::move( listBefore ), std::move( after ), lbl, selectedId );
	}

	void InspectorPropertyUndo::trackString( string* pPtr, const utf8* pLabel )
	{
		if ( pPtr == nullptr )
			return;
		struct Entry
		{
			string	_before;
			string* _pPtr{ nullptr };
			string	_label;
		};
		static unordered_map<ImGuiID, Entry> s_mapPending;
		const ImGuiID						 id = ImGui::GetItemID();
		if ( ImGui::IsItemActivated() )
		{
			Entry e;
			e._pPtr			 = pPtr;
			e._before		 = *pPtr;
			e._label		 = ( pLabel != nullptr ) ? pLabel : "Property";
			s_mapPending[id] = std::move( e );
		}
		if ( ImGui::IsItemDeactivatedAfterEdit() == false )
			return;
		const auto it = s_mapPending.find( id );
		if ( it == s_mapPending.end() )
			return;
		string after = *pPtr;
		if ( after == it->second._before )
		{
			s_mapPending.erase( it );
			return;
		}
		string*		 pTarget = it->second._pPtr;
		string		 before	 = std::move( it->second._before );
		const string lbl	 = it->second._label;
		s_mapPending.erase( it );

		const uint64 selectedId = EditorContext::get()->getWorkspace().getSelectedObjectId();
		EditorInspectorCommands::pushStringEdit( pTarget, std::move( before ), std::move( after ), lbl, selectedId );
	}
} // namespace sw::editor
