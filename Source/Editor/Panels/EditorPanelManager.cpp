#include "pch.h"

#include "Editor/Panels/EditorPanelManager.h"

#include "Editor/Common/Gui/IEditorPanel.h"

namespace sw::editor
{
    void EditorPanelManager::registerPanel( unique_ptr<IEditorPanel> pPanel,
                                            string_view              panelId,
                                            EditorPanelCategory      category )
    {
        if ( pPanel == nullptr )
            return;

        const utf8*      pTitle = pPanel->getPanelTitle();
        EditorPanelEntry entry{};
        entry._id        = panelId.empty() == false ? string{ panelId } : ( pTitle != nullptr ? pTitle : "" );
        entry._title     = pTitle != nullptr ? pTitle : "";
        entry._category  = category;
        entry._pInstance = std::move( pPanel );

        _listPanel.push_back( std::move( entry ) );
    }

    IEditorPanel* EditorPanelManager::findPanel( string_view panelId ) const
    {
        for ( const EditorPanelEntry& entry : _listPanel )
        {
            if ( entry._pInstance == nullptr )
                continue;
            if ( entry._id == panelId || entry._title == panelId )
                return entry._pInstance.get();
        }
        return nullptr;
    }

    bool EditorPanelManager::setPanelOpen( string_view panelId, bool bOpen )
    {
        IEditorPanel* pPanel = findPanel( panelId );
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

        using PanelRegistry = EditorRegistry<EditorPanelRegistration>;
        for ( uint32 index = 0; index < PanelRegistry::getCount(); ++index )
        {
            const EditorPanelRegistration& registration = PanelRegistry::getAt( index );
            registerPanel( registration._pCreate(), registration._pId, registration._category );
        }
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

    bool EditorPanelManager::saveAllDirtyDocuments()
    {
        bool bAllOk{ true };
        for ( const EditorPanelEntry& entry : _listPanel )
        {
            if ( entry._pInstance == nullptr )
                continue;
            if ( entry._pInstance->isDocumentDirty() == false )
                continue;
            if ( entry._pInstance->trySaveDirtyDocument() == false )
                bAllOk = false;
        }
        return bAllOk;
    }

    uint32 EditorPanelManager::countDirtyDocuments() const
    {
        uint32 count{ 0 };
        for ( const EditorPanelEntry& entry : _listPanel )
        {
            if ( entry._pInstance == nullptr )
                continue;
            if ( entry._pInstance->isDocumentDirty() )
                ++count;
        }
        return count;
    }

    void EditorPanelManager::discardAllDirtyDocuments()
    {
        for ( const EditorPanelEntry& entry : _listPanel )
        {
            if ( entry._pInstance == nullptr )
                continue;
            entry._pInstance->discardDirtyDocument();
        }
    }
} // namespace sw::editor
