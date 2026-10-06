#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/Workspace/AssetHotReload.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Panels/ContentBrowserPanel.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/SelfTest/EditorSelfTest.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Resource/ResourceUtil.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /**
         * @brief 패널 점검(2026-10-07)에서 찾은 결함의 재현 시험입니다 — 콘텐츠 브라우저 · 도구 창 · 오버레이.
         * @details 시험이 디스크에 만드는 것(시험 폴더)은 끝나는 단계에서 지운다. 실패로 일찍 끝나도 같은 정리 단계를 지난다.
         */
        struct EditorSelfTestPanelCasesInternal
        {
            static constexpr const utf8* kContentBrowserPanelId    = "content_browser";
            static constexpr const utf8* kContentBrowserPanelTitle = "Content Browser";
            static constexpr const utf8* kProbeFolderName          = "__editorselftest"; ///< 활성 게임 팩 아래 시험 폴더 — 끝나면 지운다
            static constexpr uint32      kMaxWaitStepCount         = 180;                ///< 폴더 목록(워커) · 파일 감시를 기다리는 최대 프레임

            /** @brief 콘텐츠 브라우저 시험의 진행 상태입니다. */
            struct ContentBrowserProbe
            {
                string _folderAbs{};
                uint32 _phase{ 0 };
                uint32 _phaseStartStep{ 0 };
            };

            static ContentBrowserProbe& getContentBrowserProbe()
            {
                static ContentBrowserProbe s_probe;
                return s_probe;
            }

            /** @brief 콘텐츠 브라우저를 열고 그 탭이 보이게 합니다(도킹 탭이 가려져 있으면 그리기가 돌지 않는다). */
            static ContentBrowserPanel* findVisibleContentBrowser( EditorSelfTestContext& context )
            {
                EditorContext* pContext = EditorContext::get();
                if ( context.expect( pContext != nullptr, "no editor context" ) == false )
                    return nullptr;
                (void)pContext->getPanelManager().setPanelOpen( kContentBrowserPanelId, true );
                ContentBrowserPanel* pPanel = static_cast<ContentBrowserPanel*>( pContext->getPanelManager().findPanel( kContentBrowserPanelId ) );
                if ( context.expect( pPanel != nullptr, "no content browser panel" ) == false )
                    return nullptr;
                ImGui::SetWindowFocus( kContentBrowserPanelTitle );
                return pPanel;
            }

            /** @brief 시험 폴더를 비운 채 새로 만들고 그 안에 파일 하나를 씁니다. */
            static bool makeProbeFolder( ContentBrowserProbe& probe, const utf8* pFileName )
            {
                probe._folderAbs = FileUtil::joinPath( ResourceUtil::getDomainFolderPath( GameConfig::getActive()._packRoot ), kProbeFolderName );
                if ( FileUtil::removeDirectory( probe._folderAbs ) == false ) // 앞 실행이 남긴 것
                    return false;
                if ( FileUtil::ensureDirectoryExists( probe._folderAbs ) == false )
                    return false;
                return FileUtil::writeTextFile( FileUtil::joinPath( probe._folderAbs, pFileName ), "editor self test" );
            }

            /** @brief 시험 폴더를 지우고 상태를 비웁니다. */
            static void removeProbeFolder( EditorSelfTestContext& context, ContentBrowserProbe& probe )
            {
                if ( probe._folderAbs.empty() == false )
                    (void)context.expect( FileUtil::removeDirectory( probe._folderAbs ), "could not remove the self test folder" );
                probe = ContentBrowserProbe{};
            }

            /** @brief 다음 단계로 넘어갑니다(기다림 한도는 새 단계의 시작부터 센다). */
            static void enterPhase( ContentBrowserProbe& probe, uint32 phase, uint32 stepIndex )
            {
                probe._phase          = phase;
                probe._phaseStartStep = stepIndex;
            }

            /** @brief 지금 단계가 기다림 한도를 넘었으면 true 입니다. */
            static bool hasPhaseTimedOut( const ContentBrowserProbe& probe, uint32 stepIndex ) { return stepIndex - probe._phaseStartStep > kMaxWaitStepCount; }

            // ------------------------------------------------------------------------------
            // contentBrowser.deleteRefreshesTheList — 삭제는 확인을 거쳐 지우고 목록에서 바로 빠진다. 에디터 밖에서 더한 파일은 파일 감시로 목록에 들어온다(D13)
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runDeleteRefreshesTheList( EditorSelfTestContext& context )
            {
                constexpr uint32      kPhaseWaitListing = 1;
                constexpr uint32      kPhaseWaitDeleted = 2;
                constexpr uint32      kPhaseWaitAdded   = 3;
                constexpr uint32      kPhaseCleanup     = 4;
                constexpr const utf8* kDeletedFileName  = "probe.txt";
                constexpr const utf8* kAddedFileName    = "added.txt";

                ContentBrowserProbe& probe     = getContentBrowserProbe();
                const uint32         stepIndex = context.getStepIndex();
                ContentBrowserPanel* pPanel    = findVisibleContentBrowser( context );
                if ( pPanel == nullptr )
                    enterPhase( probe, kPhaseCleanup, stepIndex );

                if ( stepIndex == 0 && pPanel != nullptr )
                {
                    if ( context.expect( makeProbeFolder( probe, kDeletedFileName ), "could not write the self test folder" ) == false )
                    {
                        removeProbeFolder( context, probe );
                        return EditorSelfTestStep::Done;
                    }
                    pPanel->openFolder( probe._folderAbs );
                    enterPhase( probe, kPhaseWaitListing, stepIndex );
                    return EditorSelfTestStep::Continue;
                }

                switch ( probe._phase )
                {
                    case kPhaseWaitListing:
                    {
                        if ( pPanel->getEntryCount() != 1 || pPanel->isFolderRefreshPending() )
                        {
                            if ( hasPhaseTimedOut( probe, stepIndex ) )
                            {
                                (void)context.expect( false, "the self test folder listing never arrived (is the content browser drawn?)" );
                                enterPhase( probe, kPhaseCleanup, stepIndex );
                            }
                            return EditorSelfTestStep::Continue;
                        }
                        // 우클릭 Delete 와 같은 길: 확인 모달을 열고 확인한다.
                        pPanel->requestDeleteAsset( FileUtil::joinPath( probe._folderAbs, kDeletedFileName ) );
                        (void)context.expect( pPanel->confirmDeleteAsset(), "confirming the delete did not delete the asset" );
                        // 파일 감시도 같은 변경을 알리지만 프레임이 지난 뒤다 — 지운 그 자리에서 다시 읽기로 해야 한다.
                        (void)context.expect( pPanel->isFolderRefreshPending(), "deleting an asset did not refresh the folder listing" );
                        enterPhase( probe, kPhaseWaitDeleted, stepIndex );
                        return EditorSelfTestStep::Continue;
                    }
                    case kPhaseWaitDeleted:
                    {
                        if ( pPanel->getEntryCount() != 0 || pPanel->isFolderRefreshPending() )
                        {
                            if ( hasPhaseTimedOut( probe, stepIndex ) )
                            {
                                (void)context.expect( false, "the deleted asset stayed in the list" );
                                enterPhase( probe, kPhaseCleanup, stepIndex );
                            }
                            return EditorSelfTestStep::Continue;
                        }
                        // 에디터 밖(탐색기 · git)의 변경 — 패널을 건드리지 않고 파일만 쓴다.
                        (void)context.expect( FileUtil::writeTextFile( FileUtil::joinPath( probe._folderAbs, kAddedFileName ), "editor self test" ),
                                              "could not write the added file" );
                        enterPhase( probe, kPhaseWaitAdded, stepIndex );
                        return EditorSelfTestStep::Continue;
                    }
                    case kPhaseWaitAdded:
                    {
                        if ( pPanel->getEntryCount() != 1 )
                        {
                            if ( hasPhaseTimedOut( probe, stepIndex ) )
                            {
                                (void)context.expect( false, "a file added outside the editor did not show up (file watch)" );
                                enterPhase( probe, kPhaseCleanup, stepIndex );
                            }
                            return EditorSelfTestStep::Continue;
                        }
                        enterPhase( probe, kPhaseCleanup, stepIndex );
                        return EditorSelfTestStep::Continue;
                    }
                    default:
                    {
                        break;
                    }
                }
                removeProbeFolder( context, probe );
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // contentBrowser.browsingWritesNoMeta — 폴더를 보기만 해서는 `.meta` 가 생기지 않는다(D14). GUID 는 그 에셋을 쓰는 시스템이 만든다
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runBrowsingWritesNoMeta( EditorSelfTestContext& context )
            {
                constexpr uint32      kPhaseWaitListing = 1;
                constexpr const utf8* kFileName         = "probe.txt";

                ContentBrowserProbe& probe     = getContentBrowserProbe();
                const uint32         stepIndex = context.getStepIndex();
                ContentBrowserPanel* pPanel    = findVisibleContentBrowser( context );
                if ( pPanel == nullptr )
                {
                    removeProbeFolder( context, probe );
                    return EditorSelfTestStep::Done;
                }

                if ( stepIndex == 0 )
                {
                    if ( context.expect( makeProbeFolder( probe, kFileName ), "could not write the self test folder" ) == false )
                    {
                        removeProbeFolder( context, probe );
                        return EditorSelfTestStep::Done;
                    }
                    pPanel->openFolder( probe._folderAbs );
                    enterPhase( probe, kPhaseWaitListing, stepIndex );
                    return EditorSelfTestStep::Continue;
                }

                const bool bListed = pPanel->getEntryCount() == 1 && pPanel->isFolderRefreshPending() == false;
                if ( bListed == false && hasPhaseTimedOut( probe, stepIndex ) == false )
                    return EditorSelfTestStep::Continue;
                (void)context.expect( bListed, "the self test folder listing never arrived (is the content browser drawn?)" );
                (void)context.expect( FileUtil::exists( FileUtil::joinPath( probe._folderAbs, string{ kFileName } + ".meta" ) ) == false,
                                      "browsing a folder wrote a .meta file" );
                removeProbeFolder( context, probe );
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // contentBrowser.treeDoesNotReadTheDiskEveryFrame — 폴더 트리는 처음 그릴 때만 하위 폴더를 디스크에서 읽는다(D15, Debug 16.8 ms 의 원인)
            // ------------------------------------------------------------------------------
            /** @brief 폴더 트리 시험의 진행 상태입니다. 앞 시험이 지운 폴더의 파일 감시 사건이 캐시를 비울 수 있어 변경 번호가 멎을 때까지 기다린다. */
            struct FolderTreeProbe
            {
                uint64 _contentSerial{ 0 };
                uint64 _scanCountAtStart{ 0 };
                uint64 _scanCountAfterWarmUp{ 0 };
                uint32 _phase{ 0 };
                uint32 _phaseStartStep{ 0 };
            };

            static FolderTreeProbe& getFolderTreeProbe()
            {
                static FolderTreeProbe s_probe;
                return s_probe;
            }

            static EditorSelfTestStep runTreeDoesNotReadTheDiskEveryFrame( EditorSelfTestContext& context )
            {
                constexpr uint32 kPhaseSettle       = 0; ///< `Resource/` 변경 번호가 멎기를 기다린다
                constexpr uint32 kPhaseWarmUp       = 1; ///< 캐시를 비우고 트리가 한 번 그려지기를 기다린다
                constexpr uint32 kPhaseSteady       = 2; ///< 그 뒤 프레임들은 디스크를 읽지 않아야 한다
                constexpr uint32 kSettleStepCount   = 10;
                constexpr uint32 kWarmUpStepCount   = 3;
                constexpr uint32 kSteadyStepCount   = 10;
                constexpr uint32 kMaxTotalStepCount = 600;

                FolderTreeProbe&     probe     = getFolderTreeProbe();
                const uint32         stepIndex = context.getStepIndex();
                ContentBrowserPanel* pPanel    = findVisibleContentBrowser( context );
                EditorContext*       pContext  = EditorContext::get();
                if ( pPanel == nullptr || pContext == nullptr )
                    return EditorSelfTestStep::Done;
                if ( stepIndex == 0 )
                    probe = FolderTreeProbe{};
                if ( stepIndex > kMaxTotalStepCount )
                {
                    (void)context.expect( false, "the resource folder kept changing - could not measure the folder tree" );
                    probe = FolderTreeProbe{};
                    return EditorSelfTestStep::Done;
                }

                // 변경 번호가 바뀌면 패널이 캐시를 비우므로 처음부터 다시 잰다.
                const uint64 contentSerial = pContext->getAssetHotReload().getContentChangeSerial();
                if ( stepIndex == 0 || contentSerial != probe._contentSerial )
                {
                    probe._contentSerial  = contentSerial;
                    probe._phase          = kPhaseSettle;
                    probe._phaseStartStep = stepIndex;
                    return EditorSelfTestStep::Continue;
                }

                const uint32 phaseStepCount = stepIndex - probe._phaseStartStep;
                switch ( probe._phase )
                {
                    case kPhaseSettle:
                    {
                        if ( phaseStepCount < kSettleStepCount )
                            return EditorSelfTestStep::Continue;
                        pPanel->clearFolderTreeCache();
                        probe._scanCountAtStart = EditorAssetCommands::getChildFolderScanCount();
                        probe._phase            = kPhaseWarmUp;
                        probe._phaseStartStep   = stepIndex;
                        return EditorSelfTestStep::Continue;
                    }
                    case kPhaseWarmUp:
                    {
                        if ( phaseStepCount < kWarmUpStepCount )
                            return EditorSelfTestStep::Continue;
                        probe._scanCountAfterWarmUp = EditorAssetCommands::getChildFolderScanCount();
                        if ( context.expect( probe._scanCountAfterWarmUp > probe._scanCountAtStart, "the content browser folder tree was not drawn" ) == false )
                        {
                            probe = FolderTreeProbe{};
                            return EditorSelfTestStep::Done;
                        }
                        probe._phase          = kPhaseSteady;
                        probe._phaseStartStep = stepIndex;
                        return EditorSelfTestStep::Continue;
                    }
                    default:
                    {
                        if ( phaseStepCount < kSteadyStepCount )
                            return EditorSelfTestStep::Continue;
                        (void)context.expect( EditorAssetCommands::getChildFolderScanCount() == probe._scanCountAfterWarmUp,
                                              "the folder tree read the disk again on later frames" );
                        probe = FolderTreeProbe{};
                        return EditorSelfTestStep::Done;
                    }
                }
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_SELF_TEST( ContentBrowserDelete, "contentBrowser.deleteRefreshesTheList", 1100, &EditorSelfTestPanelCasesInternal::runDeleteRefreshesTheList );
    SW_EDITOR_SELF_TEST( ContentBrowserNoMeta, "contentBrowser.browsingWritesNoMeta", 1110, &EditorSelfTestPanelCasesInternal::runBrowsingWritesNoMeta );
    SW_EDITOR_SELF_TEST( ContentBrowserTree, "contentBrowser.treeDoesNotReadTheDiskEveryFrame", 1120, &EditorSelfTestPanelCasesInternal::runTreeDoesNotReadTheDiskEveryFrame );
} // namespace sw::editor
