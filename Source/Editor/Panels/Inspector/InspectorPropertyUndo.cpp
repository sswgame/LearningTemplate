#include "pch.h"

#include "Editor/Panels/Inspector/InspectorPropertyUndo.h"

#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Utility/CommandStack.h"

#include "RuntimeAPI/Service/EditorService.h"

#include <imgui.h>

namespace sw::editor
{
	void trackPodPropertyUndo( void* pData, size_t size, const utf8* pLabel )
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
			e._listBefore.assign( static_cast<const uint8*>( pData ), static_cast<const uint8*>( pData ) + size );
			s_mapPending[id] = std::move( e );
		}
		if ( ImGui::IsItemDeactivatedAfterEdit() == false )
			return;

		const auto it = s_mapPending.find( id );
		if ( it == s_mapPending.end() )
			return;

		vector<uint8> after( static_cast<const uint8*>( pData ), static_cast<const uint8*>( pData ) + size );
		if ( after == it->second._listBefore )
		{
			s_mapPending.erase( it );
			return;
		}

		void*		  pPtr		 = it->second._pPtr;
		const size_t  sz		 = it->second._size;
		vector<uint8> listBefore = std::move( it->second._listBefore );
		const string  lbl		 = string( "Edit " ) + it->second._label;
		s_mapPending.erase( it );

		uint64				  selectedId = EditorWorkspace::selectedObjectId();
		CommandStack::Command cmd;
		cmd._label = lbl;
		cmd._undo  = [pPtr, sz, listBefore, selectedId]()
		{
			if ( pPtr != nullptr && ( selectedId == 0 || EditorWorkspace::selectedObjectId() == selectedId ) )
				Memory::copy( pPtr, listBefore.data(), sz );
		};
		cmd._redo = [pPtr, sz, after, selectedId]()
		{
			if ( pPtr != nullptr && ( selectedId == 0 || EditorWorkspace::selectedObjectId() == selectedId ) )
				Memory::copy( pPtr, after.data(), sz );
		};
		editor::getService<CommandStack>()->push( std::move( cmd ) );
	}

	void trackStringPropertyUndo( string* pPtr, const utf8* pLabel )
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
		const string lbl	 = string( "Edit " ) + it->second._label;
		s_mapPending.erase( it );

		uint64				  selectedId = EditorWorkspace::selectedObjectId();
		CommandStack::Command cmd;
		cmd._label = lbl;
		cmd._undo  = [pTarget, before, selectedId]()
		{
			if ( pTarget != nullptr && ( selectedId == 0 || EditorWorkspace::selectedObjectId() == selectedId ) )
				*pTarget = before;
		};
		cmd._redo = [pTarget, after, selectedId]()
		{
			if ( pTarget != nullptr && ( selectedId == 0 || EditorWorkspace::selectedObjectId() == selectedId ) )
				*pTarget = after;
		};
		editor::getService<CommandStack>()->push( std::move( cmd ) );
	}
} // namespace sw::editor
