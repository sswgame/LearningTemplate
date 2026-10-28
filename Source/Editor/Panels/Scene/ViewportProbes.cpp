/**
 * @file ViewportProbes.cpp
 * @brief 씬 뷰 · 게임 뷰 패널의 자동화 탐침입니다 — 카메라, 렌더 타깃 요청, 격자, 시각화, 오버레이 바, 선택 상자, 기즈모, 보기 모드.
 * @details 탐침 레지스트리(`Engine/Automation/Probe`)가 기능 층보다 아래라 기능이 자기 탐침을 등록합니다. 등록은 이 .cpp 의 정적 등록자가 하며 대상 id 를 정하는 시나리오용 전역 변수는
 *          `EditorProbeSupport.h` 에 있습니다.
 */
#include "pch.h"

#include "Core/String/hashed_string.h"

#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/Scene/SceneViewPanel.h"
#include "Editor/SelfTest/EditorProbeSupport.h"
#include "Editor/Viewport/EditorViewportBillboard.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorGridUtil.h"
#include "Editor/Viewport/Model/EditorViewportOverlayLayout.h"

#include "Engine/Automation/Probe/AutomationProbe.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Scene/Scene.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 씬 뷰 클라이언트를 반환합니다. 씬 뷰 패널이 없으면 nullptr 입니다. */
        const EditorViewportClient* findSceneViewClient()
        {
            EditorContext*        pContext = getProbeContext( __func__ );
            const SceneViewPanel* pPanel   = pContext != nullptr ? pContext->getSubsystem<EditorPanelManager>().findPanel<SceneViewPanel>() : nullptr;
            return pPanel != nullptr ? &pPanel->getViewportClient() : nullptr;
        }

        /** @brief 뷰포트 격자가 이번 프레임이나 지난 프레임에 그려졌는지 확인합니다. 그려지지 않았으면 격자 프로브는 값을 내지 않습니다. 격자를 끈 뷰나 다른 탭이 해당합니다. */
        [[nodiscard]] const EditorGridStats* findRecentGridStats()
        {
            const EditorViewportClient* pClient = findSceneViewClient();
            if ( pClient == nullptr )
                return nullptr;
            const EditorGridStats& stats = pClient->getGridStats();
            return stats._frame >= 0 && ImGui::GetFrameCount() - stats._frame <= 1 ? &stats : nullptr;
        }

        /** @brief 물리 셰이프 시각화가 지난 프레임에 그린 선분 수입니다. 씬 뷰 패널이 없으면 값을 내지 않습니다. */
        [[nodiscard]] bool readPhysicsShapeSegments( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const EditorViewportClient* pClient = findSceneViewClient();
            if ( pClient == nullptr )
                return false;
            outValue = static_cast<float64>( pClient->getVisualizerStats()._shape._physicsSegmentCount );
            return true;
        }

        /** @brief 내비메시 시각화가 지난 프레임에 그린 선분 수입니다. 씬 뷰 패널이 없으면 값을 내지 않습니다. */
        [[nodiscard]] bool readNavMeshSegments( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const EditorViewportClient* pClient = findSceneViewClient();
            if ( pClient == nullptr )
                return false;
            outValue = static_cast<float64>( pClient->getVisualizerStats()._shape._navMeshSegmentCount );
            return true;
        }

        [[nodiscard]] bool readGridStep( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const EditorGridStats* pStats = findRecentGridStats();
            if ( pStats == nullptr )
                return false;
            outValue = static_cast<float64>( pStats->_step );
            return true;
        }

        [[nodiscard]] bool readGridMajorLines( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const EditorGridStats* pStats = findRecentGridStats();
            if ( pStats == nullptr )
                return false;
            outValue = static_cast<float64>( pStats->_majorLineCount );
            return true;
        }

        [[nodiscard]] bool readGridMisplacedMajorLines( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const EditorGridStats* pStats = findRecentGridStats();
            if ( pStats == nullptr )
                return false;
            outValue = static_cast<float64>( pStats->_misplacedMajorCount );
            return true;
        }

        /** @brief 씬 뷰 카메라(에디터 카메라)의 월드 위치입니다. Play 중에도 에디터 카메라입니다. */
        [[nodiscard]] bool findSceneViewCameraPosition( float3& outPosition )
        {
            const CameraComponent* pCamera = EditorCamera::find( editor::getActiveScene() );
            if ( pCamera == nullptr )
                return false;
            outPosition = pCamera->getWorldPosition();
            return true;
        }

        /** @brief 게임 뷰가 그리는 카메라(활성 씬의 게임 카메라)의 월드 위치입니다. */
        [[nodiscard]] bool findGameViewCameraPosition( float3& outPosition )
        {
            const Scene*           pScene  = editor::getActiveScene();
            const CameraComponent* pCamera = pScene != nullptr ? pScene->getActiveGameCamera() : nullptr;
            if ( pCamera == nullptr )
                return false;
            outPosition = pCamera->getWorldPosition();
            return true;
        }

        /** @brief 씬 뷰 카메라가 오빗 중심에서 떨어진 거리(m)입니다. 휠 돌리가 줄인다. */
        [[nodiscard]] bool readSceneViewOrbitDistance( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const EditorViewportClient* pClient = findSceneViewClient();
            if ( pClient == nullptr )
                return false;
            outValue = pClient->getCameraOrbitDistance();
            return true;
        }

        [[nodiscard]] bool readSceneViewCameraX( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            float3 position{};
            if ( findSceneViewCameraPosition( position ) == false )
                return false;
            outValue = static_cast<float64>( position._x );
            return true;
        }

        [[nodiscard]] bool readSceneViewCameraY( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            float3 position{};
            if ( findSceneViewCameraPosition( position ) == false )
                return false;
            outValue = static_cast<float64>( position._y );
            return true;
        }

        [[nodiscard]] bool readGameViewCameraX( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            float3 position{};
            if ( findGameViewCameraPosition( position ) == false )
                return false;
            outValue = static_cast<float64>( position._x );
            return true;
        }

        [[nodiscard]] bool readGameViewCameraY( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            float3 position{};
            if ( findGameViewCameraPosition( position ) == false )
                return false;
            outValue = static_cast<float64>( position._y );
            return true;
        }

        /** @brief 이번 UI 프레임에 셸이 호스트에 그 뷰의 렌더 타깃을 알렸는지 확인합니다. `ImGuiEditor::getSceneViewport` 와 `getGameViewport` 가 쓰는 판정과 같습니다. */
        [[nodiscard]] bool readViewRequested( EditorViewKind kind, float64& outValue )
        {
            const EditorContext* pContext = getProbeContext( __func__ );
            if ( pContext == nullptr )
                return false;
            const bool bSceneDrawn = pContext->getSubsystem<EditorRenderViews>().wasViewDrawn( EditorViewKind::Scene );
            const bool bGameDrawn  = pContext->getSubsystem<EditorRenderViews>().wasViewDrawn( EditorViewKind::Game );
            const bool bRequested  = kind == EditorViewKind::Scene ? EditorViewTargetUtil::shouldRequestSceneView( bSceneDrawn, bGameDrawn )
                                                                   : EditorViewTargetUtil::shouldRequestGameView( bGameDrawn );
            outValue               = ( bRequested && pContext->getSubsystem<EditorRenderViews>().getViewTarget( kind )._renderTarget != 0 ) ? 1.0 : 0.0;
            return true;
        }

        [[nodiscard]] bool readSceneViewRequested( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readViewRequested( EditorViewKind::Scene, outValue );
        }

        [[nodiscard]] bool readGameViewRequested( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readViewRequested( EditorViewKind::Game, outValue );
        }

        /** @brief 이번 UI 프레임에 Scene 패널이 씬 뷰를 그렸는지 확인합니다. 앞 탭으로 보이는 경우입니다. 두 뷰가 모두 가려져도 `Editor.SceneViewRequested` 는 1 입니다. */
        [[nodiscard]] bool readSceneViewDrawn( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const EditorContext* pContext = getProbeContext( __func__ );
            if ( pContext == nullptr )
                return false;
            outValue = pContext->getSubsystem<EditorRenderViews>().wasViewDrawn( EditorViewKind::Scene ) ? 1.0 : 0.0;
            return true;
        }

        /** @brief 씬 뷰가 `-gv_editorProbeVisualizer` 의 시각화를 켜 두었으면 1 입니다. 패널이나 그 id 의 시각화가 없으면 값을 내지 않습니다. */
        [[nodiscard]] bool readVisualizerOn( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            EditorContext* pContext = getProbeContext( __func__ );
            if ( pContext == nullptr )
                return false;
            const SceneViewPanel*               pPanel        = pContext->getSubsystem<EditorPanelManager>().findPanel<SceneViewPanel>();
            const EditorVisualizerRegistration* pRegistration = EditorExtensionRegistry<EditorVisualizerRegistration>::find( gv_editorProbeVisualizer );
            if ( pPanel == nullptr || pRegistration == nullptr )
                return false;
            outValue = pPanel->getViewportClient().getToolbarSettings()._visualizerToggles.isOn( *pRegistration ) ? 1.0 : 0.0;
            return true;
        }

        /** @brief `-gv_editorProbeObject` 이름의 오브젝트에 씬 뷰가 지난 그리기에서 넘긴 빌보드 글리프의 코드 포인트입니다. 쓰레기 문자열이면 0 입니다. */
        [[nodiscard]] bool readBillboardIcon( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const GameObjectManager* pManager = editor::getActiveObjectManager();
            const GameObject*        pObject  = pManager != nullptr ? pManager->findGameObjectByName( hashed_string( gv_editorProbeObject.c_str() ) ) : nullptr;
            if ( pObject == nullptr )
                return false;
            outValue = static_cast<float64>( EditorViewportBillboard::findDrawnGlyph( *pObject ) );
            return true;
        }

        /** @brief 씬 뷰가 직교 보기면 1 입니다. */
        [[nodiscard]] bool readSceneViewOrthographic( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const EditorViewportClient* pClient = findSceneViewClient();
            if ( pClient == nullptr )
                return false;
            outValue = pClient->isOrthographicView() ? 1.0 : 0.0;
            return true;
        }

        /** @brief 씬 뷰를 최대화했으면 1 입니다. */
        [[nodiscard]] bool readSceneViewMaximized( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const EditorViewportClient* pClient = findSceneViewClient();
            if ( pClient == nullptr )
                return false;
            outValue = pClient->isMaximized() ? 1.0 : 0.0;
            return true;
        }

        /** @brief 씬 뷰 HUD 박스가 지난 프레임에 그린 섹션 수입니다. 박스를 껐으면 0 입니다. */
        [[nodiscard]] bool readSceneViewHUDSections( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const EditorViewportClient* pClient = findSceneViewClient();
            if ( pClient == nullptr )
                return false;
            outValue = pClient->getToolbarSettings()._bShowHUD ? static_cast<float64>( pClient->getHUDView().getDrawnSectionCount() ) : 0.0;
            return true;
        }

        /** @brief 씬 뷰의 호버 오브젝트를 구분합니다. `-gv_editorProbeObject` 오브젝트이면 1, 없으면 0, 다른 오브젝트이면 -1 입니다. */
        [[nodiscard]] bool readHoveredObject( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const uint64 hoveredID = EditorSelectionBounds::getHoveredObjectID();
            if ( hoveredID == 0 )
            {
                outValue = 0.0;
                return true;
            }
            const GameObject* pProbe = findProbedObject();
            outValue                 = pProbe != nullptr && pProbe->getObjectID() == hoveredID ? 1.0 : -1.0;
            return true;
        }

        /** @brief 선택 상자 시각화가 지난 프레임에 그린 상자 수입니다. */
        [[nodiscard]] bool readSelectionOutlineBoxes( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = static_cast<float64>( EditorSelectionBounds::getDrawnBoxCount() );
            return true;
        }

        /** @brief `-gv_editorProbeOverlay` 바의 상태를 읽습니다. 씬 뷰가 없거나 그 바가 없으면 false 입니다. */
        [[nodiscard]] bool findProbedOverlay( const EditorOverlayBarState*& pOutState, const EditorOverlayBarRuntime*& pOutRuntime )
        {
            const EditorViewportClient* pClient = findSceneViewClient();
            if ( pClient == nullptr )
                return false;
            pOutState   = pClient->getOverlays().getLayout().findBar( gv_editorProbeOverlay );
            pOutRuntime = pClient->getOverlays().findRuntime( gv_editorProbeOverlay );
            return pOutState != nullptr;
        }

        /** @brief 바가 붙은 쪽입니다. 0 은 떠 있음, 1 은 위, 2 는 아래, 3 은 왼쪽, 4 는 오른쪽입니다. */
        [[nodiscard]] bool readOverlayDock( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const EditorOverlayBarState*   pState   = nullptr;
            const EditorOverlayBarRuntime* pRuntime = nullptr;
            if ( findProbedOverlay( pState, pRuntime ) == false )
                return false;
            outValue = static_cast<float64>( pState->_dock );
            return true;
        }

        /** @brief 바의 왼쪽 위 X 입니다. 뷰포트 왼쪽 기준 픽셀이며 지난 프레임 값입니다. */
        [[nodiscard]] bool readOverlayX( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const EditorOverlayBarState*   pState   = nullptr;
            const EditorOverlayBarRuntime* pRuntime = nullptr;
            if ( findProbedOverlay( pState, pRuntime ) == false || pRuntime == nullptr )
                return false;
            outValue = static_cast<float64>( pRuntime->_lastPosition._x );
            return true;
        }

        /** @brief 바의 왼쪽 위 Y 입니다. 뷰포트 위쪽 기준 픽셀이며 지난 프레임 값입니다. */
        [[nodiscard]] bool readOverlayY( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const EditorOverlayBarState*   pState   = nullptr;
            const EditorOverlayBarRuntime* pRuntime = nullptr;
            if ( findProbedOverlay( pState, pRuntime ) == false || pRuntime == nullptr )
                return false;
            outValue = static_cast<float64>( pRuntime->_lastPosition._y );
            return true;
        }

        /** @brief 바가 항목을 세로로 쌓으면(왼쪽 · 오른쪽에 붙음) 1 입니다. 그린 크기가 아니라 배치 상태를 읽는다 — 항목이 적은 바는 세로로 쌓아도 폭이 높이보다 크다. */
        [[nodiscard]] bool readOverlayVertical( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const EditorOverlayBarState*   pState   = nullptr;
            const EditorOverlayBarRuntime* pRuntime = nullptr;
            if ( findProbedOverlay( pState, pRuntime ) == false || pRuntime == nullptr )
                return false;
            outValue = EditorViewportOverlayLayout::isVertical( *pState ) ? 1.0 : 0.0;
            return true;
        }

        /** @brief 바가 보이면 1, 숨겨져 있으면 0 이고, 접혀 있으면 10 을 더합니다. */
        [[nodiscard]] bool readOverlayFlags( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const EditorOverlayBarState*   pState   = nullptr;
            const EditorOverlayBarRuntime* pRuntime = nullptr;
            if ( findProbedOverlay( pState, pRuntime ) == false )
                return false;
            outValue = ( pState->_bVisible ? 1.0 : 0.0 ) + ( pState->_bCollapsed ? 10.0 : 0.0 );
            return true;
        }

        /** @brief 기즈모 조작 모드입니다. 0 은 이동, 1 은 회전, 2 는 크기입니다. */
        [[nodiscard]] bool readGizmoOperation( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            EditorContext* pContext = getProbeContext( __func__ );
            if ( pContext == nullptr )
                return false;
            outValue = static_cast<float64>( pContext->getSubsystem<EditorWorkspace>().getGizmoOperation() );
            return true;
        }

        /** @brief 씬 뷰(툴바)나 주 출력(게임 뷰)의 보기 모드 번호(`RenderViewMode`)를 읽습니다. 렌더러가 없으면 false 입니다. */
        [[nodiscard]] bool readViewMode( bool bSceneView, float64& outValue )
        {
            const FrameRenderer* pRenderer = editor::findSubsystem<FrameRenderer>();
            if ( pRenderer == nullptr ) // 필수 서비스: 없으면 버그
            {
                SW_LOG_ERROR( "[Scenario] view mode probe: FrameRenderer is missing" );
                return false;
            }
            outValue = static_cast<float64>( bSceneView ? pRenderer->getSceneViewMode() : pRenderer->getViewMode() );
            return true;
        }

        [[nodiscard]] bool readSceneViewMode( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readViewMode( true, outValue );
        }

        [[nodiscard]] bool readGameViewMode( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readViewMode( false, outValue );
        }
    } // namespace

    SW_AUTOMATION_PROBE( editorGridStep, "Editor.GridStep", "Fine line spacing of the viewport grid in meters (1, 10 or 100)",
                         &readGridStep );
    SW_AUTOMATION_PROBE( editorGridMajorLines, "Editor.GridMajorLines", "Major lines the viewport grid drew in the last frame",
                         &readGridMajorLines );
    SW_AUTOMATION_PROBE( editorGridMisplacedMajorLines, "Editor.GridMisplacedMajorLines",
                         "Major grid lines drawn off a world multiple of 5 x spacing (0 unless major lines slide with the camera)",
                         &readGridMisplacedMajorLines );
    SW_AUTOMATION_PROBE( editorPhysicsShapeSegments, "Editor.PhysicsShapeSegments", "Line segments the physics_shapes visualizer drew in the last frame",
                         &readPhysicsShapeSegments );
    SW_AUTOMATION_PROBE( editorNavMeshSegments, "Editor.NavMeshSegments", "Line segments the navmesh visualizer drew in the last frame",
                         &readNavMeshSegments );
    SW_AUTOMATION_PROBE( editorSceneViewCameraX, "Editor.SceneViewCameraX", "Scene view (editor) camera world X", &readSceneViewCameraX );
    SW_AUTOMATION_PROBE( editorSceneViewCameraY, "Editor.SceneViewCameraY", "Scene view (editor) camera world Y", &readSceneViewCameraY );
    SW_AUTOMATION_PROBE( editorSceneViewOrbitDistance, "Editor.SceneViewOrbitDistance",
                         "Distance in meters from the scene view camera to its orbit center (wheel dolly shrinks it)", &readSceneViewOrbitDistance );
    SW_AUTOMATION_PROBE( editorGameViewCameraX, "Editor.GameViewCameraX", "Game view camera (the active scene's game camera) world X",
                         &readGameViewCameraX );
    SW_AUTOMATION_PROBE( editorGameViewCameraY, "Editor.GameViewCameraY", "Game view camera (the active scene's game camera) world Y",
                         &readGameViewCameraY );
    SW_AUTOMATION_PROBE( editorSceneViewRequested, "Editor.SceneViewRequested", "1 when the editor asked the host to render the scene view this frame",
                         &readSceneViewRequested );
    SW_AUTOMATION_PROBE( editorGameViewRequested, "Editor.GameViewRequested", "1 when the editor asked the host to render the game view this frame (0 while its panel is hidden)",
                         &readGameViewRequested );
    SW_AUTOMATION_PROBE( editorSceneViewDrawn, "Editor.SceneViewDrawn", "1 when the Scene panel drew the scene view this UI frame (its tab is in front)",
                         &readSceneViewDrawn );
    SW_AUTOMATION_PROBE( editorVisualizerOn, "Editor.VisualizerOn", "1 when the scene view shows the visualizer named by gv_editorProbeVisualizer",
                         &readVisualizerOn );
    SW_AUTOMATION_PROBE( editorBillboardIcon, "Editor.BillboardIcon",
                         "Icon code point the scene view billboard of the object named by gv_editorProbeObject drew last frame (0 when not drawn or not one glyph)",
                         &readBillboardIcon );
    SW_AUTOMATION_PROBE( editorSceneViewOrthographic, "Editor.SceneViewOrthographic", "1 when the scene view uses an orthographic axis view",
                         &readSceneViewOrthographic );
    SW_AUTOMATION_PROBE( editorSceneViewMaximized, "Editor.SceneViewMaximized", "1 while the scene view is maximized (Shift+Space)",
                         &readSceneViewMaximized );
    SW_AUTOMATION_PROBE( editorSceneViewHUDSections, "Editor.SceneViewHUDSections", "Debug HUD sections the scene view HUD box drew (0 when the box is off)",
                         &readSceneViewHUDSections );
    SW_AUTOMATION_PROBE( editorHoveredObject, "Editor.HoveredObject",
                         "Scene view hover: 1 when it is the object named by gv_editorProbeObject, 0 when nothing, -1 when another object",
                         &readHoveredObject );
    SW_AUTOMATION_PROBE( editorSelectionOutlineBoxes, "Editor.SelectionOutlineBoxes", "Selection boxes the scene view drew in the last frame",
                         &readSelectionOutlineBoxes );
    SW_AUTOMATION_PROBE( editorOverlayDock, "Editor.OverlayDock", "Docked side of the scene view overlay bar gv_editorProbeOverlay: 0 free, 1 top, 2 bottom, 3 left, 4 right",
                         &readOverlayDock );
    SW_AUTOMATION_PROBE( editorOverlayX, "Editor.OverlayX", "Left of the overlay bar gv_editorProbeOverlay, pixels from the scene view's left", &readOverlayX );
    SW_AUTOMATION_PROBE( editorOverlayY, "Editor.OverlayY", "Top of the overlay bar gv_editorProbeOverlay, pixels from the scene view's top", &readOverlayY );
    SW_AUTOMATION_PROBE( editorOverlayVertical, "Editor.OverlayVertical", "1 when the overlay bar gv_editorProbeOverlay stacks its items vertically (docked left or right)",
                         &readOverlayVertical );
    SW_AUTOMATION_PROBE( editorOverlayFlags, "Editor.OverlayFlags", "Overlay bar gv_editorProbeOverlay: 1 when visible, plus 10 when collapsed",
                         &readOverlayFlags );
    SW_AUTOMATION_PROBE( editorGizmoOperation, "Editor.GizmoOperation", "Gizmo operation: 0 translate, 1 rotate, 2 scale", &readGizmoOperation );
    SW_AUTOMATION_PROBE( editorSceneViewMode, "Editor.SceneViewMode", "Scene view mode: 0 Lit, 1 Unlit, 2 Wireframe, 3 Normals, 4 Depth, 5 Overdraw, 6 LightingOnly, 7 Roughness, 8 Metallic, 9 Mipmaps, 10 LightComplexity, 11 ShadedWireframe, 12 LodLevel, 13 LodFadeCode, 14 MarkerSlots",
                         &readSceneViewMode );
    SW_AUTOMATION_PROBE( editorGameViewMode, "Editor.GameViewMode", "Main output (game view) view mode — stays 0 Lit while the scene view changes",
                         &readGameViewMode );
} // namespace sw::editor
