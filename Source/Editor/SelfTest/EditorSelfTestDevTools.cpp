/**
 * @file EditorSelfTestDevTools.cpp
 * @brief 개발 편의 기능(디버그 드로우 · 디버그 오버레이)이 Game View 에 실제로 그려지는지 보는 에디터 자체 시험입니다.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/TagID.h"

#include "Editor/Common/Commands/EditorSceneCommands.h"
#include "Editor/Common/Gui/EditorDockLayout.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorLayoutStore.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/ConsolePanel.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Panels/GameViewPanel.h"
#include "Editor/SelfTest/EditorSelfTest.h"
#include "Editor/Viewport/EditorCamera.h"
#include "Editor/Viewport/EditorVisualizerGeometry.h"

#include "Engine/Graphics/Renderer/Debug/DebugDrawQueue.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Utility/Console/DevCommandRegistry.h"
#include "Engine/Utility/Console/DevConsole.h"
#include "Engine/Utility/Debug/DebugOverlayState.h"
#include "Engine/Utility/GameTimeScale.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    SW_LOG_CALLER( "SelfTestDevTools" );

    namespace
    {
        struct EditorSelfTestDevToolsInternal
        {
            static constexpr const utf8* kGameViewPanelId = "game_view";
            static constexpr uint32      kMaxWaitFrame    = 60;
            static constexpr const utf8* kOverlayKey      = "selftest.overlay";
            static constexpr const utf8* kProbeTag        = "SelfTestDevTools";

            /** @brief Game View 를 열고 그 패널을 돌려줍니다. 없으면 실패로 적고 nullptr 입니다. */
            static GameViewPanel* openGameView( EditorSelfTestContext& context )
            {
                EditorContext* pContext = EditorContext::get();
                if ( context.expect( pContext != nullptr, "no editor context" ) == false )
                    return nullptr;
                (void)pContext->getPanelManager().setPanelOpen( kGameViewPanelId, true );
                GameViewPanel* pPanel = static_cast<GameViewPanel*>( pContext->getPanelManager().findPanel( kGameViewPanelId ) );
                (void)context.expect( pPanel != nullptr, "no game view panel" );
                return pPanel;
            }

            // ------------------------------------------------------------------------------
            // gameView.debugDraw — DebugDrawQueue 의 선 · 상자 · 화살표 · 글자가 Game View 에 투영돼 그려진다
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runDebugDrawReachesTheGameView( EditorSelfTestContext& context )
            {
                if ( openGameView( context ) == nullptr )
                    return EditorSelfTestStep::Done;
                DebugDrawQueue* pQueue = getService<DebugDrawQueue>();
                if ( context.expect( pQueue != nullptr, "no DebugDrawQueue service" ) == false )
                    return EditorSelfTestStep::Done;

                const uint32 stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    // 그리는 카메라(멈춤에서는 에디터 카메라) 앞 5 m 에 둔다. 넣은 것은 프레임 끝에 확정되고 다음 에디터 프레임에 그려진다.
                    const CameraComponent* pCamera = EditorCamera::find( editor::getActiveScene() );
                    if ( context.expect( pCamera != nullptr, "no editor camera" ) == false )
                        return EditorSelfTestStep::Done;
                    const float4x4 world   = pCamera->getWorldMatrix();
                    const float3   forward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, world ).normalize();
                    const float3   center  = pCamera->getWorldPosition() + forward * 5.0f;
                    const float4   color{ 1.0f, 0.3f, 0.1f, 1.0f };
                    pQueue->drawBox( center, float3{ 0.5f, 0.5f, 0.5f }, color, 2.0f, "SelfTest" );
                    pQueue->drawArrow( center, center + float3{ 1.0f, 0.0f, 0.0f }, color, 2.0f, "SelfTest" );
                    pQueue->drawText( center, "self test", color, 2.0f, "SelfTest" );
                    return EditorSelfTestStep::Continue;
                }

                const EditorDebugDrawStats& stats  = EditorDebugDrawStats::get();
                const bool                  bDrawn = stats._segmentCount >= 12 && stats._textCount >= 1;
                if ( bDrawn == false && stepIndex < kMaxWaitFrame )
                    return EditorSelfTestStep::Continue;
                (void)context.expect( bDrawn, "the debug_draw visualizer never drew the queued box, arrow and text" );
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // gameView.debugOverlay — 게임이 DebugOverlayState 에 쓴 값을 Game View 가 캔버스에 그린다
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runDebugOverlayIsDrawn( EditorSelfTestContext& context )
            {
                GameViewPanel* pPanel = openGameView( context );
                if ( pPanel == nullptr )
                    return EditorSelfTestStep::Done;
                DebugOverlayState* pOverlay = getService<DebugOverlayState>();
                if ( context.expect( pOverlay != nullptr, "no DebugOverlayState service" ) == false )
                    return EditorSelfTestStep::Done;

                const uint32 stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    pOverlay->setFloat( hashed_string( kOverlayKey ), 42.0f );
                    return EditorSelfTestStep::Continue;
                }
                const bool bDrawn = pPanel->getLastOverlayRowCount() >= 1;
                if ( bDrawn == false && stepIndex < kMaxWaitFrame )
                    return EditorSelfTestStep::Continue;
                (void)context.expect( bDrawn, "the game view never drew the DebugOverlayState rows" );
                pOverlay->remove( hashed_string( kOverlayKey ) );
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // console.tagFilter — 숨긴 태그의 로그 줄은 Output Log 에 나오지 않고, 다시 켜면 나온다
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runConsoleTagFilter( EditorSelfTestContext& context )
            {
                constexpr const utf8* kProbeMessage = "console tag filter probe";
                EditorContext*        pContext      = EditorContext::get();
                if ( context.expect( pContext != nullptr, "no editor context" ) == false )
                    return EditorSelfTestStep::Done;
                (void)pContext->getPanelManager().setPanelOpen( "console", true );
                ConsolePanel* pConsole = static_cast<ConsolePanel*>( pContext->getPanelManager().findPanel( "console" ) );
                if ( context.expect( pConsole != nullptr, "no console panel" ) == false )
                    return EditorSelfTestStep::Done;

                const uint32 stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    pConsole->getTagFilter().setTagVisible( kProbeTag, false );
                    SW_LOG_INFO( "%#", kProbeMessage );
                    return EditorSelfTestStep::Continue;
                }
                // 로거는 비동기다 — 줄이 패널에 닿을 때까지 기다린다.
                if ( pConsole->isMessageInSnapshot( kProbeMessage ) == false && stepIndex < kMaxWaitFrame )
                    return EditorSelfTestStep::Continue;
                if ( pConsole->getTagFilter().isTagVisible( kProbeTag ) == false )
                {
                    (void)context.expect( pConsole->isMessageInSnapshot( kProbeMessage ), "the probe log line never reached the console" );
                    (void)context.expect( pConsole->isMessageVisible( kProbeMessage ) == false, "a hidden tag's line is still listed" );
                    pConsole->getTagFilter().setTagVisible( kProbeTag, true );
                    return EditorSelfTestStep::Continue;
                }
                (void)context.expect( pConsole->isMessageVisible( kProbeMessage ), "showing the tag again did not bring its line back" );
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // console.devCommands — Output Log 의 입력 줄 콘솔이 개발 명령을 돌리고, 에디터가 등록한 명령이 등록부에 있으며, 답이 로그에 보인다
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runConsoleDevCommands( EditorSelfTestContext& context )
            {
                constexpr const utf8* kEcho    = "> timescale 0.75 : time scale = 0.75";
                EditorContext*        pContext = EditorContext::get();
                if ( context.expect( pContext != nullptr, "no editor context" ) == false )
                    return EditorSelfTestStep::Done;
                (void)pContext->getPanelManager().setPanelOpen( "console", true );
                ConsolePanel* pConsole = static_cast<ConsolePanel*>( pContext->getPanelManager().findPanel( "console" ) );
                if ( context.expect( pConsole != nullptr, "no console panel" ) == false )
                    return EditorSelfTestStep::Done;

                const uint32 stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    (void)context.expect( DevCommandRegistry::get().findCommand( "select.tag" ) != nullptr, "the editor module did not register its dev commands" );
                    (void)context.expect( pConsole->getDevConsole().submit( "timescale 0.75" ) == DevConsoleResult::Ok, "timescale failed in the editor console" );
                    (void)context.expect( GameTimeScale::get() == 0.75f, "the console command did not change the time scale" );
                    GameTimeScale::set( 1.0f );
                    return EditorSelfTestStep::Continue;
                }
                const bool bShown = pConsole->isMessageInSnapshot( kEcho );
                if ( bShown == false && stepIndex < kMaxWaitFrame )
                    return EditorSelfTestStep::Continue;
                (void)context.expect( bShown, "the console reply never reached the Output Log" );
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // hierarchy.selectAllWith — 같은 태그를 단 오브젝트를 모두 고르면 에디터 선택이 그 수가 된다
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runSelectAllWithTag( EditorSelfTestContext& context )
            {
                EditorContext*     pContext = EditorContext::get();
                GameObjectManager* pManager = editor::getActiveObjectManager();
                if ( context.expect( pContext != nullptr && pManager != nullptr, "no editor context or active scene" ) == false )
                    return EditorSelfTestStep::Done;
                const TagID tag    = TagID::request( "SwSelfTest.SelectAll" );
                GameObject* pFirst = pManager->createGameObject( hashed_string( "SelfTestSelectA" ) );
                GameObject* pOther = pManager->createGameObject( hashed_string( "SelfTestSelectB" ) );
                if ( context.expect( pFirst != nullptr && pOther != nullptr, "could not create the probe objects" ) == false )
                    return EditorSelfTestStep::Done;
                pFirst->addTag( tag );
                pOther->addTag( tag );
                pManager->mergePendingAdds();

                vector<GameObject*> listMatch;
                EditorSceneCommands::collectObjectsWithTag( *pManager, tag, listMatch );
                (void)context.expect( EditorSceneCommands::selectObjects( listMatch ) == 2, "selectObjects did not take both tagged objects" );
                (void)context.expect( pContext->getEditorSelection().getSelectedObjectCount() == 2, "the editor selection does not hold both tagged objects" );

                pContext->getWorkspace().clearSelection();
                pManager->destroyObject( pFirst );
                pManager->destroyObject( pOther );
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // layout.namedRoundTrip — 이름 붙인 레이아웃을 저장했다 불러오면 패널 가시성이 돌아오고 코어 패널은 도킹된 채다
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runNamedLayoutRoundTrip( EditorSelfTestContext& context )
            {
                constexpr const utf8* kPanelId    = "history";
                constexpr const utf8* kLayoutName = "selftest";
                EditorContext*        pContext    = EditorContext::get();
                EditorDockLayout*     pDock       = pContext != nullptr ? pContext->findDockLayout() : nullptr;
                if ( context.expect( pDock != nullptr, "no dock layout" ) == false )
                    return EditorSelfTestStep::Done;
                const string folder = FileUtil::joinPath( FileUtil::getTempDirectory(), "sw_editor_selftest_layouts" );

                const uint32 stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    (void)pContext->getPanelManager().setPanelOpen( kPanelId, true );
                    return EditorSelfTestStep::Continue;
                }
                if ( stepIndex == 1 )
                {
                    if ( context.expect( pDock->saveNamedLayout( kLayoutName, folder ), "could not save the named layout" ) == false )
                        return EditorSelfTestStep::Done;
                    string iniText;
                    (void)FileUtil::readTextFile( EditorLayoutStore::makeImguiIniPath( folder, kLayoutName ), iniText ); // 없으면 아래 단언이 알린다
                    (void)context.expect( iniText.find( "[Docking]" ) != string::npos && iniText.find( "[Window][Hierarchy]" ) != string::npos,
                                          "the saved layout has no docking section or no hierarchy window" );
                    (void)pContext->getPanelManager().setPanelOpen( kPanelId, false );
                    (void)context.expect( pDock->requestLoadNamedLayout( kLayoutName, folder ), "could not load the named layout" );
                    return EditorSelfTestStep::Continue;
                }
                if ( stepIndex < 4 )
                    return EditorSelfTestStep::Continue; // 다음 프레임 시작에 적용되고, 그 프레임에 창이 다시 선다

                const IEditorPanel* pPanel = pContext->getPanelManager().findPanel( kPanelId );
                (void)context.expect( pPanel != nullptr && pPanel->isOpen(), "loading the layout did not reopen the panel it saved open" );
                const ImGuiWindow* pHierarchy = ImGui::FindWindowByName( "Hierarchy" );
                (void)context.expect( pHierarchy != nullptr && pHierarchy->DockId != 0, "the hierarchy is no longer docked after loading the layout" );
                (void)pContext->getPanelManager().setPanelOpen( kPanelId, false );
                (void)EditorLayoutStore::remove( folder, kLayoutName ); // 임시 폴더 — 남아도 다음 실행이 덮어쓴다
                return EditorSelfTestStep::Done;
            }
        };
    } // namespace

    SW_EDITOR_SELF_TEST( GameViewDebugDraw, "gameView.debugDraw", 710, &EditorSelfTestDevToolsInternal::runDebugDrawReachesTheGameView );
    SW_EDITOR_SELF_TEST( GameViewDebugOverlay, "gameView.debugOverlay", 720, &EditorSelfTestDevToolsInternal::runDebugOverlayIsDrawn );
    SW_EDITOR_SELF_TEST( ConsoleTagFilter, "console.tagFilter", 730, &EditorSelfTestDevToolsInternal::runConsoleTagFilter );
    SW_EDITOR_SELF_TEST( ConsoleDevCommands, "console.devCommands", 735, &EditorSelfTestDevToolsInternal::runConsoleDevCommands );
    SW_EDITOR_SELF_TEST( HierarchySelectAllWith, "hierarchy.selectAllWith", 740, &EditorSelfTestDevToolsInternal::runSelectAllWithTag );
    SW_EDITOR_SELF_TEST( NamedLayout, "layout.namedRoundTrip", 750, &EditorSelfTestDevToolsInternal::runNamedLayoutRoundTrip );
} // namespace sw::editor
