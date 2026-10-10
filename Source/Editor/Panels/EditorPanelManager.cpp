#include "pch.h"

#include "Editor/Panels/EditorPanelManager.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Module/ModuleUnloadListener.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/GUI/IEditorPanel.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorPanelManager" );

    namespace
    {
        struct EditorPanelManagerInternal
        {
            /** @brief 그 등록 줄로 만든 항목입니다. 없으면 nullptr 입니다. */
            static EditorPanelEntry* findEntryByRegistration( vector<EditorPanelEntry>& listPanel, const EditorPanelRegistration* pRegistration )
            {
                for ( EditorPanelEntry& entry : listPanel )
                {
                    if ( entry._pRegistration == pRegistration && entry._pInstance != nullptr )
                        return &entry;
                }
                return nullptr;
            }

            /** @brief 기억한 열린 id 목록에 @p id 가 있으면 빼고 true 입니다. */
            static bool takeRemembered( vector<string>& listRememberedID, const string& id )
            {
                for ( size_t index = 0; index < listRememberedID.size(); ++index )
                {
                    if ( listRememberedID[index] != id )
                        continue;
                    listRememberedID.erase( listRememberedID.begin() + static_cast<ptrdiff_t>( index ) );
                    return true;
                }
                return false;
            }
        };
    } // namespace

    // `-gv_editorPanelTimes=N` — N 프레임 동안 패널마다 그리기 시간을 모아 한 번 찍는다(평균 · 최대 us, 큰 순). 프로파일러 칸을 패널 수만큼 쓰지 않는다.
    SW_TEST_GLOBAL_VARIABLE( int32, gv_editorPanelTimes, 0, "N 프레임 동안 에디터 패널마다 그리기 시간을 모아 한 번 로그로 찍는다 (0=끄기)" );

    EditorPanelManager::EditorPanelManager()
        : _listPanel{}
        , _listRememberedOpenID{}
        , _syncedGeneration{ kNotSynced }
        , _panelTimeFrameCount{ 0 }
        , _bPanelTimesReported{ false }
    {
    }

    EditorPanelManager::~EditorPanelManager() = default;

    void EditorPanelManager::registerPanel( unique_ptr<IEditorPanel> pPanel,
                                            string_view              panelID,
                                            EditorPanelCategory      category )
    {
        if ( pPanel == nullptr )
            return;

        const utf8*      pTitle = pPanel->getPanelTitle();
        EditorPanelEntry entry{};
        entry._id        = panelID.empty() == false ? string{ panelID } : ( pTitle != nullptr ? pTitle : "" );
        entry._title     = pTitle != nullptr ? pTitle : "";
        entry._category  = category;
        entry._pInstance = std::move( pPanel );

        _listPanel.push_back( std::move( entry ) );
    }

    IEditorPanel* EditorPanelManager::findPanel( string_view panelID ) const
    {
        for ( const EditorPanelEntry& entry : _listPanel )
        {
            if ( entry._pInstance == nullptr )
                continue;
            if ( entry._id == panelID || entry._title == panelID )
                return entry._pInstance.get();
        }
        return nullptr;
    }

    bool EditorPanelManager::setPanelOpen( string_view panelID, bool bOpen )
    {
        IEditorPanel* pPanel = findPanel( panelID );
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
        _syncedGeneration = kNotSynced;
        syncWithRegistry( nullptr );
    }

    void EditorPanelManager::syncWithRegistry( IRHIDevice* pRHIDevice )
    {
        using PanelRegistry                = EditorRegistry<EditorPanelRegistration>;
        const EditorRegistrationList& list = PanelRegistry::getList();
        if ( list.getGeneration() == _syncedGeneration )
            return;
        _syncedGeneration = list.getGeneration();

        // 1) 등록 목록에서 사라진 줄의 인스턴스를 종료한다(정상 경로에서는 releasePanelsWithin 이 먼저 했다 — 여기는 줄을 직접 뺀 경우)
        for ( size_t index = _listPanel.size(); index-- > 0; )
        {
            EditorPanelEntry& entry = _listPanel[index];
            if ( entry._pRegistration == nullptr || PanelRegistry::find( entry._id ) == entry._pRegistration )
                continue;
            if ( entry._pInstance != nullptr )
                entry._pInstance->shutdown( pRHIDevice );
            _listPanel.erase( _listPanel.begin() + static_cast<ptrdiff_t>( index ) );
        }
        // 2) 등록 순서대로 다시 줄 세우고 새 줄은 만든다 — 그리기 순서와 Panel 메뉴 순서가 등록 목록과 같다
        vector<EditorPanelEntry> listSorted;
        listSorted.reserve( PanelRegistry::getCount() + _listPanel.size() );
        for ( uint32 index = 0; index < PanelRegistry::getCount(); ++index )
        {
            const EditorPanelRegistration& registration = PanelRegistry::getAt( index );
            EditorPanelEntry*              pExisting    = EditorPanelManagerInternal::findEntryByRegistration( _listPanel, &registration );
            if ( pExisting != nullptr )
            {
                listSorted.push_back( std::move( *pExisting ) );
                pExisting->_pRegistration = nullptr; // 옮긴 자리 — 3) 에서 다시 옮기지 않게
                continue;
            }
            unique_ptr<IEditorPanel> pPanel = registration._pCreate();
            if ( pPanel == nullptr )
                continue;
            const utf8*      pTitle = pPanel->getPanelTitle();
            EditorPanelEntry entry{};
            entry._id            = registration._pID;
            entry._title         = pTitle != nullptr ? pTitle : "";
            entry._category      = registration._category;
            entry._pRegistration = &registration;
            entry._pInstance     = std::move( pPanel );
            if ( EditorPanelManagerInternal::takeRemembered( _listRememberedOpenID, entry._id ) )
                entry._pInstance->setOpen( true );
            listSorted.push_back( std::move( entry ) );
        }
        // 3) 등록 줄 없이 직접 넣은 패널(registerPanel)은 뒤에 그대로 둔다
        for ( EditorPanelEntry& entry : _listPanel )
        {
            if ( entry._pRegistration == nullptr && entry._pInstance != nullptr )
                listSorted.push_back( std::move( entry ) );
        }
        _listPanel = std::move( listSorted );
    }

    uint32 EditorPanelManager::releasePanelsWithin( const void* pBegin, const void* pEnd, IRHIDevice* pRHIDevice )
    {
        uint32 releasedCount{ 0 };
        for ( size_t index = _listPanel.size(); index-- > 0; )
        {
            EditorPanelEntry& entry = _listPanel[index];
            if ( IModuleUnloadListener::isAddressWithin( entry._pRegistration, pBegin, pEnd ) == false )
                continue;
            if ( entry._pInstance != nullptr )
            {
                if ( entry._pInstance->isDocumentDirty() )
                    SW_LOG_WARNING( "Panel '%#' had unsaved edits - discarded because its module is unloading", entry._title.c_str() );
                if ( entry._pInstance->isOpen() )
                    _listRememberedOpenID.push_back( entry._id );
                entry._pInstance->shutdown( pRHIDevice );
            }
            _listPanel.erase( _listPanel.begin() + static_cast<ptrdiff_t>( index ) );
            ++releasedCount;
        }
        return releasedCount;
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
