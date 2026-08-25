#include "pch.h"

#include "Editor/Windows/EditorWindowRegistry.h"

#include "Editor/Common/EditorContext.h"
#include "Editor/Tools/AnimationGraphTool.h"
#include "Editor/Tools/DialogueGraphTool.h"
#include "Editor/Tools/PrefabEditorTool.h"
#include "Editor/Tools/SequencerTool.h"
#include "Editor/Tools/SpriteClipTool.h"
#include "Editor/Tools/TileMapTool.h"
#include "Editor/Windows/ConsoleWindow.h"
#include "Editor/Windows/ContentBrowserWindow.h"
#include "Editor/Windows/GameViewWindow.h"
#include "Editor/Windows/HierarchyWindow.h"
#include "Editor/Windows/InspectorWindow.h"
#include "Editor/Windows/ProfilerWindow.h"

namespace sw
{
	namespace
	{
		EditorWindowRegistry* getImpl()
		{
			EditorContext* pContext = EditorContext::get();
			if ( pContext != nullptr )
				return &pContext->getWindowRegistry();
			return nullptr;
		}

		const vector<EditorWindowEntry> s_emptyEntries{};
		vector<EditorWindowEntry>		s_emptyEntriesMutable{};
	} // namespace

	// ------------------------------------------------------------------------------
	// Static Public Methods
	// ------------------------------------------------------------------------------
	void EditorWindowRegistry::registerWindow( unique_ptr<IEditorWindow> pWindow,
											   EditorWindowCategory category, string_view menuPath )
	{
		EditorWindowRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			pRegistry->registerWindowImpl( std::move( pWindow ), category, menuPath );
	}

	const vector<EditorWindowEntry>& EditorWindowRegistry::getWindows()
	{
		EditorWindowRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			return pRegistry->getWindowsImpl();
		return s_emptyEntries;
	}

	vector<EditorWindowEntry>& EditorWindowRegistry::getWindowsMutable()
	{
		EditorWindowRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			return pRegistry->getWindowsImpl();
		return s_emptyEntriesMutable;
	}

	IEditorWindow* EditorWindowRegistry::findWindow( string_view title )
	{
		EditorWindowRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			return pRegistry->findWindowImpl( title );
		return nullptr;
	}

	bool EditorWindowRegistry::setWindowOpen( string_view title, bool bOpen )
	{
		EditorWindowRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			return pRegistry->setWindowOpenImpl( title, bOpen );
		return false;
	}

	void EditorWindowRegistry::clear()
	{
		EditorWindowRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			pRegistry->clearImpl();
	}

	void EditorWindowRegistry::registerDefaultWindows()
	{
		EditorWindowRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			pRegistry->registerDefaultWindowsImpl();
	}

	// ------------------------------------------------------------------------------
	// Instance Implementations
	// ------------------------------------------------------------------------------
	void EditorWindowRegistry::registerWindowImpl( unique_ptr<IEditorWindow> pWindow,
												   EditorWindowCategory category, string_view menuPath )
	{
		if ( pWindow == nullptr )
			return;

		const utf8*		  pTitle = pWindow->getWindowTitle();
		EditorWindowEntry entry{};
		entry._title	 = pTitle != nullptr ? pTitle : "";
		entry._menuPath	 = menuPath.empty() == false ? string{ menuPath } : entry._title;
		entry._category	 = category;
		entry._pInstance = std::move( pWindow );

		_listWindows.push_back( std::move( entry ) );
	}

	IEditorWindow* EditorWindowRegistry::findWindowImpl( string_view title ) const
	{
		for ( const EditorWindowEntry& entry : _listWindows )
		{
			if ( entry._title == title && entry._pInstance != nullptr )
				return entry._pInstance.get();
		}
		return nullptr;
	}

	bool EditorWindowRegistry::setWindowOpenImpl( string_view title, bool bOpen )
	{
		IEditorWindow* pWindow = findWindowImpl( title );
		if ( pWindow != nullptr )
		{
			pWindow->setOpen( bOpen );
			return true;
		}
		return false;
	}

	void EditorWindowRegistry::clearImpl()
	{
		_listWindows.clear();
	}

	void EditorWindowRegistry::registerDefaultWindowsImpl()
	{
		clearImpl();

		// 핵심 윈도우 (Core)
		registerWindowImpl( make_unique<HierarchyWindow>(), EditorWindowCategory::Core, "Hierarchy" );
		registerWindowImpl( make_unique<InspectorWindow>(), EditorWindowCategory::Core, "Inspector" );
		registerWindowImpl( make_unique<GameViewWindow>(), EditorWindowCategory::Core, "Game View" );
		registerWindowImpl( make_unique<ConsoleWindow>(), EditorWindowCategory::Core, "Console" );
		registerWindowImpl( make_unique<ProfilerWindow>(), EditorWindowCategory::Core, "Profiler" );
		registerWindowImpl( make_unique<ContentBrowserWindow>(), EditorWindowCategory::Core, "Content Browser" );

		// 온디맨드 도구 (Tool)
		registerWindowImpl( make_unique<SequencerTool>(), EditorWindowCategory::Tool, "Sequencer" );
		registerWindowImpl( make_unique<AnimationGraphTool>(), EditorWindowCategory::Tool, "Animation Graph" );
		registerWindowImpl( make_unique<DialogueGraphTool>(), EditorWindowCategory::Tool, "Dialogue Graph" );
		registerWindowImpl( make_unique<PrefabEditorTool>(), EditorWindowCategory::Tool, "Prefab Editor" );
		registerWindowImpl( make_unique<TileMapTool>(), EditorWindowCategory::Tool, "Tile Map Tool" );
		registerWindowImpl( make_unique<SpriteClipTool>(), EditorWindowCategory::Tool, "Sprite Clip" );
	}
} // namespace sw
