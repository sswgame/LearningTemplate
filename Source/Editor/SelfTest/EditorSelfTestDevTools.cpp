/**
 * @file EditorSelfTestDevTools.cpp
 * @brief 개발 편의 기능(디버그 드로우 · 기즈모 · 디버그 오버레이 · 자동 플레이)이 씬 뷰 · 게임 뷰 · 상단 툴바에 실제로 그려지는지 보는 에디터 자체 시험입니다.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/TagID.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/EditorSceneCommands.h"
#include "Editor/Common/GUI/EditorDockLayout.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPlayToolbar.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorLayoutStore.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/ConsolePanel.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Panels/GameViewPanel.h"
#include "Editor/Panels/Inspector/IInspectorProperty.h"
#include "Editor/Panels/Inspector/InspectorPropertyManager.h"
#include "Editor/Panels/SceneViewPanel.h"
#include "Editor/SelfTest/EditorSelfTest.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorCamera.h"
#include "Editor/Viewport/EditorGridUtil.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorVisualizerGeometry.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Console/DevConsole.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Utility/DebugOverlayState.h"
#include "Engine/Utility/GameAutoplay.h"
#include "Engine/Utility/GameTimeScale.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

namespace sw::editor
{
    SW_LOG_CALLER( "SelfTestDevTools" );

    namespace
    {
        struct EditorSelfTestDevToolsInternal
        {
            /** @brief 그리기 확장 등록 줄의 본보기 — 시험 전용 타입 이름에 걸린다(그 타입의 프로퍼티는 없다). */
            class SelfTestPropertyDrawer final : public IInspectorProperty
            {
            public:
                bool draw( void* /*pInstance*/, const PropertyInfo& /*prop*/ ) override { return true; }
            };

            /** @brief 그리기 확장 등록 줄이 프로퍼티 매니저에 걸리고 내장 위젯은 그대로다. */
            static EditorSelfTestStep runPropertyDrawerRegistered( EditorSelfTestContext& context )
            {
                EditorContext* pContext = EditorContext::get();
                if ( context.expect( pContext != nullptr, "no editor context" ) == false )
                    return EditorSelfTestStep::Done;
                pContext->getInspectorPropertyManager().syncWithRegistry();
                context.expect( pContext->getInspectorPropertyManager().find( "SelfTestDrawerProbe" ) != nullptr, "the registered property drawer is not in the manager" );
                context.expect( pContext->getInspectorPropertyManager().find( "float32" ) != nullptr, "the built-in float32 widget is gone" );
                return EditorSelfTestStep::Done;
            }

            /** @brief 등록 줄 커맨드(`selftest.ping`)의 동작 — 로그 한 줄을 남긴다(시나리오 `commandpalette` 가 본다). */
            static void runPingCommand() { SW_LOG_INFO( "Self-test registration command ran" ); }

            static constexpr const utf8* kSceneViewPanelID = "scene_view";
            static constexpr const utf8* kGameViewPanelID  = "game_view";
            static constexpr uint32      kMaxWaitFrame     = 60;
            static constexpr const utf8* kOverlayKey       = "selftest.overlay";
            static constexpr const utf8* kProbeTag         = "SelfTestDevTools";

            /**
             * @brief 패널을 열고 그 탭을 앞으로 가져와 그 패널을 돌려줍니다. 없으면 실패로 적고 nullptr 입니다.
             * @details 씬 뷰와 게임 뷰는 같은 영역의 탭이라, 앞에 없는 쪽은 그려지지 않는다. 앞으로 오는 것은 다음 프레임부터다.
             */
            static IEditorPanel* openAndFocus( EditorSelfTestContext& context, const utf8* pPanelID )
            {
                EditorContext* pContext = EditorContext::get();
                if ( context.expect( pContext != nullptr, "no editor context" ) == false )
                    return nullptr;
                (void)pContext->getPanelManager().setPanelOpen( pPanelID, true );
                IEditorPanel* pPanel = pContext->getPanelManager().findPanel( pPanelID );
                if ( context.expect( pPanel != nullptr, "the panel is not registered" ) == false )
                    return nullptr;
                if ( context.getStepIndex() == 0 )
                    ImGui::SetWindowFocus( pPanel->getPanelTitle() );
                return pPanel;
            }

            static SceneViewPanel* openSceneView( EditorSelfTestContext& context )
            {
                return static_cast<SceneViewPanel*>( openAndFocus( context, kSceneViewPanelID ) );
            }

            static GameViewPanel* openGameView( EditorSelfTestContext& context )
            {
                return static_cast<GameViewPanel*>( openAndFocus( context, kGameViewPanelID ) );
            }

            // ------------------------------------------------------------------------------
            // sceneView.debugDraw — DebugDrawQueue 의 선 · 상자 · 화살표 · 글자가 씬 뷰에 투영돼 그려진다
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runDebugDrawReachesTheSceneView( EditorSelfTestContext& context )
            {
                if ( openSceneView( context ) == nullptr )
                    return EditorSelfTestStep::Done;
                DebugDrawQueue* pQueue = getService<DebugDrawQueue>();
                if ( context.expect( pQueue != nullptr, "no DebugDrawQueue service" ) == false )
                    return EditorSelfTestStep::Done;

                const uint32 stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    // 씬 뷰 카메라(에디터 카메라) 앞 5 m 에 둔다. 넣은 것은 프레임 끝에 확정되고 다음 에디터 프레임에 그려진다.
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
            // gameView.debugOverlay — 게임이 DebugOverlayState 에 쓴 값을 게임 뷰가 게임 화면 위에 그린다
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
            // toolbar.autoplayButton — 게임이 자동 플레이를 등록하면 상단 툴바에 단추가 서고, 스위치가 그 게임의 값을 켠다
            // ------------------------------------------------------------------------------
            struct AutoplayProbe
            {
                int32                             _value{ 0 };
                GameAutoplayRegistration          _registration{ "SelfTestGame", "AI plays the self test", "gv_selfTestAutoPlay", nullptr };
                unique_ptr<GameAutoplayRegistrar> _pRegistrar;
            };

            static AutoplayProbe& getAutoplayProbe()
            {
                static AutoplayProbe s_probe;
                return s_probe;
            }

            static EditorSelfTestStep runAutoplayButton( EditorSelfTestContext& context )
            {
                AutoplayProbe& probe     = getAutoplayProbe();
                const uint32   stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    probe._registration._pValue = &probe._value;
                    probe._pRegistrar           = make_unique<GameAutoplayRegistrar>( &probe._registration );
                    return EditorSelfTestStep::Continue;
                }
                const bool bDrawn = EditorPlayToolbar::wasAutoplayButtonDrawn();
                if ( bDrawn == false && stepIndex < kMaxWaitFrame )
                    return EditorSelfTestStep::Continue;
                (void)context.expect( bDrawn, "the editor toolbar never drew the autoplay button for a registered game" );
                (void)context.expect( GameAutoplay::setOn( true ) && probe._value == 1, "the autoplay switch did not turn the game's variable on" );
                probe._pRegistrar.reset();
                probe._value = 0;
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
                constexpr const utf8* kPanelID    = "history";
                constexpr const utf8* kLayoutName = "selftest";
                EditorContext*        pContext    = EditorContext::get();
                EditorDockLayout*     pDock       = pContext != nullptr ? pContext->findDockLayout() : nullptr;
                if ( context.expect( pDock != nullptr, "no dock layout" ) == false )
                    return EditorSelfTestStep::Done;
                const string folder = FileUtil::joinPath( FileUtil::getTempDirectory(), "sw_editor_selftest_layouts" );

                const uint32 stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    (void)pContext->getPanelManager().setPanelOpen( kPanelID, true );
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
                    (void)pContext->getPanelManager().setPanelOpen( kPanelID, false );
                    (void)context.expect( pDock->requestLoadNamedLayout( kLayoutName, folder ), "could not load the named layout" );
                    return EditorSelfTestStep::Continue;
                }
                if ( stepIndex < 4 )
                    return EditorSelfTestStep::Continue; // 다음 프레임 시작에 적용되고, 그 프레임에 창이 다시 선다

                const IEditorPanel* pPanel = pContext->getPanelManager().findPanel( kPanelID );
                (void)context.expect( pPanel != nullptr && pPanel->isOpen(), "loading the layout did not reopen the panel it saved open" );
                const ImGuiWindow* pHierarchy = ImGui::FindWindowByName( "Hierarchy" );
                (void)context.expect( pHierarchy != nullptr && pHierarchy->DockId != 0, "the hierarchy is no longer docked after loading the layout" );
                (void)pContext->getPanelManager().setPanelOpen( kPanelID, false );
                (void)EditorLayoutStore::remove( folder, kLayoutName ); // 임시 폴더 — 남아도 다음 실행이 덮어쓴다
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // sceneView.gizmoMovesTheSelection — 고른 오브젝트의 이동 기즈모 가운데(화면 평면 이동)를 끌면 오브젝트가 움직인다
            // ------------------------------------------------------------------------------
            struct GizmoProbe
            {
                uint64 _objectID{ 0 };
                float2 _pressAt{};
                uint32 _viewportID{ 0 }; ///< 씬 뷰 캔버스의 뷰포트 — 움직일 때마다 함께 알린다(플랫폼이 실제 커서의 뷰포트를 넣는다)
                float3 _startPosition{};
                bool   _bWasOver{ false };
                bool   _bWasUsing{ false };
            };

            static GizmoProbe& getGizmoProbe()
            {
                static GizmoProbe s_probe;
                return s_probe;
            }

            static void finishGizmo( GameObjectManager* pManager )
            {
                GizmoProbe& probe = getGizmoProbe();
                EditorSelfTestInput::moveMouse( float2{ -10000.0f, -10000.0f } );
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr )
                    pContext->getWorkspace().clearSelection();
                GameObject* pObj = pManager != nullptr ? pManager->findGameObjectByID( probe._objectID ) : nullptr;
                if ( pObj != nullptr )
                    pManager->destroyObject( pObj );
                probe = GizmoProbe{};
            }

            static EditorSelfTestStep runGizmoMovesTheSelection( EditorSelfTestContext& context )
            {
                constexpr float32  kDragPixel     = 60.0f;
                constexpr uint32   kDragStepCount = 4;
                GameObjectManager* pManager       = editor::getActiveObjectManager();
                Scene*             pScene         = editor::getActiveScene();
                if ( openSceneView( context ) == nullptr || context.expect( pManager != nullptr && pScene != nullptr, "no active scene" ) == false )
                    return EditorSelfTestStep::Done;
                GizmoProbe&      probe     = getGizmoProbe();
                const uint32     stepIndex = context.getStepIndex();
                CameraComponent* pCamera   = EditorCamera::find( pScene );
                if ( context.expect( pCamera != nullptr, "no editor camera" ) == false )
                    return EditorSelfTestStep::Done;
                if ( stepIndex == 0 )
                {
                    // 카메라 앞 6 m 에 놓는다 — 만들면 골라진다.
                    GameObject* pObj = EditorSceneCommands::create( pManager, nullptr );
                    if ( context.expect( pObj != nullptr && pObj->getPrimarySceneComponent() != nullptr, "could not create the probe object" ) == false )
                        return EditorSelfTestStep::Done;
                    probe._objectID = pObj->getObjectID();
                    static_cast<SceneComponent*>( pObj->getPrimarySceneComponent() )
                        ->setLocalPosition( pCamera->getWorldPosition() + pCamera->getCameraForward() * 6.0f );
                    pManager->flushSceneTransforms();
                    probe._startPosition = static_cast<SceneComponent*>( pObj->getPrimarySceneComponent() )->getWorldPosition();
                    return EditorSelfTestStep::Continue;
                }
                GameObject* pObj = pManager->findGameObjectByID( probe._objectID );
                if ( context.expect( pObj != nullptr, "the probe object vanished" ) == false )
                {
                    finishGizmo( pManager );
                    return EditorSelfTestStep::Done;
                }
                if ( stepIndex < 3 )
                    return EditorSelfTestStep::Continue; // 선택 · 기즈모가 한 번 그려지게
                if ( stepIndex == 3 )
                {
                    // 오브젝트 원점을 화면 좌표로 — 기즈모 가운데 사각형(화면 평면 이동)이 거기 있다.
                    EditorSelfTestMark canvas{};
                    if ( context.expect( EditorSelfTestMarks::find( "sceneView.canvas", canvas ), "the scene view canvas left no mark" ) == false )
                    {
                        finishGizmo( pManager );
                        return EditorSelfTestStep::Done;
                    }
                    const float2   canvasPos{ canvas._min._x, canvas._min._y };
                    const float2   canvasSize{ canvas._max._x - canvas._min._x, canvas._max._y - canvas._min._y };
                    const float4x4 viewProj = pCamera->getViewProjectionMatrix( canvasSize._x / ( canvasSize._y > 0.0f ? canvasSize._y : 1.0f ) );
                    ImVec2         screen{};
                    if ( context.expect( EditorViewportProjectionUtil::projectPoint( viewProj, probe._startPosition, canvasPos, canvasSize, screen ),
                                         "the probe object is behind the camera" ) == false )
                    {
                        finishGizmo( pManager );
                        return EditorSelfTestStep::Done;
                    }
                    probe._pressAt    = float2{ screen.x, screen.y };
                    probe._viewportID = canvas._viewportID;
                    EditorSelfTestInput::moveMouse( probe._pressAt, probe._viewportID );
                    return EditorSelfTestStep::Continue;
                }
                if ( stepIndex == 4 )
                {
                    probe._bWasOver = ImGuizmo::IsOver();
                    EditorSelfTestInput::moveMouse( probe._pressAt, probe._viewportID );
                    EditorSelfTestInput::setMouseButton( ImGuiMouseButton_Left, true );
                    return EditorSelfTestStep::Continue;
                }
                probe._bWasUsing = probe._bWasUsing || ImGuizmo::IsUsing();
                if ( stepIndex < 5 + kDragStepCount )
                {
                    const float32 offset = kDragPixel * static_cast<float32>( stepIndex - 4 ) / static_cast<float32>( kDragStepCount );
                    EditorSelfTestInput::moveMouse( float2{ probe._pressAt._x + offset, probe._pressAt._y }, probe._viewportID );
                    return EditorSelfTestStep::Continue;
                }
                if ( stepIndex == 5 + kDragStepCount )
                {
                    EditorSelfTestInput::moveMouse( float2{ probe._pressAt._x + kDragPixel, probe._pressAt._y }, probe._viewportID );
                    EditorSelfTestInput::setMouseButton( ImGuiMouseButton_Left, false );
                    return EditorSelfTestStep::Continue;
                }
                pManager->flushSceneTransforms();
                const float3 moved = static_cast<SceneComponent*>( pObj->getPrimarySceneComponent() )->getWorldPosition() - probe._startPosition;
                string       what{ "the gizmo did not move the selection (gizmo hovered before the press: " };
                what += probe._bWasOver ? "yes" : "no";
                what += probe._bWasUsing ? ", dragging: yes)" : ", dragging: no)";
                (void)context.expect( moved.getLength() > 0.01f, what.c_str() );
                finishGizmo( pManager );
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // sceneView.gridAndGizmoDraw — 씬 뷰가 앞에 있으면 격자와 고른 오브젝트의 기즈모를 그린다
            // ------------------------------------------------------------------------------
            /** @brief 카메라 앞 6 m 에 시험 오브젝트를 만들어 고릅니다(만들면 골라진다). 못 만들면 0 입니다. */
            static uint64 createSelectedProbe( GameObjectManager* pManager, const CameraComponent* pCamera )
            {
                GameObject* pObj = EditorSceneCommands::create( pManager, nullptr );
                if ( pObj == nullptr || pObj->getPrimarySceneComponent() == nullptr )
                    return 0;
                static_cast<SceneComponent*>( pObj->getPrimarySceneComponent() )->setLocalPosition( pCamera->getWorldPosition() + pCamera->getCameraForward() * 6.0f );
                pManager->flushSceneTransforms();
                return pObj->getObjectID();
            }

            /** @brief 시험 오브젝트를 지우고 선택을 비웁니다. */
            static void destroyProbe( GameObjectManager* pManager, uint64 objectID )
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr )
                    pContext->getWorkspace().clearSelection();
                GameObject* pObj = pManager != nullptr ? pManager->findGameObjectByID( objectID ) : nullptr;
                if ( pObj != nullptr )
                    pManager->destroyObject( pObj );
            }

            inline static uint64 _s_overlayProbeObjectID = 0;

            static EditorSelfTestStep runGridAndGizmoDraw( EditorSelfTestContext& context )
            {
                GameObjectManager*    pManager = editor::getActiveObjectManager();
                const SceneViewPanel* pPanel   = openSceneView( context );
                CameraComponent*      pCamera  = EditorCamera::find( editor::getActiveScene() );
                EditorContext*        pContext = EditorContext::get();
                if ( pPanel == nullptr || pContext == nullptr ||
                     context.expect( pManager != nullptr && pCamera != nullptr, "no active scene or editor camera" ) == false )
                    return EditorSelfTestStep::Done;
                const uint32 stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    _s_overlayProbeObjectID = createSelectedProbe( pManager, pCamera );
                    (void)context.expect( _s_overlayProbeObjectID != 0, "could not create the probe object" );
                    return EditorSelfTestStep::Continue;
                }
                const int32                 frame     = ImGui::GetFrameCount();
                const EditorGridStats&      grid      = EditorGridStats::get();
                const EditorViewportClient& client    = pPanel->getViewportClient();
                const bool                  bGrid     = grid._frame == frame;
                const bool                  bGizmo    = client.getLastGizmoFrame() == frame && client.getLastGizmoObjectCount() >= 1;
                const bool                  bSceneRan = pContext->wasViewDrawn( EditorViewKind::Scene );
                if ( ( bGrid == false || bGizmo == false || bSceneRan == false ) && stepIndex < kMaxWaitFrame )
                    return EditorSelfTestStep::Continue;
                (void)context.expect( bSceneRan, "the scene view was not drawn while its tab was in front" );
                (void)context.expect( bGrid, "the scene view did not draw the grid" );
                (void)context.expect( bGizmo, "the scene view did not draw the gizmo of the selection" );
                destroyProbe( pManager, _s_overlayProbeObjectID );
                _s_overlayProbeObjectID = 0;
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // gameView.hidesEditorOverlays — 게임 뷰는 게임 카메라 그림만 그린다: 이미지 뒤에 격자 · 기즈모 · 시각화 그리기가 없고, 그동안 씬 뷰는 그리지 않는다
            // ------------------------------------------------------------------------------
            /** @brief 이번 프레임 "Game" 창 그리기 목록에서 게임 뷰 이미지 뒤에 그린 정점 수를 셉니다. 이미지를 못 찾으면 false 입니다. */
            static bool countVerticesAfterGameImage( ImTextureID textureID, int32& outVertexCount )
            {
                const ImGuiWindow* pWindow = ImGui::FindWindowByName( "Game" );
                if ( pWindow == nullptr || pWindow->DrawList == nullptr || pWindow->LastFrameActive != ImGui::GetFrameCount() )
                    return false;
                const ImDrawList* pDrawList = pWindow->DrawList;
                int32             imageIndex{ -1 };
                for ( int32 commandIndex = 0; commandIndex < pDrawList->CmdBuffer.Size; ++commandIndex )
                {
                    if ( pDrawList->CmdBuffer[commandIndex].ElemCount > 0 && pDrawList->CmdBuffer[commandIndex].GetTexID() == textureID )
                        imageIndex = commandIndex;
                }
                if ( imageIndex < 0 )
                    return false;
                outVertexCount = 0;
                for ( int32 commandIndex = imageIndex + 1; commandIndex < pDrawList->CmdBuffer.Size; ++commandIndex )
                {
                    outVertexCount += static_cast<int32>( pDrawList->CmdBuffer[commandIndex].ElemCount );
                }
                return true;
            }

            static EditorSelfTestStep runGameViewHidesEditorOverlays( EditorSelfTestContext& context )
            {
                constexpr uint32     kSettleFrameCount = 3; // 탭이 앞으로 오고 → 고른 오브젝트가 한 번 그려지는 데 걸리는 프레임
                GameObjectManager*   pManager          = editor::getActiveObjectManager();
                const GameViewPanel* pPanel            = openGameView( context );
                CameraComponent*     pCamera           = EditorCamera::find( editor::getActiveScene() );
                EditorContext*       pContext          = EditorContext::get();
                if ( pPanel == nullptr || pContext == nullptr ||
                     context.expect( pManager != nullptr && pCamera != nullptr, "no active scene or editor camera" ) == false )
                    return EditorSelfTestStep::Done;
                const uint32 stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    // 선택이 있어야 "기즈모를 그리지 않는다" 가 뜻이 있다.
                    _s_overlayProbeObjectID = createSelectedProbe( pManager, pCamera );
                    (void)context.expect( _s_overlayProbeObjectID != 0, "could not create the probe object" );
                    return EditorSelfTestStep::Continue;
                }
                const EditorViewTarget& view        = pContext->getViewTarget( EditorViewKind::Game );
                int32                   vertexCount = 0;
                const bool              bImage      = view._pTextureID != nullptr && countVerticesAfterGameImage( reinterpret_cast<ImTextureID>( view._pTextureID ), vertexCount );
                if ( ( bImage == false || stepIndex < kSettleFrameCount ) && stepIndex < kMaxWaitFrame )
                    return EditorSelfTestStep::Continue;
                (void)context.expect( bImage, "the game view image was not drawn while its tab was in front" );
                (void)context.expect( pContext->wasViewDrawn( EditorViewKind::Game ), "the game view did not mark itself drawn" );
                (void)context.expect( pContext->wasViewDrawn( EditorViewKind::Scene ) == false, "the scene view was drawn behind the game view tab" );
                (void)context.expect( EditorGridStats::get()._frame != ImGui::GetFrameCount(), "the grid was drawn while only the game view was visible" );
                // HUD 줄 · 카메라 없음 안내가 없으면 이미지 뒤에 그린 것이 하나도 없어야 한다(격자 · 기즈모 · 시각화 · 통계 · 방향 큐브는 씬 뷰 몫).
                if ( pPanel->getLastOverlayRowCount() == 0 && pPanel->wasNoCameraHintShown() == false )
                {
                    string what{ "the game view drew editor overlays over the game image (" };
                    what += to_string( vertexCount );
                    what += " indices)";
                    (void)context.expect( vertexCount == 0, what.c_str() );
                }
                destroyProbe( pManager, _s_overlayProbeObjectID );
                _s_overlayProbeObjectID = 0;
                // 다음 시험이 기본 탭(씬 뷰)에서 시작하게 되돌린다.
                ImGui::SetWindowFocus( "Scene" );
                return EditorSelfTestStep::Done;
            }
        };
    } // namespace

    SW_EDITOR_SELF_TEST( SceneViewDebugDraw, "sceneView.debugDraw", 710, &EditorSelfTestDevToolsInternal::runDebugDrawReachesTheSceneView );
    SW_EDITOR_SELF_TEST( GameViewDebugOverlay, "gameView.debugOverlay", 720, &EditorSelfTestDevToolsInternal::runDebugOverlayIsDrawn );
    SW_EDITOR_SELF_TEST( ToolbarAutoplay, "toolbar.autoplayButton", 725, &EditorSelfTestDevToolsInternal::runAutoplayButton );
    SW_EDITOR_SELF_TEST( ConsoleTagFilter, "console.tagFilter", 730, &EditorSelfTestDevToolsInternal::runConsoleTagFilter );
    SW_EDITOR_SELF_TEST( ConsoleDevCommands, "console.devCommands", 735, &EditorSelfTestDevToolsInternal::runConsoleDevCommands );
    SW_EDITOR_SELF_TEST( HierarchySelectAllWith, "hierarchy.selectAllWith", 740, &EditorSelfTestDevToolsInternal::runSelectAllWithTag );
    SW_EDITOR_SELF_TEST( NamedLayout, "layout.namedRoundTrip", 750, &EditorSelfTestDevToolsInternal::runNamedLayoutRoundTrip );
    SW_EDITOR_PROPERTY_DRAWER( SelfTestDrawerProbe, "SelfTestDrawerProbe", EditorSelfTestDevToolsInternal::SelfTestPropertyDrawer );
    SW_EDITOR_SELF_TEST( PropertyDrawer, "inspector.propertyDrawerRegistered", 760, &EditorSelfTestDevToolsInternal::runPropertyDrawerRegistered );
    // 등록 줄 커맨드의 본보기 — 표가 아닌 파일에서 한 줄로 더한 커맨드가 팔레트에 나온다(메뉴 경로가 없으면 팔레트 · 단축키만).
    SW_EDITOR_COMMAND( SelfTestPing, "selftest.ping", 0, "Self Test Ping", editoricon::kBug, "SelfTest", "자체 시험과 시나리오가 쓰는 커맨드 — 로그 한 줄을 남깁니다",
                       "Write one log line (self-test)", {}, &EditorSelfTestDevToolsInternal::runPingCommand, nullptr, nullptr );
    SW_EDITOR_SELF_TEST( SceneViewGizmo, "sceneView.gizmoMovesTheSelection", 760, &EditorSelfTestDevToolsInternal::runGizmoMovesTheSelection );
    SW_EDITOR_SELF_TEST( SceneViewGridAndGizmo, "sceneView.gridAndGizmoDraw", 765, &EditorSelfTestDevToolsInternal::runGridAndGizmoDraw );
    SW_EDITOR_SELF_TEST( GameViewNoOverlays, "gameView.hidesEditorOverlays", 770, &EditorSelfTestDevToolsInternal::runGameViewHidesEditorOverlays );
} // namespace sw::editor
