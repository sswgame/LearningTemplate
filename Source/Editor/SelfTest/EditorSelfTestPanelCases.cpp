#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Commands/Asset/EditorFolderListing.h"
#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Workspace/AssetHotReload.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/Animation/AnimationRewindPanel.h"
#include "Editor/Panels/ContentBrowser/ContentBrowserPanel.h"
#include "Editor/Panels/Data/DataTablePanel.h"
#include "Editor/Panels/InputMap/InputMapPanel.h"
#include "Editor/Panels/MapCheck/MapCheckPanel.h"
#include "Editor/Panels/RenderTarget/RenderTargetPanel.h"
#include "Editor/Panels/Scene/HistoryPanel.h"
#include "Editor/Panels/Settings/GlobalVariablesPanel.h"
#include "Editor/Panels/Settings/UserSettingsPanel.h"
#include "Editor/SelfTest/EditorSelfTest.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Resource/ResourcePaths.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        /**
         * @brief 콘텐츠 브라우저, 도구 창, 오버레이의 결함 재현 테스트입니다.
         * @details 테스트가 디스크에 만드는 테스트 폴더는 끝나는 단계에서 지운다. 실패로 일찍 끝나도 같은 정리 단계를 거친다.
         */
        struct EditorSelfTestPanelCasesInternal
        {
            static constexpr const utf8* kContentBrowserPanelID    = ContentBrowserPanel::kPanelID;
            static constexpr const utf8* kContentBrowserPanelTitle = "Content Browser";
            static constexpr const utf8* kProbeFolderName          = "__editorselftest"; ///< 활성 게임 팩 아래의 테스트 폴더. 끝나면 지운다
            static constexpr uint32      kMaxWaitStepCount         = 180;                ///< 폴더 목록(워커)과 파일 감시를 기다리는 최대 프레임 수

            /** @brief 콘텐츠 브라우저 테스트의 진행 상태입니다. */
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

            /** @brief 콘텐츠 브라우저를 열고 그 탭이 보이게 합니다. 도킹 탭이 가려져 있으면 그리기가 실행되지 않습니다. */
            static ContentBrowserPanel* findVisibleContentBrowser( EditorSelfTestContext& context )
            {
                EditorContext* pContext = EditorContext::get();
                if ( context.expect( pContext != nullptr, "no editor context" ) == false )
                    return nullptr;
                (void)pContext->getSubsystem<EditorPanelManager>().setPanelOpen( kContentBrowserPanelID, true );
                ContentBrowserPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<ContentBrowserPanel>();
                if ( context.expect( pPanel != nullptr, "no content browser panel" ) == false )
                    return nullptr;
                ImGui::SetWindowFocus( kContentBrowserPanelTitle );
                return pPanel;
            }

            /** @brief 테스트 폴더를 비운 채 새로 만들고 그 안에 파일 하나를 씁니다. */
            static bool makeProbeFolder( ContentBrowserProbe& probe, const utf8* pFileName )
            {
                probe._folderAbs = FileUtil::joinPath( ResourcePaths::findLooseDomainFolder( GameConfig::getActive()._packRoot.get() ).getDiskPath(), kProbeFolderName );
                if ( FileUtil::removeDirectory( probe._folderAbs ) == false ) // 앞 실행이 남긴 폴더다
                    return false;
                if ( FileUtil::ensureDirectoryExists( probe._folderAbs ) == false )
                    return false;
                return FileUtil::writeTextFile( FileUtil::joinPath( probe._folderAbs, pFileName ), "editor self test" );
            }

            /** @brief 테스트 폴더를 지우고 상태를 비웁니다. */
            static void removeProbeFolder( EditorSelfTestContext& context, ContentBrowserProbe& probe )
            {
                if ( probe._folderAbs.empty() == false )
                    (void)context.expect( FileUtil::removeDirectory( probe._folderAbs ), "could not remove the self test folder" );
                probe = ContentBrowserProbe{};
            }

            /** @brief 다음 단계로 넘어갑니다. 기다림 한도는 새 단계의 시작부터 셉니다. */
            static void enterPhase( ContentBrowserProbe& probe, uint32 phase, uint32 stepIndex )
            {
                probe._phase          = phase;
                probe._phaseStartStep = stepIndex;
            }

            /** @brief 지금 단계가 기다림 한도를 넘었으면 true 입니다. */
            static bool hasPhaseTimedOut( const ContentBrowserProbe& probe, uint32 stepIndex ) { return stepIndex - probe._phaseStartStep > kMaxWaitStepCount; }

            // ------------------------------------------------------------------------------
            // contentBrowser.deleteRefreshesTheList: 삭제는 확인을 거쳐 지우고 목록에서 바로 빠진다. 에디터 밖에서 추가한 파일은 파일 감시로 목록에 들어온다
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
                        // 우클릭 Delete 와 같은 경로로 확인 모달을 열고 확인한다.
                        pPanel->requestDeleteAsset( FileUtil::joinPath( probe._folderAbs, kDeletedFileName ) );
                        (void)context.expect( pPanel->confirmDeleteAsset(), "confirming the delete did not delete the asset" );
                        // 파일 감시도 같은 변경을 알리지만 프레임이 지난 뒤다. 지운 직후 그 자리에서 다시 읽어야 한다.
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
                        // 에디터 밖(탐색기, git)의 변경을 흉내 낸다. 패널을 건드리지 않고 파일만 쓴다.
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
            // contentBrowser.browsingWritesNoMeta: 폴더를 보기만 해서는 `.meta` 가 생기지 않는다. GUID 는 그 에셋을 쓰는 시스템이 만든다
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
            // contentBrowser.treeDoesNotReadTheDiskEveryFrame: 폴더 트리는 처음 그릴 때만 하위 폴더를 디스크에서 읽는다. 프레임마다 읽으면 Debug 에서 16.8 ms 가 걸린다
            // ------------------------------------------------------------------------------
            /** @brief 폴더 트리 테스트의 진행 상태입니다. 앞 테스트가 지운 폴더의 파일 감시 이벤트가 캐시를 비울 수 있으므로 변경 번호가 멈출 때까지 기다립니다. */
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
                constexpr uint32 kPhaseSettle       = 0; ///< `Resource/` 변경 번호가 멈추기를 기다린다
                constexpr uint32 kPhaseWarmUp       = 1; ///< 캐시를 비우고 트리가 한 번 그려지기를 기다린다
                constexpr uint32 kPhaseSteady       = 2; ///< 그 뒤의 프레임들은 디스크를 읽지 않아야 한다
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

                // 변경 번호가 바뀌면 패널이 캐시를 비우므로 처음부터 다시 측정한다.
                const uint64 contentSerial = pContext->getSubsystem<AssetHotReload>().getContentChangeSerial();
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
                        probe._scanCountAtStart = EditorFolderListing::getChildFolderScanCount();
                        probe._phase            = kPhaseWarmUp;
                        probe._phaseStartStep   = stepIndex;
                        return EditorSelfTestStep::Continue;
                    }
                    case kPhaseWarmUp:
                    {
                        if ( phaseStepCount < kWarmUpStepCount )
                            return EditorSelfTestStep::Continue;
                        probe._scanCountAfterWarmUp = EditorFolderListing::getChildFolderScanCount();
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
                        (void)context.expect( EditorFolderListing::getChildFolderScanCount() == probe._scanCountAfterWarmUp,
                                              "the folder tree read the disk again on later frames" );
                        probe = FolderTreeProbe{};
                        return EditorSelfTestStep::Done;
                    }
                }
            }

            // ------------------------------------------------------------------------------
            // contentBrowser.showsActivePackOnly: 게임 루트는 활성 팩(`GameConfig::_packRoot`) 하나이고, All packs 를 켜면 game/ 의 팩마다 하나다
            // ------------------------------------------------------------------------------
            static bool& wasShowingAllPacks()
            {
                static bool s_bWasShowingAllPacks = false;
                return s_bWasShowingAllPacks;
            }

            static EditorSelfTestStep runShowsActivePackOnly( EditorSelfTestContext& context )
            {
                constexpr uint32 kSettleStepCount = 2; ///< 루트는 다음 그리기에서 다시 만든다

                const uint32         stepIndex = context.getStepIndex();
                ContentBrowserPanel* pPanel    = findVisibleContentBrowser( context );
                if ( pPanel == nullptr )
                    return EditorSelfTestStep::Done;
                if ( stepIndex == 0 )
                {
                    wasShowingAllPacks() = pPanel->isShowingAllPacks();
                    pPanel->setShowAllPacks( false );
                    return EditorSelfTestStep::Continue;
                }
                if ( stepIndex < kSettleStepCount )
                    return EditorSelfTestStep::Continue;
                if ( stepIndex == kSettleStepCount )
                {
                    const string& packRoot = GameConfig::getActive()._packRoot.get();
                    (void)context.expect( pPanel->hasRoot( packRoot ), "the active game pack is not a content root" );
                    (void)context.expect( pPanel->getGameRootCount() == 1, "the content browser lists game packs other than the active one" );
                    pPanel->setShowAllPacks( true );
                    return EditorSelfTestStep::Continue;
                }
                if ( stepIndex < kSettleStepCount * 2 )
                    return EditorSelfTestStep::Continue;

                vector<string> listPackFolder;
                (void)FileUtil::collectFolders( ResourcePaths::findLooseDomainFolder( "game" ).getDiskPath(), listPackFolder, false ); // 없으면 아래 기대가 실패한다
                (void)context.expect( listPackFolder.empty() == false && pPanel->getGameRootCount() == static_cast<uint32>( listPackFolder.size() ),
                                      "All packs does not list every game pack" );
                pPanel->setShowAllPacks( wasShowingAllPacks() );
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // mapCheck.selectsIssueObject: 결과 행을 누르면 그 오브젝트가 선택된다. 검증 결과는 ValidationIssueLog::replaceIssues 로 넣는다
            // ------------------------------------------------------------------------------
            struct MapCheckProbe
            {
                uint64 _objectID{ 0 };
                uint64 _previousSelectionID{ 0 };
            };

            static MapCheckProbe& getMapCheckProbe()
            {
                static MapCheckProbe s_probe;
                return s_probe;
            }

            static EditorSelfTestStep runMapCheckSelectsIssueObject( EditorSelfTestContext& context )
            {
                constexpr uint32      kSettleStepCount = 2; ///< 패널이 열리고 줄을 다시 만드는 프레임
                constexpr const utf8* kObjectName      = "MapCheckSelfTestObject";

                EditorContext*     pContext  = EditorContext::get();
                GameObjectManager* pManager  = editor::getActiveObjectManager();
                MapCheckProbe&     probe     = getMapCheckProbe();
                const uint32       stepIndex = context.getStepIndex();
                if ( context.expect( pContext != nullptr && pManager != nullptr, "no editor context or active scene" ) == false )
                    return EditorSelfTestStep::Done;
                MapCheckPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<MapCheckPanel>();
                if ( context.expect( pPanel != nullptr, "no map check panel" ) == false )
                    return EditorSelfTestStep::Done;

                if ( stepIndex == 0 )
                {
                    GameObject* pObject = pManager->createGameObject( hashed_string( kObjectName ) );
                    if ( context.expect( pObject != nullptr, "could not create the self test object" ) == false )
                        return EditorSelfTestStep::Done;
                    probe._objectID = pObject->getObjectID();
                    ValidationIssue issue{};
                    issue._message     = "self test issue";
                    issue._sourceLabel = kObjectName;
                    issue._typeName    = hashed_string( "sw::PointLightComponent" );
                    issue._severity    = ValidationSeverity::Error;
                    ValidationIssueLog::get().replaceIssues( probe._objectID, { issue } );
                    editor::getMainSceneSelection().clearObjectSelection();
                    (void)pContext->getSubsystem<EditorPanelManager>().setPanelOpen( MapCheckPanel::kPanelID, true );
                    pContext->getSubsystem<EditorWorkspace>().requestOpenPanel<MapCheckPanel>();
                    return EditorSelfTestStep::Continue;
                }
                if ( stepIndex < kSettleStepCount )
                    return EditorSelfTestStep::Continue;

                // 오류가 먼저 나오고 이 테스트의 결과는 오류 하나다. 첫 오류 행들 중에서 이 오브젝트의 행을 누른다.
                bool bSelected = false;
                for ( uint32 rowIndex = 0; rowIndex < pPanel->getRowCount() && bSelected == false; ++rowIndex )
                {
                    if ( pPanel->selectRowObject( rowIndex ) == false )
                        continue;
                    const GameObject* pPrimary = editor::getMainSceneSelection().getPrimaryObject();
                    bSelected                  = pPrimary != nullptr && pPrimary->getObjectID() == probe._objectID;
                }
                (void)context.expect( bSelected, "clicking the issue row did not select its object" );

                ValidationIssueLog::get().removeSource( probe._objectID );
                editor::getMainSceneSelection().clearObjectSelection();
                GameObject* pObject = pManager->findGameObjectByID( probe._objectID );
                if ( pObject != nullptr )
                    pManager->destroyObject( pObject );
                (void)pContext->getSubsystem<EditorPanelManager>().setPanelOpen( MapCheckPanel::kPanelID, false );
                probe = MapCheckProbe{};
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // prefab.ignoresOtherFocusedAssets: 프리팹이 아닌 오브젝트의 오버라이드를 모을 때 포커스된 머티리얼을 프리팹으로 읽지 않는다.
            // 프리팹으로 읽으면 Prefab Editor 가 `Missing <Prefab> root` 와 `Prefab source could not be loaded` 두 [Error] 를 남긴다.
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runPrefabIgnoresOtherFocusedAssets( EditorSelfTestContext& context )
            {
                constexpr const utf8* kFocusedMaterialPath = "engine/materials/defaultmaterial.material";

                EditorContext*     pContext = EditorContext::get();
                GameObjectManager* pManager = editor::getActiveObjectManager();
                if ( context.expect( pContext != nullptr && pManager != nullptr, "no editor context or active scene" ) == false )
                    return EditorSelfTestStep::Done;
                GameObject* pObj = pManager->createGameObject( hashed_string( "EditorSelfTestNotAPrefab" ) );
                if ( context.expect( pObj != nullptr, "could not create the probe object" ) == false )
                    return EditorSelfTestStep::Done;

                EditorWorkspace& workspace  = pContext->getSubsystem<EditorWorkspace>();
                const string     savedFocus = workspace.getFocusedAssetPath();
                workspace.setFocusedAssetPath( kFocusedMaterialPath );

                string                     prefabPath;
                string                     instanceName;
                vector<PrefabOverrideItem> listOverride;
                vector<string>             listNestedPrefab;
                EditorToolAssetCommands::collectPrefabOverrides( pObj, {}, prefabPath, instanceName, listOverride, listNestedPrefab );
                (void)context.expect( prefabPath.empty(), "a focused material was taken as the prefab of a non-prefab object" );
                (void)context.expect( listOverride.empty(), "a non-prefab object has prefab overrides" );

                workspace.setFocusedAssetPath( savedFocus.c_str() );
                pManager->destroyObject( pObj );
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // globalVariables.groupsStack: 모듈별 그룹 테이블은 자기 행 수만큼만 차지한다. 그룹 테이블이 혼자 스크롤하면 패널 높이를 다 차지해서
            // 다음 모듈이 바깥 스크롤 아래로 밀려난다. 이 프레임에 그려진 변수 테이블(Pin 에서 Reset 까지 다섯 열)이 둘 이상이고, 어느 것도 자기 스크롤 창이 없어야 한다.
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runGlobalVariableGroupsStack( EditorSelfTestContext& context )
            {
                constexpr const utf8* kPanelID      = GlobalVariablesPanel::kPanelID;
                constexpr uint32      kMaxStepCount = 30;

                EditorContext* pContext = EditorContext::get();
                if ( context.expect( pContext != nullptr, "no editor context" ) == false )
                    return EditorSelfTestStep::Done;
                const uint32 stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    (void)pContext->getSubsystem<EditorPanelManager>().setPanelOpen( kPanelID, true );
                    return EditorSelfTestStep::Continue;
                }

                ImGuiContext& imguiContext = *ImGui::GetCurrentContext();
                uint32        tableCount{ 0 };
                uint32        scrollingTableCount{ 0 };
                for ( int32 tableIndex = 0; tableIndex < imguiContext.Tables.GetMapSize(); ++tableIndex )
                {
                    ImGuiTable* pTable            = imguiContext.Tables.TryGetMapData( tableIndex );
                    const bool  bNotMatchingTable = pTable == nullptr || pTable->LastFrameActive != imguiContext.FrameCount || pTable->ColumnsCount != 5;
                    if ( bNotMatchingTable )
                        continue;
                    const string_view firstName{ ImGui::TableGetColumnName( pTable, 0 ) };
                    const string_view lastName{ ImGui::TableGetColumnName( pTable, 4 ) };
                    if ( firstName != "Pin" || lastName != "Reset" )
                        continue;
                    ++tableCount;
                    if ( pTable->InnerWindow != pTable->OuterWindow )
                        ++scrollingTableCount;
                }
                if ( tableCount < 2 && stepIndex < kMaxStepCount )
                    return EditorSelfTestStep::Continue; // 패널이 처음 그려지기를 기다린다
                (void)context.expect( tableCount >= 2, "the global variables panel did not draw two module groups" );
                (void)context.expect( scrollingTableCount == 0, "a module group table scrolls on its own and takes the whole panel height" );
                (void)pContext->getSubsystem<EditorPanelManager>().setPanelOpen( kPanelID, false );
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // panels.toolWindowsOpenAtAUsableSize: 기본 도킹이 없는 도구 창은 닫힌 채 시작하고, 열면 쓸 수 있는 크기로 주 뷰포트 안에 뜬다.
            // 크기를 정하지 않은 도구 창은 내용 크기로 열려서 Data Table 은 높이가 100 px 가 되고, User Settings 는 값 입력란의 폭이 0 이 된다. Input Map Editor 도 시작할 때 닫혀 있어야 한다.
            // 자체 테스트는 레이아웃을 저장하거나 복원하지 않으므로 이 실행에서 처음 만드는 창에 처음 크기가 적용된다.
            // ------------------------------------------------------------------------------
            // UI Preview 는 `.ui.xml` 에셋을 여는 패널이라(EditorAssetType::UIDocument) 기본 배치가 다른 에셋 패널처럼 가운데 영역에 도킹한다 — 여기 넣지 않는다.
            static constexpr const utf8* kArrFloatingToolPanelID[] = { HistoryPanel::kPanelID, GlobalVariablesPanel::kPanelID, RenderTargetPanel::kPanelID,
                                                                       AnimationRewindPanel::kPanelID, DataTablePanel::kPanelID, InputMapPanel::kPanelID,
                                                                       UserSettingsPanel::kPanelID };

            /** @brief 도구 창 테스트의 진행 상태입니다. */
            struct ToolWindowProbe
            {
                uint32 _panelIndex{ 0 };
                uint32 _openStep{ 0 };
                bool   _bWasOpen{ false };
            };

            static ToolWindowProbe& getToolWindowProbe()
            {
                static ToolWindowProbe s_probe;
                return s_probe;
            }

            static EditorSelfTestStep runToolWindowsOpenAtAUsableSize( EditorSelfTestContext& context )
            {
                constexpr uint32  kPanelCount      = static_cast<uint32>( sizeof( kArrFloatingToolPanelID ) / sizeof( kArrFloatingToolPanelID[0] ) );
                constexpr uint32  kSettleStepCount = 3; ///< 연 뒤 창이 만들어지고 크기가 자리 잡기까지
                constexpr float32 kMinUsableWidth  = 400.0f;
                constexpr float32 kMinUsableHeight = 300.0f;
                constexpr float32 kViewportRatio   = 0.9f; ///< `EditorChrome::setNextPanelSize` 가 자르는 비율
                constexpr float32 kEdgeTolerancePx = 1.0f;

                ToolWindowProbe&     probe     = getToolWindowProbe();
                EditorContext*       pContext  = EditorContext::get();
                const ImGuiViewport* pViewport = ImGui::GetMainViewport();
                if ( context.expect( pContext != nullptr && pViewport != nullptr, "no editor context or main viewport" ) == false )
                    return EditorSelfTestStep::Done;
                EditorPanelManager& panelManager = pContext->getSubsystem<EditorPanelManager>();
                const uint32        stepIndex    = context.getStepIndex();

                if ( stepIndex == 0 )
                {
                    probe = ToolWindowProbe{};
                    // 처음 열림은 레지스트리가 만드는 새 인스턴스로 판단한다. 실행 중인 패널은 앞 테스트나 사용자가 열었을 수 있다.
                    for ( const utf8* pPanelID : kArrFloatingToolPanelID )
                    {
                        const EditorPanelRegistration* pRegistration = EditorExtensionRegistry<EditorPanelRegistration>::find( pPanelID );
                        if ( context.expect( pRegistration != nullptr, "a floating tool panel is not registered" ) == false )
                            continue;
                        const unique_ptr<IEditorPanel> pFresh = pRegistration->_pCreate();
                        string                         what{ "a floating tool panel starts open: " };
                        what += pPanelID;
                        (void)context.expect( pFresh != nullptr && pFresh->isOpen() == false, what.c_str() );
                    }
                }

                if ( probe._panelIndex >= kPanelCount )
                {
                    probe = ToolWindowProbe{};
                    return EditorSelfTestStep::Done;
                }

                const utf8*   pPanelID = kArrFloatingToolPanelID[probe._panelIndex];
                IEditorPanel* pPanel   = panelManager.findPanelByID( pPanelID );
                if ( pPanel == nullptr )
                {
                    ++probe._panelIndex;
                    return EditorSelfTestStep::Continue;
                }
                if ( probe._openStep == 0 )
                {
                    probe._bWasOpen = pPanel->isOpen();
                    probe._openStep = stepIndex + 1; // 0 은 "아직 열지 않음" 이다
                    (void)panelManager.setPanelOpen( pPanelID, true );
                    return EditorSelfTestStep::Continue;
                }
                if ( stepIndex + 1 - probe._openStep < kSettleStepCount )
                    return EditorSelfTestStep::Continue;

                const float32      dpiScale  = EditorThemeUtil::getDpiScale();
                const float32      minWidth  = MathUtil::min( kMinUsableWidth * dpiScale, pViewport->WorkSize.x * kViewportRatio );
                const float32      minHeight = MathUtil::min( kMinUsableHeight * dpiScale, pViewport->WorkSize.y * kViewportRatio );
                const ImGuiWindow* pWindow   = ImGui::FindWindowByName( pPanel->getPanelTitle() );
                string             what{ pPanelID };
                if ( context.expect( pWindow != nullptr, ( what + ": the tool window was not created" ).c_str() ) )
                {
                    const string sizeText = " (" + to_string( static_cast<int32>( pWindow->Size.x ) ) + "x" + to_string( static_cast<int32>( pWindow->Size.y ) ) + " < " +
                                            to_string( static_cast<int32>( minWidth ) ) + "x" + to_string( static_cast<int32>( minHeight ) ) +
                                            ( pWindow->DockIsActive ? ", docked)" : ")" );
                    (void)context.expect( pWindow->Size.x >= minWidth && pWindow->Size.y >= minHeight, ( what + ": the tool window opened too small" + sizeText ).c_str() );
                    const bool bInsideViewport = pViewport->Pos.x - kEdgeTolerancePx <= pWindow->Pos.x && pViewport->Pos.y - kEdgeTolerancePx <= pWindow->Pos.y &&
                                                 pWindow->Pos.x + pWindow->Size.x <= pViewport->Pos.x + pViewport->Size.x + kEdgeTolerancePx &&
                                                 pWindow->Pos.y + pWindow->Size.y <= pViewport->Pos.y + pViewport->Size.y + kEdgeTolerancePx;
                    (void)context.expect( bInsideViewport, ( what + ": the tool window does not fit in the main viewport" ).c_str() );
                }
                (void)panelManager.setPanelOpen( pPanelID, probe._bWasOpen );
                ++probe._panelIndex;
                probe._openStep = 0;
                return EditorSelfTestStep::Continue;
            }

            // ------------------------------------------------------------------------------
            // sceneView.overlaysStayInsideTheCanvas: 씬 뷰의 격자와 시각화 선은 캔버스(씬 뷰 이미지) 사각형으로 잘린다.
            // 창 그리기 목록에 자르지 않고 그리면 카메라 절두체 선이 탭과 툴바 위까지 뻗는다. 이미지 명령 뒤에 캔버스와 같은 ClipRect 의 명령이 있어야
            // 한다. 격자는 기본으로 켜져 있다.
            // ------------------------------------------------------------------------------
            /** @brief @p pDrawList 에서 텍스처가 @p textureID 인 명령의 순번과 그 정점이 덮는 사각형을 찾습니다. 없으면 false 입니다. */
            static bool findImageCommand( const ImDrawList* pDrawList, ImTextureID textureID, int32& outCommandIndex, ImVec4& outRect )
            {
                for ( int32 commandIndex = 0; commandIndex < pDrawList->CmdBuffer.Size; ++commandIndex )
                {
                    const ImDrawCmd& command = pDrawList->CmdBuffer[commandIndex];
                    if ( command.ElemCount == 0 || command.GetTexID() != textureID )
                        continue;
                    ImVec4 rect{ MathUtil::kMaxFloat, MathUtil::kMaxFloat, -MathUtil::kMaxFloat, -MathUtil::kMaxFloat };
                    for ( uint32 elemIndex = 0; elemIndex < command.ElemCount; ++elemIndex )
                    {
                        const ImDrawIdx  vertexIndex = pDrawList->IdxBuffer[static_cast<int32>( command.IdxOffset + elemIndex )];
                        const ImDrawVert vertex      = pDrawList->VtxBuffer[static_cast<int32>( command.VtxOffset + vertexIndex )];
                        rect.x                       = MathUtil::min( rect.x, vertex.pos.x );
                        rect.y                       = MathUtil::min( rect.y, vertex.pos.y );
                        rect.z                       = MathUtil::max( rect.z, vertex.pos.x );
                        rect.w                       = MathUtil::max( rect.w, vertex.pos.y );
                    }
                    outCommandIndex = commandIndex;
                    outRect         = rect;
                    return true;
                }
                return false;
            }

            static EditorSelfTestStep runOverlaysStayInsideTheCanvas( EditorSelfTestContext& context )
            {
                constexpr const utf8* kSceneViewTitle  = "Scene";
                constexpr uint32      kMaxStepCount    = 30;
                constexpr float32     kRectTolerancePx = 1.0f;

                EditorContext* pContext = EditorContext::get();
                if ( context.expect( pContext != nullptr, "no editor context" ) == false )
                    return EditorSelfTestStep::Done;
                const uint32 stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    ImGui::SetWindowFocus( kSceneViewTitle ); // 가운데 탭이 게임 뷰나 다른 도구로 가려져 있으면 씬 뷰가 그려지지 않는다
                    return EditorSelfTestStep::Continue;
                }

                const EditorViewTarget& view         = pContext->getSubsystem<EditorRenderViews>().getViewTarget( EditorViewKind::Scene );
                const ImTextureID       textureID    = reinterpret_cast<ImTextureID>( view._pTextureID );
                const ImGuiContext&     imguiContext = *ImGui::GetCurrentContext();
                int32                   imageCommandIndex{ -1 };
                ImVec4                  canvasRect{};
                const ImDrawList*       pCanvasDrawList{ nullptr };
                for ( const ImGuiWindow* pWindow : imguiContext.Windows )
                {
                    const bool bInSceneView = pWindow != nullptr && pWindow->RootWindow != nullptr && pWindow->RootWindow->Name != nullptr &&
                                              StringUtil::equals( pWindow->RootWindow->Name, kSceneViewTitle ) && pWindow->LastFrameActive == imguiContext.FrameCount;
                    const bool bSceneViewCanvasFound = bInSceneView && view._pTextureID != nullptr && findImageCommand( pWindow->DrawList, textureID, imageCommandIndex, canvasRect );
                    if ( bSceneViewCanvasFound )
                    {
                        pCanvasDrawList = pWindow->DrawList;
                        break;
                    }
                }
                if ( pCanvasDrawList == nullptr )
                {
                    if ( stepIndex < kMaxStepCount )
                        return EditorSelfTestStep::Continue;
                    (void)context.expect( false, "the scene view image was not drawn" );
                    return EditorSelfTestStep::Done;
                }

                // 이미지 다음 명령이 오버레이(격자부터)다. 캔버스와 같은 사각형으로 잘려 있어야 한다.
                bool bFoundCanvasClip{ false };
                for ( int32 commandIndex = imageCommandIndex + 1; commandIndex < pCanvasDrawList->CmdBuffer.Size; ++commandIndex )
                {
                    const ImVec4& clip  = pCanvasDrawList->CmdBuffer[commandIndex].ClipRect;
                    const bool    bSame = MathUtil::abs( clip.x - canvasRect.x ) <= kRectTolerancePx && MathUtil::abs( clip.y - canvasRect.y ) <= kRectTolerancePx &&
                                       MathUtil::abs( clip.z - canvasRect.z ) <= kRectTolerancePx && MathUtil::abs( clip.w - canvasRect.w ) <= kRectTolerancePx;
                    bFoundCanvasClip = bFoundCanvasClip || bSame;
                }
                (void)context.expect( bFoundCanvasClip, "the viewport overlays are not clipped to the scene view canvas" );
                return EditorSelfTestStep::Done;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_SELF_TEST( ContentBrowserDelete, "contentBrowser.deleteRefreshesTheList", 1100, &EditorSelfTestPanelCasesInternal::runDeleteRefreshesTheList );
    SW_EDITOR_SELF_TEST( ContentBrowserNoMeta, "contentBrowser.browsingWritesNoMeta", 1110, &EditorSelfTestPanelCasesInternal::runBrowsingWritesNoMeta );
    SW_EDITOR_SELF_TEST( ContentBrowserTree, "contentBrowser.treeDoesNotReadTheDiskEveryFrame", 1120, &EditorSelfTestPanelCasesInternal::runTreeDoesNotReadTheDiskEveryFrame );
    SW_EDITOR_SELF_TEST( ContentBrowserActivePack, "contentBrowser.showsActivePackOnly", 1130, &EditorSelfTestPanelCasesInternal::runShowsActivePackOnly );
    SW_EDITOR_SELF_TEST( MapCheckSelect, "mapCheck.selectsIssueObject", 1140, &EditorSelfTestPanelCasesInternal::runMapCheckSelectsIssueObject );
    SW_EDITOR_SELF_TEST( PrefabOtherFocus, "prefab.ignoresOtherFocusedAssets", 1200, &EditorSelfTestPanelCasesInternal::runPrefabIgnoresOtherFocusedAssets );
    SW_EDITOR_SELF_TEST( GlobalVariableGroups, "globalVariables.groupsStack", 1300, &EditorSelfTestPanelCasesInternal::runGlobalVariableGroupsStack );
    SW_EDITOR_SELF_TEST( ToolWindowSize, "panels.toolWindowsOpenAtAUsableSize", 1400, &EditorSelfTestPanelCasesInternal::runToolWindowsOpenAtAUsableSize );
    SW_EDITOR_SELF_TEST( SceneViewOverlayClip, "sceneView.overlaysStayInsideTheCanvas", 1500, &EditorSelfTestPanelCasesInternal::runOverlaysStayInsideTheCanvas );
} // namespace sw::editor
