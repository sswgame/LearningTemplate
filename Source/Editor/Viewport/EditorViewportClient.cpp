#include "pch.h"

#include "Editor/Viewport/EditorViewportClient.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/EditorContextMenuExtension.h"
#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/IEditorPanelHost.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorColliderHandleTool.h"
#include "Editor/Viewport/EditorViewportAssetDrop.h"
#include "Editor/Viewport/EditorViewportMouseClaim.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportRayPick.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/EditorVisualizerGeometry.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
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
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorViewportClient::EditorViewportClient()
        : _toolbarSettings{}
        , _camera{}
        , _picker{}
        , _gizmo{}
        , _canvasOverlay{}
        , _overlays{}
        , _hudView{}
        , _listSceneObject{}
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
        _toolbarSettings._rotationSnapValue          = preferences._rotationSnapValue;
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
        if ( bWindowHovered || bWindowFocused )
        {
            ImGuiIO& io = ImGui::GetIO();

            _camera.applyBookmarkRequest( _toolbarSettings._requestedBookmarkSlot );

            if ( io.WantTextInput == false )
            {
                _camera.processBookmarkKeys();

                const bool bFrameSelectedPressed = ImGui::IsKeyPressed( ImGuiKey_F, false ) && io.KeyCtrl == false && io.KeyAlt == false;
                if ( bFrameSelectedPressed )
                    _camera.frameSelected();
                if ( io.MouseDown[1] == false )
                    processShortcutKeys();
            }

            const bool bOrthographicZoom = _camera.isOrthographicView() && bWindowHovered && MathUtil::abs( io.MouseWheel ) > 0.01f;
            if ( io.KeyAlt )
                _camera.processOrbitInput();
            else if ( io.MouseDown[1] )
                _camera.processFlyInput( deltaTime, _toolbarSettings._cameraSpeed );
            else if ( bOrthographicZoom )
                _camera.zoomOrthographic( io.MouseWheel );
        }

        _camera.applyToCamera( getSceneViewCamera() );
    }

    void EditorViewportClient::processShortcutKeys()
    {
        ImGuiIO& io = ImGui::GetIO();
        if ( io.KeyCtrl || io.KeyAlt )
            return;
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;
        // 기즈모 모드는 유니티, 언리얼, Godot 모두 W, E, R 이다. Q 와 Space 는 다음 모드로 순환한다(Space 는 언리얼).
        EditorWorkspace& ws = pContext->getSubsystem<EditorWorkspace>();
        if ( io.KeyShift == false )
        {
            if ( ImGui::IsKeyPressed( ImGuiKey_W, false ) )
                ws.setGizmoOperation( 0 );
            if ( ImGui::IsKeyPressed( ImGuiKey_E, false ) )
                ws.setGizmoOperation( 1 );
            if ( ImGui::IsKeyPressed( ImGuiKey_R, false ) )
                ws.setGizmoOperation( 2 );
            if ( ImGui::IsKeyPressed( ImGuiKey_Q, false ) || ImGui::IsKeyPressed( ImGuiKey_Space, false ) )
                ws.setGizmoOperation( ( ws.getGizmoOperation() + 1 ) % 3 );
        }
        else if ( ImGui::IsKeyPressed( ImGuiKey_Space, false ) )
        {
            toggleMaximize();
        }
        // 직교 보기(Blender, Godot 키패드). 7 은 위, 1 은 앞, 3 은 오른쪽이고, 5 는 직교와 원근을 전환한다.
        if ( ImGui::IsKeyPressed( ImGuiKey_Keypad7, false ) )
            _camera.setOrthographicView( 89.9f, 0.0f );
        if ( ImGui::IsKeyPressed( ImGuiKey_Keypad1, false ) )
            _camera.setOrthographicView( 0.0f, 0.0f );
        if ( ImGui::IsKeyPressed( ImGuiKey_Keypad3, false ) )
            _camera.setOrthographicView( 0.0f, -90.0f );
        if ( ImGui::IsKeyPressed( ImGuiKey_Keypad5, false ) )
            _camera.toggleOrthographic();
    }

    void EditorViewportClient::toggleMaximize()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;
        if ( _bMaximized == SW_TRUE )
        {
            _bMaximized = SW_FALSE;
            if ( pContext->getSubsystem<IEditorPanelHost>().restoreMaximizedLayout() == false )
                SW_LOG_WARNING( "The layout before maximizing is gone - use Panel > Reset Layout" );
            return;
        }
        if ( pContext->getSubsystem<IEditorPanelHost>().maximizePanel( "scene_view" ) )
            _bMaximized = SW_TRUE;
    }

    void EditorViewportClient::drawOverlays( const float2& canvasMin, const float2& canvasSize )
    {
        EditorContext* pContext      = EditorContext::get();
        const bool     bHasSelection = pContext != nullptr && pContext->getSubsystem<EditorSelection>().getSelectedObjectCount() > 0;
        _overlays.draw( canvasMin, canvasSize, _toolbarSettings, bHasSelection );
    }

    void EditorViewportClient::draw( const void* pTextureID, const float2& canvasSize )
    {
        const ImVec2 imagePos = ImGui::GetCursorScreenPos();
        if ( pTextureID != nullptr )
            ImGui::Image( reinterpret_cast<ImTextureID>( pTextureID ), ImVec2{ canvasSize._x, canvasSize._y } );
        else
            ImGui::Dummy( ImVec2{ canvasSize._x, canvasSize._y } );
        EditorSelfTestMarks::note( "sceneView.canvas" ); // 시나리오가 씬 뷰 가운데를 누른다. 뷰포트 피킹과 카메라 비행에 쓴다
        // 캔버스(이미지)의 호버와 클릭은 지금 읽는다. 뒤에서 시각화가 레이블 위치에 항목을 추가하면(빌보드) "마지막 항목" 이 바뀐다.
        const bool bCanvasHovered = ImGui::IsItemHovered();
        const bool bCanvasClicked = ImGui::IsItemClicked( ImGuiMouseButton_Left );

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
                EditorViewportCanvasOverlay::drawAdaptiveGrid( ImGui::GetWindowDrawList(), canvasPos, canvasSize, arrView, arrProj, _camera.getPosition(),
                                                               _toolbarSettings._bIs2DMode );

            // 이 프레임의 스냅샷을 한 번만 만든다. 시각화와 아래의 통계 오버레이가 함께 읽는다.
            _listSceneObject.clear();
            GameObjectManager* pSnapshotManager = editor::getActiveObjectManager();
            if ( pSnapshotManager != nullptr )
                pSnapshotManager->getAllGameObjects( _listSceneObject );

            EditorViewportVisualizerArgs visualizerArgs{};
            visualizerArgs._pDrawList       = ImGui::GetWindowDrawList();
            visualizerArgs._pViewProj       = &viewProj;
            visualizerArgs._canvasPos       = canvasPos;
            visualizerArgs._canvasSize      = canvasSize;
            visualizerArgs._pActiveCamera   = pCamera;
            visualizerArgs._pListObject     = &_listSceneObject;
            visualizerArgs._pListCamera     = ( pSnapshotManager != nullptr ) ? &pSnapshotManager->getCameraRegistry().getAll() : nullptr;
            visualizerArgs._pListCollider   = ( pSnapshotManager != nullptr ) ? &pSnapshotManager->getSubsystem<sw::ScenePhysics>().getColliders2D() : nullptr;
            visualizerArgs._pDebugDrawQueue = findSubsystem<DebugDrawQueue>();
            visualizerArgs._pObjectManager  = pSnapshotManager;
            visualizerArgs._bGizmoDragging  = _gizmo.isDragging();
            visualizerArgs._bFlat2D         = EditorVisualizerGeometryUtil::isFlat2DView( pCamera->isOrthographic(), pCamera->getViewMatrix() );
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
                EditorContext* pGizmoContext = EditorContext::get();
                const bool     bGizmoYields  = EditorColliderHandleTool::isCapturingMouse() || EditorViewportMouseClaim::isClaimed();
                if ( pGizmoContext != nullptr && bGizmoYields == false )
                    _gizmo.draw( pGizmoContext->getSubsystem<EditorSelection>(), arrView, arrProj, canvasPos, canvasSize, _toolbarSettings ); // 메인 뷰포트는 메인 선택
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
        static constexpr const utf8* kPopupID = "##ViewportContextMenu";
        // 패널 고유 항목이 없으므로 등록된 항목이 없으면 메뉴도 없다. 오른쪽 단추를 끌었으면 비행 · 궤도 줌이었다.
        // GetMouseDragDelta 는 끌기 문턱을 넘지 않았으면 0 이고, 놓은 프레임에도 값이 남는다(IsMouseDragging 은 놓은 프레임에 거짓이다).
        // 끌지 않고 WASD 로만 날았어도 비행이다 — 카메라가 그 누름 동안 날았는지(hasFlownThisRightHold)를 함께 본다.
        const ImVec2 dragDelta  = ImGui::GetMouseDragDelta( ImGuiMouseButton_Right );
        const bool   bNavigated = ( dragDelta.x != 0.0f || dragDelta.y != 0.0f ) || _camera.hasFlownThisRightHold();
        const bool   bOpen      = bCanvasHovered && ImGui::IsMouseReleased( ImGuiMouseButton_Right ) && bNavigated == false && ImGui::GetIO().KeyAlt == false &&
                           EditorContextMenuExtensionUtil::countEntries( EditorContextMenuKind::Viewport ) != 0;
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
            ImGui::OpenPopup( kPopupID );
        }

        if ( ImGui::BeginPopup( kPopupID ) == false )
            return;

        // 메뉴가 열린 동안 오브젝트가 지워질 수 있으므로 포인터를 들고 있지 않고 id 로 다시 찾는다.
        GameObjectManager*      pManager = editor::getActiveObjectManager();
        EditorContextMenuTarget target{};
        target._kind              = EditorContextMenuKind::Viewport;
        target._pManager          = pManager;
        target._pGameObject       = ( pManager != nullptr && _contextMenuObjectID != 0 ) ? pManager->findGameObjectByID( _contextMenuObjectID ) : nullptr;
        target._worldPosition     = _contextMenuWorldPosition;
        target._bHasWorldPosition = _bContextMenuHasWorldPosition;

        void ( *pfnAction )( const EditorContextMenuTarget& ) =
            EditorContextMenuExtensionUtil::visitEntries( EditorContextMenuKind::Viewport, [&]( const EditorContextMenuEntryRegistration& entry )
        {
            return ImGui::MenuItem( entry._pLabel, nullptr, false, EditorContextMenuExtensionUtil::isEnabled( entry, target ) );
        } );
        ImGui::EndPopup();

        // 동작은 순회가 끝난 뒤 부른다. 동작이 모듈을 내려 목록이 바뀌어도 순회 중인 배열을 읽지 않는다.
        if ( pfnAction != nullptr )
            pfnAction( target );
    }
} // namespace sw::editor
