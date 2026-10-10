#include "pch.h"

#include "Editor/Panels/EditorPanelManager.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/GUI/IEditorPanel.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorPanelManager" );

    // `-gv_editorPanelTimes=N` — N 프레임 동안 패널마다 그리기 시간을 모아 한 번 찍는다(평균 · 최대 us, 큰 순). 프로파일러 칸을 패널 수만큼 쓰지 않는다.
    SW_TEST_GLOBAL_VARIABLE( int32, gv_editorPanelTimes, 0, "N 프레임 동안 에디터 패널마다 그리기 시간을 모아 한 번 로그로 찍는다 (0=끄기)" );

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
        const bool bTimed = gv_editorPanelTimes > 0 && _bPanelTimesReported == false;
        for ( EditorPanelEntry& entry : _listPanel )
        {
            if ( entry._pInstance == nullptr || entry._pInstance->isOpen() == false )
                continue;
            if ( bTimed == false )
            {
                entry._pInstance->draw();
                continue;
            }
            const Stopwatch stopwatch;
            entry._pInstance->draw();
            const uint64 elapsedNanos = static_cast<uint64>( stopwatch.getElapsedNanoseconds() );
            entry._drawNanosSum += elapsedNanos;
            entry._drawNanosMax = MathUtil::max( entry._drawNanosMax, elapsedNanos );
        }
        if ( bTimed && ++_panelTimeFrameCount >= static_cast<uint32>( gv_editorPanelTimes ) )
            reportPanelTimes();
    }

    void EditorPanelManager::reportPanelTimes()
    {
        _bPanelTimesReported = true;
        vector<const EditorPanelEntry*> listEntry;
        for ( const EditorPanelEntry& entry : _listPanel )
        {
            if ( entry._drawNanosSum > 0 )
                listEntry.push_back( &entry );
        }
        std::sort( listEntry.begin(), listEntry.end(),
                   []( const EditorPanelEntry* pLeft, const EditorPanelEntry* pRight )
        { return pLeft->_drawNanosSum > pRight->_drawNanosSum; } );
        const uint64 frameCount = MathUtil::max<uint64>( 1u, _panelTimeFrameCount );
        for ( const EditorPanelEntry* pEntry : listEntry )
        {
            SW_LOG_INFO( "EditorPanelTime|%#|avg %# us|max %# us", pEntry->_id.c_str(), pEntry->_drawNanosSum / 1000ull / frameCount, pEntry->_drawNanosMax / 1000ull );
        }
    }

    void EditorPanelManager::preRenderOpenPanels( IRHIDevice* pRHIDevice )
    {
        for ( const EditorPanelEntry& entry : _listPanel )
        {
            if ( entry._pInstance != nullptr && entry._pInstance->isOpen() )
                entry._pInstance->preRender( pRHIDevice );
        }
    }

    void EditorPanelManager::shutdownAllPanels( IRHIDevice* pRHIDevice )
    {
        for ( const EditorPanelEntry& entry : _listPanel )
        {
            if ( entry._pInstance != nullptr )
                entry._pInstance->shutdown( pRHIDevice );
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
