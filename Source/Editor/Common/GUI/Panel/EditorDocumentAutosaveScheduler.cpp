// 문서 패널 자동 저장의 주기(사본 모으기 · 쓰기 잡 내기)입니다.
#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorDocumentAutosaveScheduler.h"

#include "Core/File/ProcessRoots.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Commands/Asset/EditorDocumentAutosave.h"
#include "Editor/Common/Commands/Scene/EditorAutosaveCommands.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/GUI/Panel/EditorDocumentPanelBase.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/ContentHash.h"

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
