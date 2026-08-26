#include "pch.h"

#include "Editor/Panels/EditorPanelRegistry.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Tools/AnimationGraphTool.h"
#include "Editor/Tools/DialogueGraphTool.h"
#include "Editor/Tools/PrefabEditorTool.h"
#include "Editor/Tools/SequencerTool.h"
#include "Editor/Tools/SpriteClipTool.h"
#include "Editor/Tools/TileMapTool.h"
#include "Editor/Panels/ConsolePanel.h"
#include "Editor/Panels/ContentBrowserPanel.h"
#include "Editor/Panels/GameViewPanel.h"
#include "Editor/Panels/GlobalVariablesPanel.h"
#include "Editor/Panels/HierarchyPanel.h"
#include "Editor/Panels/InspectorPanel.h"
#include "Editor/Panels/ProfilerPanel.h"

namespace sw
{
	namespace
	{
		EditorPanelRegistry* getImpl()
		{
			EditorContext* pContext = EditorContext::get();
			if ( pContext != nullptr )
				return &pContext->getPanelRegistry();
			return nullptr;
		}

		const vector<EditorPanelEntry> s_emptyEntries{};
		vector<EditorPanelEntry>		s_emptyEntriesMutable{};
	} // namespace

	// ------------------------------------------------------------------------------
	// Static Public Methods
	// ------------------------------------------------------------------------------
	void EditorPanelRegistry::registerPanel( unique_ptr<IEditorPanel> pPanel,
											   EditorPanelCategory category, string_view menuPath )
	{
		EditorPanelRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			pRegistry->registerPanelImpl( std::move( pPanel ), category, menuPath );
	}

	const vector<EditorPanelEntry>& EditorPanelRegistry::getPanels()
	{
		EditorPanelRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			return pRegistry->getPanelsImpl();
		return s_emptyEntries;
	}

	vector<EditorPanelEntry>& EditorPanelRegistry::getPanelsMutable()
	{
		EditorPanelRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			return pRegistry->getPanelsImpl();
		return s_emptyEntriesMutable;
	}

	IEditorPanel* EditorPanelRegistry::findPanel( string_view title )
	{
		EditorPanelRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			return pRegistry->findPanelImpl( title );
		return nullptr;
	}

	bool EditorPanelRegistry::setPanelOpen( string_view title, bool bOpen )
	{
		EditorPanelRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			return pRegistry->setPanelOpenImpl( title, bOpen );
		return false;
	}

	void EditorPanelRegistry::clear()
	{
		EditorPanelRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			pRegistry->clearImpl();
	}

	void EditorPanelRegistry::registerDefaultPanels()
	{
		EditorPanelRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			pRegistry->registerDefaultPanelsImpl();
	}

	void EditorPanelRegistry::drawOpenPanels()
	{
		for ( const EditorPanelEntry& entry : getPanels() )
		{
			if ( entry._pInstance != nullptr && entry._pInstance->isOpen() )
				entry._pInstance->draw();
		}
	}

	void EditorPanelRegistry::preRenderOpenPanels( IRHIDevice* pRhiDevice )
	{
		for ( const EditorPanelEntry& entry : getPanels() )
		{
			if ( entry._pInstance != nullptr && entry._pInstance->isOpen() )
				entry._pInstance->preRender( pRhiDevice );
		}
	}

	void EditorPanelRegistry::shutdownAllPanels( IRHIDevice* pRhiDevice )
	{
		for ( const EditorPanelEntry& entry : getPanels() )
		{
			if ( entry._pInstance != nullptr )
				entry._pInstance->shutdown( pRhiDevice );
		}
	}

	// ------------------------------------------------------------------------------
	// Instance Implementations
	// ------------------------------------------------------------------------------
	void EditorPanelRegistry::registerPanelImpl( unique_ptr<IEditorPanel> pPanel,
												   EditorPanelCategory category, string_view menuPath )
	{
		if ( pPanel == nullptr )
			return;

		const utf8*		  pTitle = pPanel->getPanelTitle();
		EditorPanelEntry entry{};
		entry._title	 = pTitle != nullptr ? pTitle : "";
		entry._menuPath	 = menuPath.empty() == false ? string{ menuPath } : entry._title;
		entry._category	 = category;
		entry._pInstance = std::move( pPanel );

		_listPanels.push_back( std::move( entry ) );
	}

	IEditorPanel* EditorPanelRegistry::findPanelImpl( string_view title ) const
	{
		for ( const EditorPanelEntry& entry : _listPanels )
		{
			if ( entry._title == title && entry._pInstance != nullptr )
				return entry._pInstance.get();
		}
		return nullptr;
	}

	bool EditorPanelRegistry::setPanelOpenImpl( string_view title, bool bOpen )
	{
		IEditorPanel* pPanel = findPanelImpl( title );
		if ( pPanel != nullptr )
		{
			pPanel->setOpen( bOpen );
			return true;
		}
		return false;
	}

	void EditorPanelRegistry::clearImpl()
	{
		_listPanels.clear();
	}

	void EditorPanelRegistry::registerDefaultPanelsImpl()
	{
		clearImpl();

		// 핵심 패널 (Core)
		registerPanelImpl( make_unique<HierarchyPanel>(), EditorPanelCategory::Core, "Hierarchy" );
		registerPanelImpl( make_unique<InspectorPanel>(), EditorPanelCategory::Core, "Inspector" );
		registerPanelImpl( make_unique<GameViewPanel>(), EditorPanelCategory::Core, "Game View" );
		registerPanelImpl( make_unique<ConsolePanel>(), EditorPanelCategory::Core, "Console" );
		registerPanelImpl( make_unique<ProfilerPanel>(), EditorPanelCategory::Core, "Profiler" );
		registerPanelImpl( make_unique<ContentBrowserPanel>(), EditorPanelCategory::Core, "Content Browser" );

		// 온디맨드 도구 (Tool)
		registerPanelImpl( make_unique<GlobalVariablesPanel>(), EditorPanelCategory::Tool, "Global Variables" );
		registerPanelImpl( make_unique<SequencerTool>(), EditorPanelCategory::Tool, "Sequencer" );
		registerPanelImpl( make_unique<AnimationGraphTool>(), EditorPanelCategory::Tool, "Animation Graph" );
		registerPanelImpl( make_unique<DialogueGraphTool>(), EditorPanelCategory::Tool, "Dialogue Graph" );
		registerPanelImpl( make_unique<PrefabEditorTool>(), EditorPanelCategory::Tool, "Prefab Editor" );
		registerPanelImpl( make_unique<TileMapTool>(), EditorPanelCategory::Tool, "Tile Map Tool" );
		registerPanelImpl( make_unique<SpriteClipTool>(), EditorPanelCategory::Tool, "Sprite Clip" );
	}
} // namespace sw
