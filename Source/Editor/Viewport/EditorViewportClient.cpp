#include "pch.h"

#include "Editor/Viewport/EditorViewportClient.h"

#include "Core/Container/vector.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/IEditorPanelHost.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorColliderHandleTool.h"
#include "Editor/Viewport/EditorViewportAssetDrop.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorViewportMouseClaim.h"
#include "Editor/Viewport/Model/EditorViewportRayPick.h"
#include "Editor/Viewport/Model/EditorVisualizerGeometry.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/Physics/ScenePhysics.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /**
         * @brief 씬 뷰 이미지를 그린 카메라(에디터 카메라)입니다. 호스트가 씬 뷰를 그리는 카메라와 같습니다(`ImGuiEditor::getSceneViewCamera`).
         * @details 오버레이(격자, 시각화, 디버그 드로우, 피킹, 기즈모)는 이 카메라로 투영해야 이미지와 겹친다. Play 중에도 씬 뷰는 이 카메라를 쓴다.
         */
        CameraComponent* getSceneViewCamera()
        {
            return EditorCamera::ensure( editor::getActiveScene() );
        }

        struct EditorViewportClientInternal
        {
            /** @brief 오른쪽 단추를 누른 동안 켜는 비행 레이어입니다(`editor.input.xml`). */
            static constexpr string_view kFlyLayer = "Editor.Viewport.Fly";

            /** @brief 선택 밖 오브젝트 위에서 메뉴를 열면 그것만 고릅니다(R2). */
            static void selectOnlyObject( uint64 objectID )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                GameObject*        pObject  = pManager != nullptr ? pManager->findGameObjectByID( objectID ) : nullptr;
                if ( pObject != nullptr )
                    editor::getMainSceneSelection().selectObject( pObject, SelectionMode::Replace );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorViewportClient::EditorViewportClient()
        : _toolbarSettings{}
        , _canvasHeight{ 0.0f }
        , _camera{}
        , _picker{}
        , _gizmo{}
        , _canvasOverlay{}
        , _overlays{}
        , _hudView{}
        , _visualizerStats{}
        , _contextMenuObjectID{ 0 }
        , _contextMenuWorldPosition{}
        , _bContextMenuHasWorldPosition{ false }
        , _bMaximized{ SW_FALSE }
        , _reserved{ 0 }
    {
        // 어떤 시각화가 기본으로 켜지는지는 시각화 등록 항목이 정한다. 스냅, 카메라 속도, 표시 기본값은 환경설정(Editor/Viewport)에서 온다.
        const EditorViewportPreferences& preferences = getPreferences<EditorViewportPreferences>();
        _toolbarSettings._cameraSpeed                = preferences._cameraSpeed;
        _toolbarSettings._gridSnapValue              = preferences._gridSnapValue;
        _toolbarSettings._rotationSnapValue          = MathUtil::toDegree( preferences._rotationSnapValue ); // 도구 막대 · ImGuizmo 는 도, 설정은 라디안
        _toolbarSettings._scaleSnapValue             = preferences._scaleSnapValue;
        _toolbarSettings._bShowHUD                   = preferences._bShowHUD;
        _toolbarSettings._bShowGrid                  = preferences._bShowGrid;
        _toolbarSettings._bShowOrientationCube       = preferences._bShowOrientationCube;
    }

    void EditorViewportClient::resetCameraToDefault()
    {
        _camera.resetToDefault();
        _toolbarSettings._cameraSpeed = getPreferences<EditorViewportPreferences>()._cameraSpeed;
    }

    void EditorViewportClient::update( float32 deltaTime, bool bWindowFocused, bool bWindowHovered )
    {
        const EditorPointerState pointer = EditorInputGUI::readPointer();
        // 비행(오른쪽 단추를 누른 채 WASD)은 모드 레이어다 — 누른 동안만 켜서 같은 W 가 기즈모 모드(패널 레이어)와 겹치지 않는다(우선순위가 높고 아래를 막는다).
        const bool bAlt    = pointer.hasModifier( ModifierKey::Alt );
        const bool bFlying = ( bWindowHovered || bWindowFocused ) && bAlt == false && pointer.isDown( EditorMouseButton::Right );
        EditorInputGUI::setModeLayerEnabled( EditorViewportClientInternal::kFlyLayer, bFlying );
        if ( bWindowHovered || bWindowFocused )
        {
            _camera.applyBookmarkRequest( _toolbarSettings._requestedBookmarkSlot );

            // 키는 `Editor.Panel.scene_view` 레이어의 액션이다(씬 뷰가 포커스이고 글을 치는 중이 아닐 때만 발화한다).
            _camera.processBookmarkKeys();
            if ( EditorInputGUI::wasActionTriggered( "viewport.frameSelected" ) )
                _camera.frameSelected();
            if ( bFlying == false )
                processShortcutKeys();

            // 휠은 직교 보기에서 확대 · 축소, 원근 보기에서 궤도 거리 줌(dolly)이다. 가운데 단추 끌기는 팬이다(언리얼 · 유니티 · Godot).
            const bool bWheel = bWindowHovered && MathUtil::abs( pointer._wheel ) > 0.01f;
            if ( bAlt )
            {
                _camera.processOrbitInput();
            }
            else if ( bFlying )
            {
                _camera.processFlyInput( deltaTime, _toolbarSettings._cameraSpeed );
            }
            else
            {
                if ( pointer.isDown( EditorMouseButton::Middle ) && bWindowHovered )
                    _camera.pan( pointer._delta._x, pointer._delta._y, _canvasHeight );
                if ( bWheel )
                {
                    if ( _camera.isOrthographicView() )
                        _camera.zoomOrthographic( pointer._wheel );
                    else
                        _camera.dolly( pointer._wheel );
                }
            }
        }

        _camera.applyToCamera( getSceneViewCamera() );
    }

    void EditorViewportClient::processShortcutKeys()
    {
        // 기즈모 모드는 유니티, 언리얼, Godot 모두 W, E, R 이다. Q 와 Space 는 다음 모드로 순환한다(Space 는 언리얼). Shift+Space 는 씬 뷰 최대화다.
        // 키는 `Editor.Panel.scene_view` 레이어가 정한다 — 수정 키는 정확히 맞아야 발화하므로 Ctrl+R 이 R 이 되지 않는다.
        EditorWorkspace& ws = editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>();
        if ( EditorInputGUI::wasActionTriggered( "viewport.gizmoTranslate" ) )
            ws.setGizmoOperation( 0 );
        if ( EditorInputGUI::wasActionTriggered( "viewport.gizmoRotate" ) )
            ws.setGizmoOperation( 1 );
        if ( EditorInputGUI::wasActionTriggered( "viewport.gizmoScale" ) )
            ws.setGizmoOperation( 2 );
        if ( EditorInputGUI::wasActionTriggered( "viewport.gizmoCycle" ) )
            ws.setGizmoOperation( ( ws.getGizmoOperation() + 1 ) % 3 );
        if ( EditorInputGUI::wasActionTriggered( "viewport.toggleMaximize" ) )
            toggleMaximize();
        // 직교 보기(Blender, Godot 키패드). 7 은 위, 1 은 앞, 3 은 오른쪽이고, 5 는 직교와 원근을 전환한다.
        if ( EditorInputGUI::wasActionTriggered( "viewport.viewTop" ) )
            _camera.setOrthographicView( 89.9f, 0.0f );
        if ( EditorInputGUI::wasActionTriggered( "viewport.viewFront" ) )
            _camera.setOrthographicView( 0.0f, 0.0f );
        if ( EditorInputGUI::wasActionTriggered( "viewport.viewRight" ) )
            _camera.setOrthographicView( 0.0f, -90.0f );
        if ( EditorInputGUI::wasActionTriggered( "viewport.toggleOrthographic" ) )
            _camera.toggleOrthographic();
    }

    void EditorViewportClient::toggleMaximize()
    {
        EditorContext& context = editor::getSubsystem<EditorContext>();
        if ( _bMaximized == SW_TRUE )
        {
            _bMaximized = SW_FALSE;
            if ( context.getSubsystem<IEditorPanelHost>().restoreMaximizedLayout() == false )
                SW_LOG_WARNING( "The layout before maximizing is gone - use Panel > Reset Layout" );
            return;
        }
        if ( context.getSubsystem<IEditorPanelHost>().maximizePanel( editorpanel::kSceneView ) )
            _bMaximized = SW_TRUE;
    }

    void EditorViewportClient::drawOverlays( const float2& canvasMin, const float2& canvasSize )
    {
        const bool bHasSelection = editor::getMainSceneSelection().getSelectedObjectCount() > 0;
        _overlays.draw( canvasMin, canvasSize, _toolbarSettings, bHasSelection );
    }

    void EditorViewportClient::draw( const void* pTextureID, const float2& canvasSize )
    {
        _canvasHeight         = canvasSize._y; // 팬이 마우스 픽셀을 월드 길이로 바꾸는 데 쓴다(다음 프레임의 update)
        const ImVec2 imagePos = ImGui::GetCursorScreenPos();
        if ( pTextureID != nullptr )
            ImGui::Image( reinterpret_cast<ImTextureID>( pTextureID ), ImVec2{ canvasSize._x, canvasSize._y } );
        else
            ImGui::Dummy( ImVec2{ canvasSize._x, canvasSize._y } );
        EditorSelfTestMarks::note( "sceneView.canvas" ); // 시나리오가 씬 뷰 가운데를 누른다. 뷰포트 피킹과 카메라 비행에 쓴다
        // 캔버스(이미지)의 호버와 클릭은 지금 읽는다. 뒤에서 시각화가 레이블 위치에 항목을 추가하면(빌보드) "마지막 항목" 이 바뀐다.
        const bool bCanvasHovered = ImGui::IsItemHovered();
        const bool bCanvasClicked = EditorInputGUI::wasItemClicked();
        // 씬 뷰(메인 월드)가 포커스면 인스펙터 · 전역 명령이 다시 메인 선택을 겨눈다 — 미리보기 창이 걸어 둔 것을 푼다(`editor::getFocusedSelection`).
        if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) )
            editor::setFocusedSelection( nullptr );

        CameraComponent* pCamera = getSceneViewCamera();
        const float2     canvasPos{ imagePos.x, imagePos.y };

        if ( pCamera != nullptr )
        {
            const float32  aspect   = canvasSize._x / ( canvasSize._y > 0.0f ? canvasSize._y : 1.0f );
            const float4x4 viewProj = pCamera->getViewProjectionMatrix( aspect );

            float32 arrView[16];
            float32 arrProj[16];
            EditorViewportProjectionUtil::storeGizmoMatrix( arrView, pCamera->getViewMatrix() );
            EditorViewportProjectionUtil::storeGizmoMatrix( arrProj, pCamera->getProjectionMatrix( aspect ) );

            // 격자, 시각화, 자, 통계, 방향 큐브는 창의 그리기 목록에 그린다. 캔버스로 자르지 않으면 화면 밖으로 뻗은 선(카메라 절두체 등)이
            // 탭과 툴바 위까지 그려진다. 기즈모는 ImGuizmo 가 같은 사각형(SetRect)으로 자른다.
            ImDrawList*  pCanvasDrawList = ImGui::GetWindowDrawList();
            const ImVec2 canvasMax{ imagePos.x + canvasSize._x, imagePos.y + canvasSize._y };
            pCanvasDrawList->PushClipRect( imagePos, canvasMax, true );
            if ( _toolbarSettings._bShowGrid )
                _canvasOverlay.drawAdaptiveGrid( ImGui::GetWindowDrawList(), canvasPos, canvasSize, arrView, arrProj, _camera.getPosition(), _toolbarSettings._bIs2DMode );

            // 시각화는 씬 전체를 복사하지 않고 매니저의 레지스트리(카메라 · 물리)를 읽는다.
            GameObjectManager* pActiveManager = editor::getActiveObjectManager();

            EditorViewportVisualizerArgs visualizerArgs{};
            visualizerArgs._pDrawList       = ImGui::GetWindowDrawList();
            visualizerArgs._pViewProj       = &viewProj;
            visualizerArgs._canvasPos       = canvasPos;
            visualizerArgs._canvasSize      = canvasSize;
            visualizerArgs._pActiveCamera   = pCamera;
            visualizerArgs._pListCamera     = ( pActiveManager != nullptr ) ? &pActiveManager->getCameraRegistry().getAll() : nullptr;
            visualizerArgs._pListCollider   = ( pActiveManager != nullptr ) ? &pActiveManager->getSubsystem<sw::ScenePhysics>().getColliders2D() : nullptr;
            visualizerArgs._pDebugDrawQueue = &editor::getSubsystem<DebugDrawQueue>();
            visualizerArgs._pObjectManager  = pActiveManager;
            visualizerArgs._bGizmoDragging  = _gizmo.isDragging();
            visualizerArgs._bFlat2D         = EditorVisualizerGeometryUtil::isFlat2DView( pCamera->isOrthographic(), pCamera->getViewMatrix() );
            visualizerArgs._pStats          = &_visualizerStats;
            // 마우스 양보는 시각화가 매 프레임 다시 잡아야 유지된다(끄는 중에 토글을 꺼 그리기 함수가 안 돌아도 풀린다). 클릭 선택은 이 그리기 뒤라 같은 프레임의 첫 클릭도 양보를 본다.
            EditorViewportMouseClaim::beginFrame( static_cast<uint64>( ImGui::GetFrameCount() ) );
            EditorViewportVisualizer::drawAll( visualizerArgs, _toolbarSettings._visualizerToggles );

            _canvasOverlay.processRulerTool( ImGui::GetWindowDrawList(), canvasPos, canvasSize, arrView, arrProj, _toolbarSettings._bShowRuler );
            pCanvasDrawList->PopClipRect();

            // 피킹과 호버는 기즈모보다 먼저 본다. ImGuizmo::IsOver · IsUsing 은 지난 프레임에 그린 기즈모의 상태다.
            _picker.processClick( canvasPos, canvasSize, pCamera, bCanvasClicked && ( EditorColliderHandleTool::isCapturingMouse() || EditorViewportMouseClaim::isClaimed() ) == false, _toolbarSettings );
            _picker.processHover( canvasPos, canvasSize, pCamera, bCanvasHovered, _gizmo.isDragging(), _toolbarSettings );
            EditorSelectionBounds::setHoveredObjectID( _picker.getHoveredObjectID() );
            processContextMenu( canvasPos, canvasSize, pCamera, bCanvasHovered );

            {
                GameObjectManager* pObjectManager = editor::getActiveObjectManager();
                if ( pObjectManager != nullptr )
                    pObjectManager->flushSceneTransforms();
                // 콜라이더 손잡이나 확장 도구(EditorViewportMouseClaim) 위에서는 기즈모가 마우스를 양보한다 — 손잡이는 기즈모 축 위에 놓인다.
                const bool bGizmoYields = EditorColliderHandleTool::isCapturingMouse() || EditorViewportMouseClaim::isClaimed();
                if ( bGizmoYields == false )
                    _gizmo.draw( editor::getMainSceneSelection(), arrView, arrProj, canvasPos, canvasSize, _toolbarSettings ); // 메인 뷰포트는 메인 선택
            }

            pCanvasDrawList->PushClipRect( imagePos, canvasMax, true );
            // 위쪽이나 오른쪽에 붙은 오버레이 바 아래에 둔다(지난 프레임 위치). 방향 큐브는 HUD 박스 아래다.
            const float2  topRightInset = _overlays.computeTopRightInset();
            const float32 hudBoxHeight  = _toolbarSettings._bShowHUD
                                            ? _hudView.draw( ImGui::GetWindowDrawList(), canvasPos, canvasSize, topRightInset, ImGui::GetIO().DeltaTime )
                                            : topRightInset._y;

            if ( _toolbarSettings._bShowOrientationCube )
                EditorViewportCanvasOverlay::drawOrientationCube( ImGui::GetWindowDrawList(), canvasPos, canvasSize, hudBoxHeight, _camera );
            pCanvasDrawList->PopClipRect();

            EditorViewportAssetDrop::processDrop( canvasPos, canvasSize, arrView, arrProj, _toolbarSettings._bIs2DMode );
        }
    }

    void EditorViewportClient::processContextMenu( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera, bool bCanvasHovered )
    {
        // 오른쪽 단추를 끌기 문턱 안에서 떼고 그 누름 동안 날지 않았으면 메뉴다(R1). 끌었거나 WASD 로 날았으면 비행이었다 — 비행과 메뉴는 이 캔버스가 가른다.
        // 오브젝트 위면 오브젝트 메뉴(계층 창과 같은 명령 — R4), 빈 곳이면 뷰포트 메뉴(여기 만들기 · 붙여 넣기 — 확장이 없어도 열린다).
        const EditorPointerState pointer  = EditorInputGUI::readPointer();
        const bool               bClicked = pointer.wasClickedWithoutDrag( EditorMouseButton::Right ) && _camera.hasFlownThisRightHold() == false;
        const bool               bOpen    = bCanvasHovered && bClicked && pointer.hasModifier( ModifierKey::Alt ) == false;
        if ( bOpen )
        {
            GameObject* pObject    = nullptr;
            Component*  pComponent = nullptr;
            _contextMenuObjectID   = _picker.findObjectUnderMouse( canvasPos, canvasSize, pCamera, _toolbarSettings, pObject, pComponent ) ? pObject->getObjectID() : 0;

            const float32 aspect = canvasSize._x / ( canvasSize._y > 0.0f ? canvasSize._y : 1.0f );
            EditorPickRay mouseRay{};
            const uint32  planeAxis       = _toolbarSettings._bIs2DMode ? 2u : 1u; // 애셋 드롭과 같은 바닥: 2D 는 Z = 0, 3D 는 Y = 0
            _bContextMenuHasWorldPosition = EditorViewportProjectionUtil::makeMouseRay( pCamera->getViewProjectionMatrix( aspect ).invert(), canvasPos, canvasSize, mouseRay ) &&
                                            EditorViewportRayPick::rayHitsAxisPlane( mouseRay, planeAxis, _contextMenuWorldPosition );
        }

        vector<GameObject*> listSelected;
        editor::getMainSceneSelection().getSelectedObjects( listSelected );
        EditorContextMenuSubject subject{};
        subject._pPopupID           = _contextMenuObjectID != 0 ? "##ViewportObjectMenu" : "##ViewportContextMenu";
        subject._kind               = _contextMenuObjectID != 0 ? EditorCommandTargetKind::GameObject : EditorCommandTargetKind::Viewport;
        subject._hoveredID          = _contextMenuObjectID;
        subject._worldPosition      = _contextMenuWorldPosition;
        subject._bHasWorldPosition  = _bContextMenuHasWorldPosition;
        subject._bCallerDecidesOpen = true;
        subject._bOpenRequested     = bOpen;
        for ( const GameObject* pSelected : listSelected )
        {
            subject._listSelectedID.push_back( pSelected->getObjectID() );
        }
        subject._selectOnly = &EditorViewportClientInternal::selectOnlyObject;
        (void)EditorInputGUI::drawContextMenuFor( subject ); // 열렸는지는 쓰지 않는다
    }
} // namespace sw::editor
