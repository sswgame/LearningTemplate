#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorPanelManager.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/vector.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Commands/Asset/EditorDocumentAutosave.h"
#include "Editor/Common/Commands/Scene/EditorAutosaveCommands.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/EditorExports.h"
#include "Editor/Common/GUI/Panel/EditorDocumentPanelBase.h"
#include "Editor/Common/GUI/Panel/EditorPanel.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/SelfTest/EditorWatchSupport.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/ContentHash.h"

// 문서 패널 자동 저장의 주기입니다 — 열린 문서 패널의 사본(`EditorDocumentPanelBase::takeAutosaveCopies`)을 주기마다 모아 워커로 씁니다(감사 E-1). ImGui 를 그리지 않는다.
namespace sw::editor
{
    namespace
    {

        /**
         * @struct EditorDocumentAutosaveScheduler
         * @brief 주기(`EditorToolDefaults::_documentAutosaveSeconds` — 설정 필수)가 지나면 dirty 문서의 사본을 모아 쓰기 잡(`EditorDocumentAutosaveWriteJob`)에 넘깁니다.
         * @details 상호작용 구간 중인 문서(`DocumentAutosaveResult::Deferred`)는 이 판에서 빼고 주기를 다시 재지 않는다 — 구간이 끝난 첫 프레임에 그 문서만 다시 나온다(다른 문서는 글이 같아 다시 나오지 않는다).
         *          낸 잡이 끝나지 않았으면 내지 않고 다음 프레임에 다시 본다(앞 묶음을 새 세대가 버리지 않게). 사본 폴더는 씬 자동 저장의 세션 폴더다 — 세션이 없으면 쓰지 않는다.
         *          상태(주기 시계 · 잡)는 에디터 상태 저장소가 든다. 패널 관리자가 패널을 다 그린 뒤 프레임마다 부른다.
         */
        struct EditorDocumentAutosaveScheduler
        {
            /**
             * @brief 패널 목록 @p listPanel(패널 관리자의 줄 그대로) 을 보고 주기가 되었으면 사본을 냅니다.
             * @details 주기가 되지 않은 프레임(거의 모든 프레임)은 목록을 읽지 않고 돌아온다 — 예전에는 부르는 쪽이 프레임마다 패널 포인터 목록을 새로 지어 넘겼다.
             */
            static void update( const vector<EditorPanelEntry>& listPanel );
            /** @brief 이번 세션에 워커가 쓴 문서 사본 수입니다(감시 값 `Editor.AutosaveDocumentCopyCount`). */
            static uint32 getWrittenCopyCount();
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    namespace
    {
        struct EditorPanelManagerInternal
        {
            /** @brief 그 등록 줄로 만든 항목입니다. 없으면 nullptr 입니다. */
            static EditorPanelEntry* findEntryByRegistration( vector<EditorPanelEntry>& listPanel, const EditorPanelRegistration* pRegistration )
            {
                return VectorUtil::findIf( listPanel, [&]( EditorPanelEntry& entry )
                { return entry._pRegistration == pRegistration && entry._pInstance != nullptr; } );
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
        , _bPanelTimesReported{ SW_FALSE }
    {
    }

    EditorPanelManager::~EditorPanelManager() = default;

    EditorPanelManager* EditorPanelManager::findInEditorContext()
    {
        EditorContext* const pContext = editor::findSubsystem<EditorContext>();
        return pContext != nullptr ? &pContext->getSubsystem<EditorPanelManager>() : nullptr;
    }

    EditorPanelManager& EditorPanelManager::getInEditorContext()
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<EditorPanelManager>();
    }

    void EditorPanelManager::registerPanel( unique_ptr<EditorPanel> pPanel,
                                            string_view             panelID,
                                            EditorPanelCategory     category )
    {
        if ( pPanel == nullptr )
            return;

        const utf8*      pTitle = pPanel->getPanelTitle();
        EditorPanelEntry entry{};
        entry._id             = panelID.empty() == false ? string{ panelID } : ( pTitle != nullptr ? pTitle : "" );
        entry._title          = pTitle != nullptr ? pTitle : "";
        entry._category       = category;
        entry._bOpenByDefault = pPanel->isOpen();
        entry._pInstance      = std::move( pPanel );

        _listPanel.push_back( std::move( entry ) );
    }

    EditorPanel* EditorPanelManager::findPanelByID( string_view panelID ) const
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

    EditorPanel* EditorPanelManager::findDocumentPanel( const EditorAssetType& kind ) const
    {
        for ( const EditorPanelEntry& entry : _listPanel )
        {
            if ( entry._pInstance != nullptr && entry._pInstance->getDocumentKind() == kind )
                return entry._pInstance.get();
        }
        return nullptr;
    }

    bool EditorPanelManager::setPanelOpen( string_view panelID, bool bOpen )
    {
        EditorPanel* pPanel = findPanelByID( panelID );
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
        syncWithRegistry();
    }

    void EditorPanelManager::syncWithRegistry()
    {
        using PanelRegistry                         = EditorExtensionRegistry<EditorPanelRegistration>;
        const EditorExtensionRegistrationList& list = PanelRegistry::getList();
        if ( list.getGeneration() == _syncedGeneration )
            return;
        _syncedGeneration = list.getGeneration();

        // 1) 등록 목록에서 사라진 줄의 인스턴스를 종료한다(모듈을 내리기 전 — 줄이 빠진 그 자리에서 불린다)
        for ( size_t index = _listPanel.size(); index-- > 0; )
        {
            EditorPanelEntry& entry = _listPanel[index];
            if ( entry._pRegistration == nullptr || PanelRegistry::find( entry._id ) == entry._pRegistration )
                continue;
            if ( entry._pInstance != nullptr )
            {
                if ( entry._pInstance->hasUnsavedDocument() )
                    SW_LOG_WARNING_ID( "EditorPanelManager.PanelUnsavedEditsDiscarded", "Panel '%#' had unsaved edits - discarded because its module is unloading", entry._title.c_str() );
                if ( entry._pInstance->isOpen() )
                    _listRememberedOpenID.push_back( entry._id );
                entry._pInstance->shutdown();
            }
            _listPanel.erase( _listPanel.begin() + static_cast<ptrdiff_t>( index ) );
        }
        // 2) 등록 순서대로 다시 줄 세우고 새 줄은 만든다 — 그리기 순서와 Panel 메뉴 순서가 등록 목록과 같다
        vector<EditorPanelEntry> listSorted;
        listSorted.reserve( PanelRegistry::getCount() + _listPanel.size() );
        uint64 createNanos{ 0 };
        uint32 createdCount{ 0 };
        for ( uint32 index = 0; index < PanelRegistry::getCount(); ++index )
        {
            const EditorPanelRegistration& registration = PanelRegistry::getAt( index );
            EditorPanelEntry*              pExisting    = EditorPanelManagerInternal::findEntryByRegistration( _listPanel, &registration );
            if ( pExisting != nullptr )
            {
                listSorted.push_back( std::move( *pExisting ) );
                pExisting->_pRegistration = nullptr; // 옮긴 항목 — 3) 에서 다시 옮기지 않게
                continue;
            }
            const Stopwatch         createStopwatch; // 패널 지연 생성의 판정 근거 — 닫힌 패널까지 모두 만드는 비용(감시 값 `Editor.PanelCreateMs`)
            unique_ptr<EditorPanel> pPanel = registration._pCreate();
            createNanos += static_cast<uint64>( createStopwatch.getElapsedNanoseconds() );
            if ( pPanel == nullptr )
                continue;
            ++createdCount;
            const utf8*      pTitle = pPanel->getPanelTitle();
            EditorPanelEntry entry{};
            entry._id             = registration._pID;
            entry._title          = pTitle != nullptr ? pTitle : "";
            entry._category       = registration._category;
            entry._pRegistration  = &registration;
            entry._bOpenByDefault = pPanel->isOpen();
            entry._pInstance      = std::move( pPanel );
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
        if ( createdCount > 0 )
        {
            _lastPanelCreateNanos = createNanos;
            SW_LOG_INFO_ID( "EditorPanelManager.PanelsCreated", "Editor panels created: %# in %# us (closed ones included)", createdCount,
                            createNanos / static_cast<uint64>( constant::time::kNanosecondsPerMicrosecond ) );
        }
    }

    uint32 EditorPanelManager::resetOptionalPanels()
    {
        return resetPanels( false );
    }

    uint32 EditorPanelManager::resetAllPanels()
    {
        return resetPanels( true );
    }

    uint32 EditorPanelManager::resetPanels( bool bIncludeCore )
    {
        uint32 resetCount{ 0 };
        for ( EditorPanelEntry& entry : _listPanel )
        {
            if ( entry._pInstance == nullptr || ( entry._category == EditorPanelCategory::Core && bIncludeCore == false ) )
                continue;
            const bool bOpenAfterReset = entry._category == EditorPanelCategory::Core && entry._bOpenByDefault;
            if ( entry._pRegistration == nullptr )
            {
                entry._pInstance->setOpen( bOpenAfterReset );
                continue;
            }
            entry._pInstance->shutdown();
            entry._pInstance = entry._pRegistration->_pCreate();
            if ( entry._pInstance != nullptr )
                entry._pInstance->setOpen( bOpenAfterReset );
            ++resetCount;
        }
        return resetCount;
    }

    void EditorPanelManager::restoreCorePanelVisibility()
    {
        for ( const EditorPanelEntry& entry : _listPanel )
        {
            if ( entry._category == EditorPanelCategory::Core && entry._pInstance != nullptr )
                entry._pInstance->setOpen( entry._bOpenByDefault );
        }
    }

    void EditorPanelManager::drawOpenPanels()
    {
        const bool bReportTimed = gv_editorPanelTimes > 0 && _bPanelTimesReported == false;
        const bool bWatchTimed  = gv_editorWatchPanel.get().empty() == false;
        const bool bTimed       = bReportTimed || bWatchTimed;
        uint64     panelsDrawNanos{ 0 };
        for ( EditorPanelEntry& entry : _listPanel )
        {
            if ( entry._pInstance == nullptr || entry._pInstance->isOpen() == false )
                continue;
            if ( bTimed == false )
            {
                entry._pInstance->draw();
                continue;
            }
            entry._pInstance->setGatherTimed( true );
            const Stopwatch stopwatch;
            entry._pInstance->draw();
            const uint64 elapsedNanos = static_cast<uint64>( stopwatch.getElapsedNanoseconds() );
            entry._pInstance->setGatherTimed( false );
            entry._lastDrawNanos   = elapsedNanos;
            entry._lastGatherNanos = entry._pInstance->takeGatherNanoseconds();
            panelsDrawNanos += elapsedNanos;
            if ( bReportTimed )
            {
                entry._drawNanosSum += elapsedNanos;
                entry._gatherNanosSum += entry._lastGatherNanos;
                entry._drawNanosMax = MathUtil::max( entry._drawNanosMax, elapsedNanos );
            }
        }
        if ( bTimed )
            _lastPanelsDrawNanos = panelsDrawNanos;
        if ( bReportTimed && ++_panelTimeFrameCount >= static_cast<uint32>( gv_editorPanelTimes ) )
            reportPanelTimes();
        updateFocusedCommandStack();
        // 그린 뒤에 본다 — 이번 프레임의 편집 · 상호작용 구간이 끝난 상태로 사본을 모은다(감사 E-1)
        EditorDocumentAutosaveScheduler::update( _listPanel );
    }

    void EditorPanelManager::updateFocusedCommandStack()
    {
        // 패널 id 만 기억한다 — 닫힌 패널 · 사라진 스택은 읽는 순간의 목록에 없어 손으로 풀 것이 없다(`getFocusedCommandStack`).
        for ( const EditorPanelEntry& entry : _listPanel )
        {
            EditorPanel* const pPanel = entry._pInstance.get();
            if ( pPanel == nullptr || pPanel->isOpen() == false || pPanel->isWindowFocused() == false )
                continue;
            if ( pPanel->shouldKeepFocusedCommandStack() == false )
                setFocusedPanel( entry._id );
            return;
        }
    }

    void EditorPanelManager::reportPanelTimes()
    {
        _bPanelTimesReported = SW_TRUE;
        vector<const EditorPanelEntry*> listEntry;
        for ( const EditorPanelEntry& entry : _listPanel )
        {
            if ( entry._drawNanosSum > 0 )
                listEntry.push_back( &entry );
        }
        VectorUtil::sort( listEntry, []( const EditorPanelEntry* pLeft, const EditorPanelEntry* pRight )
        { return pLeft->_drawNanosSum > pRight->_drawNanosSum; } );
        const uint64 frameCount = MathUtil::max<uint64>( 1u, _panelTimeFrameCount );
        for ( const EditorPanelEntry* pEntry : listEntry )
        {
            SW_LOG_INFO_ID( "EditorPanelManager.EditorPanelTimeAvgUsMax", "EditorPanelTime|%#|avg %# us|max %# us|gather avg %# us", pEntry->_id.c_str(), pEntry->_drawNanosSum / static_cast<uint64>( constant::time::kNanosecondsPerMicrosecond ) / frameCount,
                            pEntry->_drawNanosMax / static_cast<uint64>( constant::time::kNanosecondsPerMicrosecond ), pEntry->_gatherNanosSum / static_cast<uint64>( constant::time::kNanosecondsPerMicrosecond ) / frameCount );
        }
    }

    bool EditorPanelManager::findWatchedPanelTimes( uint64& outDrawNanos, uint64& outGatherNanos, uint64& outSkippedGatherCount ) const
    {
        const string_view watchedID = gv_editorWatchPanel.get();
        for ( const EditorPanelEntry& entry : _listPanel )
        {
            if ( entry._id != watchedID || entry._pInstance == nullptr || entry._pInstance->isOpen() == false )
                continue;
            outDrawNanos          = entry._lastDrawNanos;
            outGatherNanos        = entry._lastGatherNanos;
            outSkippedGatherCount = entry._pInstance->getSkippedGatherCount();
            return true;
        }
        return false;
    }

    void EditorPanelManager::shutdownAllPanels()
    {
        for ( const EditorPanelEntry& entry : _listPanel )
        {
            if ( entry._pInstance != nullptr )
                entry._pInstance->shutdown();
        }
    }

    bool EditorPanelManager::saveFocusedDirtyDocument()
    {
        for ( const EditorPanelEntry& entry : _listPanel )
        {
            if ( entry._pInstance == nullptr || entry._pInstance->isOpen() == false )
                continue;
            EditorDocumentPanelBase* const pDocument = entry._pInstance->asDocumentPanel();
            if ( pDocument == nullptr || entry._pInstance->isWindowFocused() == false )
                continue;
            if ( pDocument->trySaveDirtyDocument() )
                return true;
        }
        return false;
    }

    bool EditorPanelManager::saveAllDirtyDocuments()
    {
        bool bAllOk{ true };
        for ( const EditorPanelEntry& entry : _listPanel )
        {
            EditorDocumentPanelBase* const pDocument = entry._pInstance != nullptr ? entry._pInstance->asDocumentPanel() : nullptr;
            if ( pDocument == nullptr || pDocument->isDocumentDirty() == false )
                continue;
            if ( pDocument->trySaveDirtyDocument() == false )
                bAllOk = false;
        }
        return bAllOk;
    }

    uint32 EditorPanelManager::countDirtyDocuments() const
    {
        uint32 count{ 0 };
        for ( const EditorPanelEntry& entry : _listPanel )
        {
            if ( entry._pInstance != nullptr && entry._pInstance->hasUnsavedDocument() )
                ++count;
        }
        return count;
    }

    void EditorPanelManager::discardAllDirtyDocuments()
    {
        for ( const EditorPanelEntry& entry : _listPanel )
        {
            EditorDocumentPanelBase* const pDocument = entry._pInstance != nullptr ? entry._pInstance->asDocumentPanel() : nullptr;
            if ( pDocument != nullptr )
                pDocument->discardDirtyDocument();
        }
    }

    // 이 종류의 등록 목록 — EditorModule 이미지에 하나(확장 DLL 의 등록자도 내보낸 이 함수로 오른다).
    SW_DEFINE_EDITOR_EXTENSION_KIND( EditorPanelRegistration )

    namespace
    {
        struct EditorPanelTimeWatchInternal
        {
            /** @brief 감시 값이 읽는 칸입니다 — `-gv_editorWatchPanel` 패널의 마지막 그리기 · 모으기 시간과 건너뛴 모으기 수. */
            enum class PanelTimeField : uint8
            {
                DrawMicroseconds,
                GatherMicroseconds,
                SkippedGatherCount,
            };

            /** @brief 감시 대상 패널(`-gv_editorWatchPanel`)의 칸 @p kField 를 읽습니다. 에디터 · 패널이 없거나 재지 않았으면 값이 없다. */
            template <PanelTimeField kField>
            [[nodiscard]] static bool readWatchedPanel( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorPanelManager* const pPanels = EditorPanelManager::findInEditorContext();
                uint64                          drawNanos{ 0 };
                uint64                          gatherNanos{ 0 };
                uint64                          skippedCount{ 0 };
                if ( pPanels == nullptr || pPanels->findWatchedPanelTimes( drawNanos, gatherNanos, skippedCount ) == false )
                    return false;
                const uint64 nanosPerMicro = static_cast<uint64>( constant::time::kNanosecondsPerMicrosecond );
                if constexpr ( kField == PanelTimeField::DrawMicroseconds )
                    outValue = static_cast<float64>( drawNanos / nanosPerMicro );
                else if constexpr ( kField == PanelTimeField::GatherMicroseconds )
                    outValue = static_cast<float64>( gatherNanos / nanosPerMicro );
                else
                    outValue = static_cast<float64>( skippedCount );
                return true;
            }

            [[nodiscard]] static bool readPanelCreate( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorPanelManager* const pPanels = EditorPanelManager::findInEditorContext();
                if ( pPanels == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanels->getLastPanelCreateNanoseconds() ) / static_cast<float64>( constant::time::kNanosecondsPerMillisecond );
                return true;
            }

            [[nodiscard]] static bool readPanelsDraw( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorPanelManager* const pPanels = EditorPanelManager::findInEditorContext();
                if ( pPanels == nullptr || gv_editorWatchPanel.get().empty() )
                    return false;
                outValue = static_cast<float64>( pPanels->getLastPanelsDrawNanoseconds() / static_cast<uint64>( constant::time::kNanosecondsPerMicrosecond ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( editorPanelDrawUs, "Editor.PanelDrawUs", "Microseconds the panel named by gv_editorWatchPanel took to draw last frame (gather included)",
                         &EditorPanelTimeWatchInternal::readWatchedPanel<EditorPanelTimeWatchInternal::PanelTimeField::DrawMicroseconds> );
    SW_AUTOMATION_WATCH( editorPanelGatherUs, "Editor.PanelGatherUs", "Microseconds the panel named by gv_editorWatchPanel spent in gather last frame (0 when it does not gather through gatherViewState)",
                         &EditorPanelTimeWatchInternal::readWatchedPanel<EditorPanelTimeWatchInternal::PanelTimeField::GatherMicroseconds> );
    SW_AUTOMATION_WATCH( editorGatherSkipCount, "Editor.GatherSkipCount", "Gathers the panel named by gv_editorWatchPanel skipped because its view model generation did not change",
                         &EditorPanelTimeWatchInternal::readWatchedPanel<EditorPanelTimeWatchInternal::PanelTimeField::SkippedGatherCount> );
    SW_AUTOMATION_WATCH( editorPanelCreateMs, "Editor.PanelCreateMs", "Milliseconds the editor spent creating every registered panel (closed ones included) the last time it synced the panel list",
                         &EditorPanelTimeWatchInternal::readPanelCreate );
    SW_AUTOMATION_WATCH( editorPanelsDrawUs, "Editor.PanelsDrawUs", "Microseconds every open editor panel took to draw last frame (measured while gv_editorWatchPanel is set)",
                         &EditorPanelTimeWatchInternal::readPanelsDraw );
} // namespace sw::editor

namespace sw::editor
{
    namespace
    {
        struct EditorDocumentAutosaveSchedulerInternal
        {
            /** @brief 주기 시계와 쓰기 잡입니다. 에디터 상태 저장소가 소유한다(UI 스레드만). */
            struct State
            {
                Stopwatch                      _sinceCopy{};
                EditorDocumentAutosaveWriteJob _writeJob{};
                vector<const EditorPanel*>     _listDeferred{};      ///< 지난 판에 미룬 패널 — 비어 있지 않으면 이 패널만 다시 묻는다(다른 문서의 글을 끄는 동안 프레임마다 만들지 않게). 지금 목록에 있는 것만 쓴다(비교만 한다)
                uint32                         _writtenCount{ 0 };   ///< 워커가 쓴 사본 수(끝난 잡의 결과를 더한다)
                bool                           _bForceNext{ false }; ///< 다음 판은 주기를 보지 않는다(`autosave.documentsNow`).
            };

            /** @brief 설정의 초를 시계의 밀리초로 바꾸는 배수입니다. */
            static constexpr float64 kMillisecondsPerSecond = 1000.0;

            static State& getState() { return getEditorState<State>(); }

            [[nodiscard]] static bool readCopyCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                if ( editor::findSubsystem<EditorStateStore>() == nullptr )
                    return false;
                outValue = static_cast<float64>( EditorDocumentAutosaveScheduler::getWrittenCopyCount() );
                return true;
            }

            static bool runNow( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                getState()._bForceNext = true;
                outReply               = "the next frame writes the autosave copies of the dirty documents (ignores the interval)";
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( editorAutosaveDocumentCopyCount, "Editor.AutosaveDocumentCopyCount", "Document autosave copies the writer finished this session",
                         &EditorDocumentAutosaveSchedulerInternal::readCopyCount );
    SW_DEV_COMMAND( AutosaveDocumentsNow, "autosave.documentsNow", "autosave.documentsNow",
                    "Write autosave copies of the dirty document panels on the next frame (ignores the interval)", &EditorDocumentAutosaveSchedulerInternal::runNow );

    void EditorDocumentAutosaveScheduler::update( const vector<EditorPanelEntry>& listPanel )
    {
        using Internal             = EditorDocumentAutosaveSchedulerInternal;
        Internal::State&     state = Internal::getState();
        uint32               writtenCount{ 0 };
        const EditorTaskPoll poll = state._writeJob.take( writtenCount ); // 주기와 무관하게 끝난 결과는 프레임마다 받는다
        if ( poll == EditorTaskPoll::Ready )
            state._writtenCount += writtenCount;
        const float64 intervalMilli = static_cast<float64>( getEditorToolDefaults()._documentAutosaveSeconds ) * Internal::kMillisecondsPerSecond;
        if ( state._bForceNext == false && static_cast<float64>( state._sinceCopy.getElapsedMilliseconds() ) < intervalMilli )
            return;
        if ( poll == EditorTaskPoll::Running )
            return; // 앞 묶음이 아직 쓰는 중이다 — 다음 프레임에 다시 본다
        const AbsolutePath sessionFolder = EditorAutosaveCommands::getSessionFolder();
        if ( sessionFolder.empty() )
            return; // 씬 자동 저장이 세션을 아직 열지 않았거나 열지 못했다(이유는 거기서 남겼다)

        EditorDocumentAutosaveBatch batch;
        batch._sessionFolder                       = sessionFolder;
        const vector<const EditorPanel*> listRetry = std::move( state._listDeferred );
        state._listDeferred.clear();
        for ( const EditorPanelEntry& panelEntry : listPanel )
        {
            EditorPanel* const pPanel = panelEntry._pInstance.get();
            if ( pPanel == nullptr )
                continue;
            EditorDocumentPanelBase* const pDocument = pPanel->asDocumentPanel();
            const bool                     bAsked    = listRetry.empty() || std::find( listRetry.begin(), listRetry.end(), pPanel ) != listRetry.end();
            if ( pDocument == nullptr || bAsked == false )
                continue;
            const size_t                 firstIndex = batch._listEntry.size();
            const DocumentAutosaveResult take       = pDocument->takeAutosaveCopies( batch._listEntry );
            if ( take == DocumentAutosaveResult::Deferred )
                state._listDeferred.push_back( pPanel );
            for ( size_t index = firstIndex; index < batch._listEntry.size(); ++index )
            {
                EditorDocumentAutosaveEntry& entry = batch._listEntry[index];
                if ( pDocument->getDocumentAssetPath().empty() )
                    entry._panelID = panelEntry._id; // 연 에셋이 없는 상태 문서다 — 복원이 이 패널을 연다
                entry._projectRoot       = ProcessRoots::get()._project.str();
                entry._sourceContentHash = EditorAutosaveCommands::computeDocumentSourceHash( entry );
            }
        }
        // 끄는 중인 문서가 있으면 주기를 다시 재지 않는다 — 구간이 끝난 첫 프레임에 그 문서가 나온다(감사 E-1 "끌기 중이면 한 프레임 미루기")
        if ( state._listDeferred.empty() )
        {
            state._sinceCopy.restart();
            state._bForceNext = false;
        }
        if ( batch._listEntry.empty() )
            return;
        state._writeJob.request( std::move( batch ) );
    }

    uint32 EditorDocumentAutosaveScheduler::getWrittenCopyCount()
    {
        return EditorDocumentAutosaveSchedulerInternal::getState()._writtenCount;
    }
} // namespace sw::editor
