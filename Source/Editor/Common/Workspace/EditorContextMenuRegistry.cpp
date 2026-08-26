#include "pch.h"

#include "Editor/Common/Workspace/EditorContextMenuRegistry.h"

#include "Editor/Common/Workspace/EditorContext.h"

#include <imgui.h>

namespace sw::editor
{
	namespace
	{
		EditorContextMenuRegistry* getImpl()
		{
			EditorContext* pContext = EditorContext::get();
			if ( pContext != nullptr )
				return &pContext->getContextMenuRegistry();
			return nullptr;
		}
	} // namespace

	void EditorContextMenuRegistry::registerItem( ContextMenuLocation location, string_view path,
												  Delegate<void()> action, string_view shortcut,
												  Delegate<bool()> enabledPredicate )
	{
		EditorContextMenuRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			pRegistry->registerItemImpl( location, path, std::move( action ), shortcut, std::move( enabledPredicate ) );
	}

	void EditorContextMenuRegistry::drawContextMenu( ContextMenuLocation location )
	{
		EditorContextMenuRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			pRegistry->drawContextMenuImpl( location );
	}

	void EditorContextMenuRegistry::registerItemImpl( ContextMenuLocation location, string_view path,
													  Delegate<void()> action, string_view shortcut,
													  Delegate<bool()> enabledPredicate )
	{
		const size_t locIdx = static_cast<size_t>( location );
		if ( locIdx >= 4 )
			return;

		ContextMenuItem item;
		item._path			   = string{ path };
		item._shortcut		   = string{ shortcut };
		item._action		   = std::move( action );
		item._enabledPredicate = std::move( enabledPredicate );

		_mapItems[locIdx].push_back( std::move( item ) );
	}

	void EditorContextMenuRegistry::drawContextMenuImpl( ContextMenuLocation location )
	{
		const size_t locIdx = static_cast<size_t>( location );
		if ( locIdx >= 4 || _mapItems[locIdx].empty() )
			return;

		for ( const ContextMenuItem& item : _mapItems[locIdx] )
		{
			bool bEnabled = true;
			if ( item._enabledPredicate.isBound() )
				bEnabled = item._enabledPredicate();

			const utf8* pShortcut = item._shortcut.empty() ? nullptr : item._shortcut.c_str();

			// Submenu support (e.g. "Create/3D Object")
			const size_t slashPos = item._path.find( '/' );
			if ( slashPos != string::npos )
			{
				const string subMenu = item._path.substr( 0, slashPos );
				const string subItem = item._path.substr( slashPos + 1 );
				if ( ImGui::BeginMenu( subMenu.c_str(), bEnabled ) )
				{
					if ( ImGui::MenuItem( subItem.c_str(), pShortcut, false, bEnabled ) )
					{
						if ( item._action.isBound() )
							item._action();
					}
					ImGui::EndMenu();
				}
			}
			else
			{
				if ( ImGui::MenuItem( item._path.c_str(), pShortcut, false, bEnabled ) )
				{
					if ( item._action.isBound() )
						item._action();
				}
			}
		}
	}
} // namespace sw::editor
