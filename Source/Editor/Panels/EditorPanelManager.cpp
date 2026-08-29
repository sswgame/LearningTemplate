#include "pch.h"

#include "Editor/Panels/EditorPanelManager.h"

#include "Editor/Panels/AnimationGraphPanel.h"
#include "Editor/Panels/ConsolePanel.h"
#include "Editor/Panels/ContentBrowserPanel.h"
#include "Editor/Panels/DataTablePanel.h"
#include "Editor/Panels/DialogueGraphPanel.h"
#include "Editor/Panels/GameViewPanel.h"
#include "Editor/Panels/GlobalVariablesPanel.h"
#include "Editor/Panels/HierarchyPanel.h"
#include "Editor/Panels/HistoryPanel.h"
#include "Editor/Panels/InspectorPanel.h"
#include "Editor/Panels/PrefabEditorPanel.h"
#include "Editor/Panels/ProfilerPanel.h"
#include "Editor/Panels/SequencerPanel.h"
#include "Editor/Panels/SpriteClipPanel.h"
#include "Editor/Panels/TileMapPanel.h"

namespace sw::editor
{
	void EditorPanelManager::registerPanel( unique_ptr<IEditorPanel> pPanel,
											EditorPanelCategory category, string_view menuPath )
	{
		if ( pPanel == nullptr )
			return;

		const utf8*		 pTitle = pPanel->getPanelTitle();
		EditorPanelEntry entry{};
		entry._title	 = pTitle != nullptr ? pTitle : "";
		entry._menuPath	 = menuPath.empty() == false ? string{ menuPath } : entry._title;
		entry._category	 = category;
		entry._pInstance = std::move( pPanel );

		_listPanel.push_back( std::move( entry ) );
	}

	IEditorPanel* EditorPanelManager::findPanel( string_view title ) const
	{
		for ( const EditorPanelEntry& entry : _listPanel )
		{
			if ( entry._pInstance == nullptr )
				continue;
			if ( entry._title == title || entry._menuPath == title )
				return entry._pInstance.get();
		}
		return nullptr;
	}

	bool EditorPanelManager::setPanelOpen( string_view title, bool bOpen )
	{
		IEditorPanel* pPanel = findPanel( title );
		if ( pPanel != nullptr )
		{
			pPanel->setOpen( bOpen );
			return true;
		}
		return false;
	}

	void EditorPanelManager::clear()
	{
		_listPanel.clear();
	}

	void EditorPanelManager::registerDefaultPanels()
	{
		clear();

		// 핵심 패널 (Core)
		registerPanel( make_unique<HierarchyPanel>(), EditorPanelCategory::Core );
		registerPanel( make_unique<InspectorPanel>(), EditorPanelCategory::Core );
		registerPanel( make_unique<GameViewPanel>(), EditorPanelCategory::Core );
		registerPanel( make_unique<ConsolePanel>(), EditorPanelCategory::Core );
		registerPanel( make_unique<ProfilerPanel>(), EditorPanelCategory::Core );
		registerPanel( make_unique<ContentBrowserPanel>(), EditorPanelCategory::Core );

		registerPanel( make_unique<HistoryPanel>(), EditorPanelCategory::Tool );
		registerPanel( make_unique<GlobalVariablesPanel>(), EditorPanelCategory::Tool );
		registerPanel( make_unique<SequencerPanel>(), EditorPanelCategory::Tool );
		registerPanel( make_unique<AnimationGraphPanel>(), EditorPanelCategory::Tool );
		registerPanel( make_unique<DialogueGraphPanel>(), EditorPanelCategory::Tool );
		registerPanel( make_unique<PrefabEditorPanel>(), EditorPanelCategory::Tool );
		registerPanel( make_unique<TileMapPanel>(), EditorPanelCategory::Tool );
		registerPanel( make_unique<SpriteClipPanel>(), EditorPanelCategory::Tool );
		registerPanel( make_unique<DataTablePanel>(), EditorPanelCategory::Tool );
	}

	void EditorPanelManager::drawOpenPanels()
	{
		for ( const EditorPanelEntry& entry : _listPanel )
		{
			if ( entry._pInstance != nullptr && entry._pInstance->isOpen() )
				entry._pInstance->draw();
		}
	}

	void EditorPanelManager::preRenderOpenPanels( IRHIDevice* pRhiDevice )
	{
		for ( const EditorPanelEntry& entry : _listPanel )
		{
			if ( entry._pInstance != nullptr && entry._pInstance->isOpen() )
				entry._pInstance->preRender( pRhiDevice );
		}
	}

	void EditorPanelManager::shutdownAllPanels( IRHIDevice* pRhiDevice )
	{
		for ( const EditorPanelEntry& entry : _listPanel )
		{
			if ( entry._pInstance != nullptr )
				entry._pInstance->shutdown( pRhiDevice );
		}
	}

	bool EditorPanelManager::saveFocusedDirtyDocument()
	{
		for ( const EditorPanelEntry& entry : _listPanel )
		{
			if ( entry._pInstance == nullptr || entry._pInstance->isOpen() == false )
				continue;
			if ( entry._pInstance->isWindowFocused() == false )
				continue;
			if ( entry._pInstance->trySaveDirtyDocument() )
				return true;
		}
		return false;
	}
} // namespace sw::editor
