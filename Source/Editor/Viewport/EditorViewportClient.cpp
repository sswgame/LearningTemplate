#include "pch.h"

#include "Editor/Viewport/EditorViewportClient.h"

#include "Core/Common/Defines.h"
#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/Commands/EditorSceneCommands.h"
#include "Editor/Common/Commands/EditorViewportPick.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorCamera.h"
#include "Editor/Viewport/EditorGridUtil.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportToolbar.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/EditorVisualizerGeometry.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include <imgui.h>
#include <ImGuizmo.h>

namespace sw::editor
{
    namespace
    {
        struct EditorViewportClientInternal
        {
            // 축 색의 정본. 그리드 · 오리엔테이션 큐브가 같은 값을 본다. 기즈모(ImGuizmo)도 같은 관례(X 빨강 · Y 초록 · Z 파랑)를 쓴다.
            static constexpr ImU32 _s_kColorAxisX = IM_COL32( 235, 65, 65, 255 );
            static constexpr ImU32 _s_kColorAxisY = IM_COL32( 65, 220, 95, 255 );
            static constexpr ImU32 _s_kColorAxisZ = IM_COL32( 65, 130, 245, 255 );

            /**
             * @brief 엔진 행렬을 ImGuizmo 배열(float[16])로 옮깁니다. **그대로 복사한다.**
             * @details 엔진 float4x4 는 행 벡터 규약(이동이 _41 · _42 · _43)의 행 우선 저장이라 메모리가 곧 ImGuizmo 가 받는 배열
             *          (이동이 [12] · [13] · [14])이다. 전치하면 이동이 [3] · [7] · [11] 로 가서 ImGuizmo 는 물체를 원점 · 카메라 눈 자리로 보고
             *          (클립 w = 0) 기즈모를 엉뚱한 곳에 그리며 손잡이를 잡지 못한다.
             */
            static void storeGizmoMatrix( float32* pOut, const float4x4& matrix ) { Memory::copy( pOut, &matrix, sizeof( float32 ) * 16 ); }

            static void loadGizmoMatrix( float4x4& outMatrix, const float32* pIn ) { Memory::copy( &outMatrix, pIn, sizeof( float32 ) * 16 ); }

            /** @brief view · proj 배열(ImGuizmo 형식)에서 뷰-투영 행렬을 만듭니다. */
            static void loadViewProj( const float32* pView, const float32* pProj, float4x4& outViewProj )
            {
                float4x4 viewMat{};
                float4x4 projMat{};
                loadGizmoMatrix( viewMat, pView );
                loadGizmoMatrix( projMat, pProj );
                outViewProj = viewMat * projMat;
            }

            /**
             * @brief 마우스 아래의 월드 레이를 만듭니다. 캔버스 밖이거나 퇴화했으면 false 입니다.
             * @details 피킹 · 자 · 애셋 드롭이 같은 레이를 씁니다. ImGui 에 닿는 것은 마우스 위치뿐이고, 나머지는
             *          `EditorViewportPick::makeRay` 라 테스트가 있습니다.
             */
            static bool makeMouseRay( const float4x4& invViewProj, const float2& canvasPos, const float2& canvasSize,
                                      EditorPickRay& outRay )
            {
                if ( canvasSize._x <= 1.0f || canvasSize._y <= 1.0f )
                    return false;

                const ImVec2  mouse = ImGui::GetMousePos();
                const float32 u     = ( mouse.x - canvasPos._x ) / canvasSize._x;
                const float32 v     = ( mouse.y - canvasPos._y ) / canvasSize._y;
                if ( u < 0.0f || 1.0f < u || v < 0.0f || 1.0f < v )
                    return false;
                return EditorViewportPick::makeRay( invViewProj, u, v, outRay );
            }

            static void applyWorldMatrix( SceneComponent* pSc, const float4x4& world )
            {
                if ( pSc != nullptr )
                    pSc->setWorldTransform( world );
            }

            /**
             * @brief 씬 뷰 이미지를 그린 카메라(에디터 카메라)입니다 — 호스트가 씬 뷰를 그리는 카메라와 같다(`ImGuiEditor::getSceneViewCamera`).
             * @details 오버레이(격자 · 시각화 · 디버그 드로우 · 피킹 · 기즈모)는 이 카메라로 투영해야 그림과 겹칩니다. Play 중에도 씬 뷰는 이 카메라다.
             */
            static CameraComponent* getSceneViewCamera()
            {
                return EditorCamera::ensure( editor::getActiveScene() );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorViewportClient::drawStatsOverlay( ImDrawList* pDrawList, const float2& canvasPos,
                                                 const float2& canvasSize )
    {
        if ( pDrawList == nullptr )
            return;

        const float32 fps         = ImGui::GetIO().Framerate;
        const float32 frameTimeMs = ( fps > 0.0f ) ? ( 1000.0f / fps ) : 0.0f;

        // 개수만 필요하므로 이 프레임에 이미 만들어 둔 스냅샷을 본다(씬 전체를 다시 복사하지 않는다).
        const uint32 totalObjects = static_cast<uint32>( _listSceneObject.size() );

        constexpr float32 overlayW = 160.0f;
        constexpr float32 overlayH = 72.0f;
        const float32     x0       = canvasPos._x + canvasSize._x - overlayW - 12.0f;
        const float32     y0       = canvasPos._y + 12.0f;
        const float32     x1       = x0 + overlayW;
        const float32     y1       = y0 + overlayH;

        // 배경과 테두리
        pDrawList->AddRectFilled( ImVec2( x0, y0 ), ImVec2( x1, y1 ), IM_COL32( 15, 17, 22, 210 ), 6.0f );
        pDrawList->AddRect( ImVec2( x0, y0 ), ImVec2( x1, y1 ), IM_COL32( 50, 60, 80, 180 ), 6.0f );

        // 텍스트 줄
        fixed_string<constant::kMaxBuffer32> arrFps;
        formatstring( arrFps.data(), arrFps.capacity(), "FPS: %# (%# ms)", Fmt( static_cast<float64>( fps ), Format().precision( 1 ) ),
                      Fmt( static_cast<float64>( frameTimeMs ), Format().precision( 2 ) ) );
        pDrawList->AddText( ImVec2( x0 + 10.0f, y0 + 8.0f ), IM_COL32( 80, 230, 120, 240 ), arrFps.c_str() );

        fixed_string<constant::kMaxBuffer32> arrObj;
        formatstring( arrObj.data(), arrObj.capacity(), "Objects: %u", totalObjects );
        pDrawList->AddText( ImVec2( x0 + 10.0f, y0 + 28.0f ), IM_COL32( 210, 215, 230, 230 ), arrObj.c_str() );

        fixed_string<constant::kMaxBuffer32> arrResolutionText;
        formatstring( arrResolutionText.data(), arrResolutionText.capacity(), "Res: %#×%#", Fmt( static_cast<float64>( canvasSize._x ), Format().precision( 0 ) ),
                      Fmt( static_cast<float64>( canvasSize._y ), Format().precision( 0 ) ) );
        pDrawList->AddText( ImVec2( x0 + 10.0f, y0 + 48.0f ), IM_COL32( 140, 160, 190, 220 ), arrResolutionText.c_str() );
    }

    EditorViewportClient::EditorViewportClient()
        : _cameraPos{ 0.0f, 3.0f, -6.0f }
        , _cameraRot{ 20.0f, 0.0f, 0.0f }
        , _orbitTarget{ 0.0f, 0.0f, 0.0f }
        , _rulerStartWorld{ 0.0f, 0.0f, 0.0f }
        , _rulerEndWorld{ 0.0f, 0.0f, 0.0f }
        , _orbitDistance{ 8.0f }
        , _fovY{ 60.0f }
        , _nearZ{ 0.1f }
        , _farZ{ 1000.0f }
        , _toolbarSettings{}
        , _gizmoUndoBefore{}
        , _gizmoObject{}
        , _listGizmoObject{}
        , _listGizmoUndo{}
        , _listGizmoRelativeWorld{}
        , _arrGizmoGroupMatrix{}
        , _lastGizmoFrame{ -1 }
        , _lastGizmoObjectCount{ 0 }
        , _bRulerActive{ SW_FALSE }
        , _bGizmoTracking{ SW_FALSE }
        , _reservedGizmo{ 0 }
    {
        // 어떤 시각화가 기본으로 켜지는지는 시각화 표가 정한다.
        _toolbarSettings._visualizerMask = EditorViewportVisualizer::getDefaultMask();
    }

    void EditorViewportClient::getViewMatrix( float32* pOutMatrix ) const
    {
        const float32 pitchRad = MathUtil::toRadian( _cameraRot._x );
        const float32 yawRad   = MathUtil::toRadian( _cameraRot._y );

        const float3   forward{ MathUtil::sin( yawRad ) * MathUtil::cos( pitchRad ), -MathUtil::sin( pitchRad ),
                              MathUtil::cos( yawRad ) * MathUtil::cos( pitchRad ) };
        const float4x4 viewMat = float4x4::makeLookAt( _cameraPos, _cameraPos + forward, float3::Up );
        EditorViewportClientInternal::storeGizmoMatrix( pOutMatrix, viewMat );
    }

    void EditorViewportClient::getProjectionMatrix( float32* pOutMatrix, float32 aspect ) const
    {
        const float32  effectiveAspect = aspect > 0.001f ? aspect : 1.0f;
        const float4x4 projMat         = float4x4::makePerspectiveFieldOfView( MathUtil::toRadian( _fovY ), effectiveAspect, _nearZ, _farZ );
        EditorViewportClientInternal::storeGizmoMatrix( pOutMatrix, projMat );
    }

    void EditorViewportClient::update( float32 deltaTime, bool bWindowFocused, bool bWindowHovered )
    {
        if ( bWindowHovered || bWindowFocused )
        {
            ImGuiIO& io = ImGui::GetIO();

            if ( _toolbarSettings._requestedBookmarkSlot >= 0 )
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr )
                {
                    const CameraBookmark* pBm = pContext->getWorkspace().getCameraBookmark(
                        static_cast<uint32>( _toolbarSettings._requestedBookmarkSlot ) );
                    if ( pBm != nullptr && pBm->_bValid )
                    {
                        _cameraPos     = pBm->_position;
                        _cameraRot     = pBm->_rotation;
                        _orbitTarget   = pBm->_orbitTarget;
                        _orbitDistance = pBm->_orbitDistance;
                    }
                }
                _toolbarSettings._requestedBookmarkSlot = -1;
            }

            if ( io.WantTextInput == false )
            {
                for ( int32 keyIndex = 0; keyIndex < 9; ++keyIndex )
                {
                    const ImGuiKey key = static_cast<ImGuiKey>( ImGuiKey_1 + keyIndex );
                    if ( ImGui::IsKeyPressed( key, false ) )
                    {
                        EditorContext* pContext = EditorContext::get();
                        if ( pContext != nullptr )
                        {
                            if ( io.KeyCtrl )
                            {
                                CameraBookmark bm{};
                                bm._position      = _cameraPos;
                                bm._rotation      = _cameraRot;
                                bm._orbitTarget   = _orbitTarget;
                                bm._orbitDistance = _orbitDistance;
                                fixed_string<constant::kMaxBuffer32> arrName;
                                formatstring( arrName.data(), arrName.capacity(), "POI %d", keyIndex + 1 );
                                bm._name = arrName.c_str();
                                pContext->getWorkspace().setCameraBookmark( static_cast<uint32>( keyIndex ), bm );
                            }
                            else if ( io.KeyAlt == false && io.KeyShift == false )
                            {
                                const CameraBookmark* pBm = pContext->getWorkspace().getCameraBookmark(
                                    static_cast<uint32>( keyIndex ) );
                                if ( pBm != nullptr && pBm->_bValid )
                                {
                                    _cameraPos     = pBm->_position;
                                    _cameraRot     = pBm->_rotation;
                                    _orbitTarget   = pBm->_orbitTarget;
                                    _orbitDistance = pBm->_orbitDistance;
                                }
                            }
                        }
                    }
                }

                if ( ImGui::IsKeyPressed( ImGuiKey_F, false ) && io.KeyCtrl == false && io.KeyAlt == false )
                    frameSelected();
            }

            if ( io.KeyAlt )
                processOrbitInput();
            else if ( io.MouseDown[1] )
                processFlyInput( deltaTime );
        }

        CameraComponent* pCam = EditorViewportClientInternal::getSceneViewCamera();
        if ( pCam != nullptr )
        {
            pCam->setLocalPosition( _cameraPos );
            const float32 pitchRad = MathUtil::toRadian( _cameraRot._x );
            const float32 yawRad   = MathUtil::toRadian( _cameraRot._y );
            const float3  forward{ MathUtil::sin( yawRad ) * MathUtil::cos( pitchRad ), -MathUtil::sin( pitchRad ),
                                  MathUtil::cos( yawRad ) * MathUtil::cos( pitchRad ) };
            pCam->lookAt( _cameraPos + forward );
        }
    }

    void EditorViewportClient::processFlyInput( float32 deltaTime )
    {
        ImGuiIO& io = ImGui::GetIO();

        // 회전 (마우스 델타)
        _cameraRot._y += io.MouseDelta.x * 0.2f;
        _cameraRot._x += io.MouseDelta.y * 0.2f;
        _cameraRot._x = MathUtil::clamp( _cameraRot._x, -89.0f, 89.0f );

        // 이동 (WASD + QE)
        const float32 pitchRad = MathUtil::toRadian( _cameraRot._x );
        const float32 yawRad   = MathUtil::toRadian( _cameraRot._y );

        const float3 forward = float3{ MathUtil::sin( yawRad ) * MathUtil::cos( pitchRad ), -MathUtil::sin( pitchRad ),
                                       MathUtil::cos( yawRad ) * MathUtil::cos( pitchRad ) };
        const float3 right   = float3{ MathUtil::cos( yawRad ), 0.0f, -MathUtil::sin( yawRad ) };
        const float3 up      = float3{ 0.0f, 1.0f, 0.0f };

        float3 moveDir{ 0.0f, 0.0f, 0.0f };
        if ( ImGui::IsKeyDown( ImGuiKey_W ) )
            moveDir += forward;
        if ( ImGui::IsKeyDown( ImGuiKey_S ) )
            moveDir -= forward;
        if ( ImGui::IsKeyDown( ImGuiKey_D ) )
            moveDir += right;
        if ( ImGui::IsKeyDown( ImGuiKey_A ) )
            moveDir -= right;
        if ( ImGui::IsKeyDown( ImGuiKey_E ) )
            moveDir += up;
        if ( ImGui::IsKeyDown( ImGuiKey_Q ) )
            moveDir -= up;

        const float32 speed = _toolbarSettings._cameraSpeed * ( io.KeyShift ? 3.0f : 1.0f ) * deltaTime;
        _cameraPos += moveDir * speed;
    }

    void EditorViewportClient::processOrbitInput()
    {
        ImGuiIO& io = ImGui::GetIO();

        // Alt + LMB: 궤도 회전
        if ( io.MouseDown[0] )
        {
            _cameraRot._y += io.MouseDelta.x * 0.3f;
            _cameraRot._x += io.MouseDelta.y * 0.3f;
            _cameraRot._x = MathUtil::clamp( _cameraRot._x, -89.0f, 89.0f );
        }

        // Alt + RMB 또는 휠: 궤도 줌
        if ( io.MouseDown[1] )
            _orbitDistance = MathUtil::max( _orbitDistance + ( io.MouseDelta.x - io.MouseDelta.y ) * 0.05f, 0.5f );

        if ( MathUtil::abs( io.MouseWheel ) > 0.01f )
            _orbitDistance = MathUtil::max( _orbitDistance - io.MouseWheel * 1.0f, 0.5f );

        const float32 pitchRad = MathUtil::toRadian( _cameraRot._x );
        const float32 yawRad   = MathUtil::toRadian( _cameraRot._y );

        const float3 forward = float3{ MathUtil::sin( yawRad ) * MathUtil::cos( pitchRad ), -MathUtil::sin( pitchRad ),
                                       MathUtil::cos( yawRad ) * MathUtil::cos( pitchRad ) };

        _cameraPos = _orbitTarget - forward * _orbitDistance;
    }

    void EditorViewportClient::drawViewportToolbar( float32 viewportWidth )
    {
        EditorViewportToolbar::draw( _toolbarSettings, viewportWidth );
    }

    void EditorViewportClient::drawTransformBar( const float2& anchorPos, float32 maxWidth )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        const bool bHasSelection = pContext->getEditorSelection().getSelectedObjectCount() > 0;
        EditorViewportToolbar::drawTransformBar( _toolbarSettings, anchorPos, maxWidth, bHasSelection );
    }

    void EditorViewportClient::draw( const void* pTextureID, const float2& canvasSize )
    {
        const ImVec2 imagePos = ImGui::GetCursorScreenPos();
        if ( pTextureID != nullptr )
            ImGui::Image( reinterpret_cast<ImTextureID>( pTextureID ), ImVec2{ canvasSize._x, canvasSize._y } );
        else
            ImGui::Dummy( ImVec2{ canvasSize._x, canvasSize._y } );
        EditorSelfTestMarks::note( "sceneView.canvas" ); // 시나리오가 씬 뷰 가운데를 누른다(뷰포트 피킹 · 카메라 비행)

        CameraComponent* pCamera = EditorViewportClientInternal::getSceneViewCamera();
        const float2     canvasPos{ imagePos.x, imagePos.y };

        if ( pCamera != nullptr )
        {
            const float32  aspect   = canvasSize._x / ( canvasSize._y > 0.0f ? canvasSize._y : 1.0f );
            const float4x4 viewProj = pCamera->getViewProjectionMatrix( aspect );

            float32 arrView[16];
            float32 arrProj[16];
            EditorViewportClientInternal::storeGizmoMatrix( arrView, pCamera->getViewMatrix() );
            EditorViewportClientInternal::storeGizmoMatrix( arrProj, pCamera->getProjectionMatrix( aspect ) );

            // 격자 · 시각화 · 자 · 통계 · 방향 큐브는 창 그리기 목록에 그린다 — 캔버스로 자르지 않으면 화면 밖으로 뻗은 선(카메라 절두체 등)이
            // 탭 · 툴바 위까지 그려진다. 기즈모는 ImGuizmo 가 같은 사각형(SetRect)으로 자른다.
            ImDrawList*  pCanvasDrawList = ImGui::GetWindowDrawList();
            const ImVec2 canvasMax{ imagePos.x + canvasSize._x, imagePos.y + canvasSize._y };
            pCanvasDrawList->PushClipRect( imagePos, canvasMax, true );
            if ( _toolbarSettings._bShowGrid )
                drawAdaptiveGrid( ImGui::GetWindowDrawList(), canvasPos, canvasSize, arrView, arrProj );

            // 이 프레임의 스냅샷을 한 번만 만든다. 시각화와 아래 통계 오버레이가 함께 본다.
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
            visualizerArgs._pListCollider   = ( pSnapshotManager != nullptr ) ? &pSnapshotManager->getOverlapWorld2D().getColliders() : nullptr;
            visualizerArgs._pDebugDrawQueue = getService<DebugDrawQueue>();
            visualizerArgs._bFlat2D         = EditorVisualizerGeometryUtil::isFlat2DView( pCamera->isOrthographic(), pCamera->getViewMatrix() );
            EditorViewportVisualizer::drawAll( visualizerArgs, _toolbarSettings._visualizerMask );

            processRulerTool( ImGui::GetWindowDrawList(), canvasPos, canvasSize, arrView, arrProj );
            pCanvasDrawList->PopClipRect();

            processPicking( canvasPos, canvasSize, pCamera );

            {
                GameObjectManager* pObjectManager = editor::getActiveObjectManager();
                if ( pObjectManager != nullptr )
                    pObjectManager->flushSceneTransforms();
                drawGizmo( arrView, arrProj, canvasPos, canvasSize );
            }

            pCanvasDrawList->PushClipRect( imagePos, canvasMax, true );
            if ( _toolbarSettings._bShowStats )
                drawStatsOverlay( ImGui::GetWindowDrawList(), canvasPos, canvasSize );

            if ( _toolbarSettings._bShowOrientationCube )
                drawOrientationCube( ImGui::GetWindowDrawList(), canvasPos, canvasSize );
            pCanvasDrawList->PopClipRect();

            string droppedAssetPath;
            if ( EditorWidgets::acceptAssetDrop( droppedAssetPath ) )
                handleViewportAssetDrop( droppedAssetPath.c_str(), canvasPos, canvasSize, arrView, arrProj );
        }
    }

    void EditorViewportClient::processPicking( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        if ( pCamera == nullptr )
            return;
        if ( ImGui::IsItemClicked( ImGuiMouseButton_Left ) == false )
            return;
        if ( ImGuizmo::IsOver() || ImGuizmo::IsUsing() )
            return;
        if ( ImGui::GetIO().KeyAlt )
            return;
        const float32  aspect      = canvasSize._x / ( canvasSize._y > 0.0f ? canvasSize._y : 1.0f );
        const float4x4 invViewProj = pCamera->getViewProjectionMatrix( aspect ).invert();
        EditorPickRay  pickRay{};
        if ( EditorViewportClientInternal::makeMouseRay( invViewProj, canvasPos, canvasSize, pickRay ) == false )
            return;

        Scene* pScene = editor::getActiveScene();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;
        GameObjectManager* pManager = pScene->getObjectManager();
        pManager->flushSceneTransforms();

        // 어떤 컴포넌트 종류를 집을 수 있는지는 EditorViewportPick 의 표가 정한다 (ImGui 없이 테스트된다).
        EditorPickResult pickResult{};
        if ( EditorViewportPick::pick( pManager, pickRay, _toolbarSettings._bIs2DMode, pickResult ) )
        {
            pContext->getWorkspace().selectComponent( pickResult._pObject,
                                                      pickResult._pComponent );
        }
        else
        {
            pContext->getWorkspace().clearSelection();
        }
    }

    void EditorViewportClient::drawGizmo( const float32* pView, const float32* pProj, const float2& canvasPos,
                                          const float2& canvasSize )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;
        if ( EditorUtil::areSceneEditsAllowed() == false )
        {
            endGizmoDrag();
            return;
        }

        vector<GameObject*> listSelected;
        pContext->getEditorSelection().getSelectedObjects( listSelected );
        vector<GameObject*> listGizmo;
        listGizmo.reserve( listSelected.size() );
        for ( GameObject* pRaw : listSelected )
        {
            if ( pRaw->getPrimarySceneComponent() == nullptr )
                continue;
            listGizmo.push_back( pRaw );
        }
        if ( listGizmo.empty() )
        {
            endGizmoDrag();
            return;
        }

        _lastGizmoFrame       = ImGui::GetFrameCount();
        _lastGizmoObjectCount = static_cast<uint32>( listGizmo.size() );
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect( canvasPos._x, canvasPos._y, canvasSize._x, canvasSize._y );
        // beginFrame 이 프레임마다 끈다 — 캔버스가 이번 프레임에 그려졌고 편집이 허용될 때(위에서 걸렀다)만 켠다. 끈 채면 그리기만 하고 조작을 받지 않는다.
        ImGuizmo::Enable( true );

        EditorWorkspace&    ws    = pContext->getWorkspace();
        const int32         opInt = ws.getGizmoOperation();
        ImGuizmo::OPERATION op    = ImGuizmo::TRANSLATE;
        if ( opInt == 1 )
            op = ImGuizmo::ROTATE;
        else if ( opInt == 2 )
            op = ImGuizmo::SCALE;

        const ImGuizmo::MODE mode = ws.isGizmoLocalSpace() ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

        float32 arrSnap[3] = { 0.0f, 0.0f, 0.0f };
        if ( op == ImGuizmo::TRANSLATE && _toolbarSettings._bGridSnap )
            arrSnap[0] = arrSnap[1] = arrSnap[2] = _toolbarSettings._gridSnapValue;
        else if ( op == ImGuizmo::ROTATE && _toolbarSettings._bRotationSnap )
            arrSnap[0] = arrSnap[1] = arrSnap[2] = _toolbarSettings._rotationSnapValue;
        else if ( op == ImGuizmo::SCALE && _toolbarSettings._bScaleSnap )
            arrSnap[0] = arrSnap[1] = arrSnap[2] = _toolbarSettings._scaleSnapValue;
        const bool bUseSnap = ( arrSnap[0] > 0.0f );

        const bool bGroup = ( listGizmo.size() > 1 );
        if ( bGroup )
        {
            manipulateGroupGizmo( pView, pProj, static_cast<uint32>( op ), static_cast<uint32>( mode ), listGizmo, bUseSnap, bUseSnap ? arrSnap : nullptr );
            return;
        }

        GameObject*     pRaw       = listGizmo[0];
        SceneComponent* pSceneComp = pRaw->getPrimarySceneComponent();
        if ( pSceneComp == nullptr )
            return;
        // 추적 중인 드래그가 이 오브젝트 것이 아니면(선택이 바뀌었다 · 그룹 드래그였다) 먼저 정리한다 — 그 스냅숏을 이 오브젝트에 커밋하지 않는다.
        if ( _bGizmoTracking == SW_TRUE && _gizmoObject != pRaw->getHandle() )
            endGizmoDrag();

        float32 arrMatrix[16];
        EditorViewportClientInternal::storeGizmoMatrix( arrMatrix, pSceneComp->getWorldMatrix() );

        if ( _bGizmoTracking == SW_FALSE && ImGuizmo::IsOver() && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
        {
            _gizmoUndoBefore = EditorTransaction::captureSnapshot( pRaw );
            _gizmoObject     = pRaw->getHandle();
        }

        if ( ImGuizmo::Manipulate( pView, pProj, op, mode, arrMatrix, nullptr, bUseSnap ? arrSnap : nullptr ) )
        {
            float4x4 newWorldMat{};
            EditorViewportClientInternal::loadGizmoMatrix( newWorldMat, arrMatrix );

            // 표면 붙이기는 **월드** 위치 · 월드 Y 스케일로 한다. 로컬로 분해한 값을 넘기면 부모가 있을 때 다른 오브젝트의 월드 윗면과
            // 로컬 높이를 견준다.
            if ( op == ImGuizmo::TRANSLATE && _toolbarSettings._bSurfaceSnap )
            {
                float3 worldTranslation = newWorldMat.getTranslation();
                EditorSceneCommands::snapTranslationToSurface( pRaw, worldTranslation );
                newWorldMat.setTranslation( worldTranslation );
            }

            // 분해는 엔진이 한다(엔진의 오일러 규칙 — ImGuizmo 의 XYZ 분해는 엔진의 요 · 피치 · 롤 순서와 달라 섞인 회전이 틀어진다).
            EditorSceneCommands::applyWorldTransform( pRaw, newWorldMat );
        }

        if ( ImGuizmo::IsUsing() )
        {
            // 클릭 없이 시작된 드래그(스냅숏이 없다)는 대상만 기억한다 — 커밋할 "이전" 이 없으니 끝에서 버린다.
            if ( _bGizmoTracking == SW_FALSE && _gizmoObject != pRaw->getHandle() )
            {
                _gizmoUndoBefore = ObjectSnapshot{};
                _gizmoObject     = pRaw->getHandle();
            }
            _bGizmoTracking = SW_TRUE;
        }
        else if ( _bGizmoTracking == SW_TRUE )
        {
            endGizmoDrag();
        }
    }

    void EditorViewportClient::endGizmoDrag()
    {
        if ( _bGizmoTracking == SW_TRUE && EditorUtil::areSceneEditsAllowed() )
        {
            // 단일 드래그: 스냅숏을 찍은 **그 오브젝트**에만 커밋한다(핸들로 다시 찾는다 — 지워졌으면 버린다).
            GameObject* pTracked = editor::findGameObject( _gizmoObject );
            if ( pTracked != nullptr && _gizmoUndoBefore._xml.empty() == false )
                EditorSceneCommands::commitModify( pTracked, _gizmoUndoBefore, "Gizmo Transform" );
            // 그룹 드래그: 살아 있는 대상마다 커밋한다.
            const uint32 count = static_cast<uint32>( _listGizmoObject.size() );
            for ( uint32 objectIndex = 0; objectIndex < count; ++objectIndex )
            {
                GameObject* pObj = editor::findGameObject( _listGizmoObject[objectIndex] );
                if ( pObj != nullptr )
                    EditorSceneCommands::commitModify( pObj, _listGizmoUndo[objectIndex], "Gizmo Transform" );
            }
        }
        _gizmoUndoBefore = ObjectSnapshot{};
        _gizmoObject     = GameObjectHandle{};
        _listGizmoObject.clear();
        _listGizmoUndo.clear();
        _listGizmoRelativeWorld.clear();
        _bGizmoTracking = SW_FALSE;
    }

    void EditorViewportClient::manipulateGroupGizmo( const float32* pView, const float32* pProj, uint32 operation, uint32 gizmoMode, const vector<GameObject*>& listGizmo, bool bUseSnap, const float32* pSnap )
    {
        // 헤더가 ImGuizmo 를 알 필요는 없다. 열거형은 여기서만 되돌린다.
        const ImGuizmo::OPERATION op   = static_cast<ImGuizmo::OPERATION>( operation );
        const ImGuizmo::MODE      mode = static_cast<ImGuizmo::MODE>( gizmoMode );

        if ( _bGizmoTracking == SW_FALSE )
        {
            float3 centroid{};
            for ( GameObject* pObj : listGizmo )
            {
                centroid = centroid + pObj->getPrimarySceneComponent()->getWorldPosition();
            }
            const float32 invCount = 1.0f / static_cast<float32>( listGizmo.size() );
            centroid               = float3{ centroid._x * invCount, centroid._y * invCount, centroid._z * invCount };
            float4x4 groupWorld{};
            groupWorld._41 = centroid._x;
            groupWorld._42 = centroid._y;
            groupWorld._43 = centroid._z;
            EditorViewportClientInternal::storeGizmoMatrix( _arrGizmoGroupMatrix, groupWorld );

            if ( ImGuizmo::IsOver() && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
            {
                _listGizmoObject.clear();
                _listGizmoUndo.clear();
                _listGizmoRelativeWorld.clear();
                const float4x4 invGroup = groupWorld.invert();
                for ( GameObject* pObj : listGizmo )
                {
                    _listGizmoObject.push_back( pObj->getHandle() );
                    _listGizmoUndo.push_back( EditorTransaction::captureSnapshot( pObj ) );
                    _listGizmoRelativeWorld.push_back( pObj->getPrimarySceneComponent()->getWorldMatrix() * invGroup );
                }
            }
        }

        if ( ImGuizmo::Manipulate( pView, pProj, op, mode, _arrGizmoGroupMatrix, nullptr, bUseSnap ? pSnap : nullptr ) )
        {
            float4x4 dummyWorld{};
            EditorViewportClientInternal::loadGizmoMatrix( dummyWorld, _arrGizmoGroupMatrix );
            const uint32 count = static_cast<uint32>( _listGizmoObject.size() );
            for ( uint32 objectIndex = 0; objectIndex < count; ++objectIndex )
            {
                GameObject* pObj = editor::findGameObject( _listGizmoObject[objectIndex] );
                if ( pObj == nullptr )
                    continue;
                SceneComponent* pSc = pObj->getPrimarySceneComponent();
                if ( pSc == nullptr )
                    continue;
                EditorViewportClientInternal::applyWorldMatrix( pSc, _listGizmoRelativeWorld[objectIndex] * dummyWorld );
            }
        }

        if ( ImGuizmo::IsUsing() )
            _bGizmoTracking = SW_TRUE;
        else if ( _bGizmoTracking == SW_TRUE )
            endGizmoDrag();
    }

    void EditorViewportClient::frameSelected()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        GameObject* pRaw = pContext->getEditorSelection().getPrimaryObject();
        if ( pRaw == nullptr )
            return;

        SceneComponent* pSceneComp = pRaw->getPrimarySceneComponent();
        if ( pSceneComp == nullptr )
            return;

        const float3 worldPos = pSceneComp->getWorldPosition();
        _orbitTarget          = worldPos;

        // 크기는 오브젝트의 월드 상자로 잰다(`GameObject::getWorldBox`). 메시의 **로컬** 스케일 · 콜라이더 오프셋 크기로 따로 셈하면
        // 부모가 키운 오브젝트 · 단위 상자가 아닌 메시를 너무 가깝거나 멀게 잡는다.
        float32 objectRadius = 2.0f;
        AABB    objectBox{};
        if ( pRaw->getWorldBox( objectBox ) )
        {
            const float3 diagonal{ objectBox._max._x - objectBox._min._x, objectBox._max._y - objectBox._min._y, objectBox._max._z - objectBox._min._z };
            objectRadius = MathUtil::max( diagonal.getLength() * 0.5f, 0.1f );
        }

        _orbitDistance = MathUtil::clamp( objectRadius * 2.5f, 3.0f, 60.0f );

        const float32 pitchRad = MathUtil::toRadian( _cameraRot._x );
        const float32 yawRad   = MathUtil::toRadian( _cameraRot._y );
        const float3  forward{ MathUtil::sin( yawRad ) * MathUtil::cos( pitchRad ), -MathUtil::sin( pitchRad ),
                              MathUtil::cos( yawRad ) * MathUtil::cos( pitchRad ) };

        _cameraPos = _orbitTarget - forward * _orbitDistance;
    }

    void EditorViewportClient::drawOrientationCube( ImDrawList* pDrawList, const float2& canvasPos,
                                                    const float2& canvasSize )
    {
        if ( pDrawList == nullptr )
            return;

        const float32     cubeCenterX = canvasPos._x + canvasSize._x - 45.0f;
        const float32     cubeCenterY = canvasPos._y + ( _toolbarSettings._bShowStats ? 128.0f : 45.0f );
        constexpr float32 cubeRadius  = 26.0f;

        // 원형 배경
        pDrawList->AddCircleFilled( ImVec2( cubeCenterX, cubeCenterY ), cubeRadius + 6.0f,
                                    IM_COL32( 18, 22, 30, 200 ) );
        pDrawList->AddCircle( ImVec2( cubeCenterX, cubeCenterY ), cubeRadius + 6.0f, IM_COL32( 55, 65, 85, 180 ), 0,
                              1.5f );

        const float32 pitchRad = MathUtil::toRadian( _cameraRot._x );
        const float32 yawRad   = MathUtil::toRadian( _cameraRot._y );

        const float3 forward{ MathUtil::sin( yawRad ) * MathUtil::cos( pitchRad ), -MathUtil::sin( pitchRad ),
                              MathUtil::cos( yawRad ) * MathUtil::cos( pitchRad ) };
        const float3 right{ MathUtil::cos( yawRad ), 0.0f, -MathUtil::sin( yawRad ) };
        const float3 up{ right._y * forward._z - right._z * forward._y, right._z * forward._x - right._x * forward._z,
                         right._x * forward._y - right._y * forward._x };

        struct AxisItem
        {
            float3      _dir;
            ImU32       _color;
            const utf8* _pLabel;
            float32     _depth;
            float2      _screenOffset;
            float3      _targetRot;
        };

        AxisItem arrAxis[6] = {
            { float3{ 1.0f, 0.0f, 0.0f }, EditorViewportClientInternal::_s_kColorAxisX, "X", 0.0f, float2{},
             float3{ 0.0f, -90.0f, 0.0f } },
            { float3{ -1.0f, 0.0f, 0.0f }, IM_COL32( 130, 60, 60, 200 ), "-X", 0.0f, float2{},
             float3{ 0.0f, 90.0f, 0.0f } },
            { float3{ 0.0f, 1.0f, 0.0f }, EditorViewportClientInternal::_s_kColorAxisY, "Y", 0.0f, float2{},
             float3{ 89.0f, 0.0f, 0.0f } },
            { float3{ 0.0f, -1.0f, 0.0f }, IM_COL32( 50, 130, 70, 200 ), "-Y", 0.0f, float2{},
             float3{ -89.0f, 0.0f, 0.0f } },
            { float3{ 0.0f, 0.0f, 1.0f }, EditorViewportClientInternal::_s_kColorAxisZ, "Z", 0.0f, float2{},
             float3{ 0.0f, 180.0f, 0.0f } },
            { float3{ 0.0f, 0.0f, -1.0f }, IM_COL32( 50, 70, 140, 200 ), "-Z", 0.0f, float2{},
             float3{ 0.0f, 0.0f, 0.0f } }
        };

        for ( uint32 axisIndex = 0; axisIndex < 6; ++axisIndex )
        {
            AxisItem&     ax   = arrAxis[axisIndex];
            const float32 dotR = ax._dir._x * right._x + ax._dir._y * right._y + ax._dir._z * right._z;
            const float32 dotU = ax._dir._x * up._x + ax._dir._y * up._y + ax._dir._z * up._z;
            const float32 dotF = ax._dir._x * forward._x + ax._dir._y * forward._y + ax._dir._z * forward._z;

            ax._depth        = dotF;
            ax._screenOffset = float2{ dotR * cubeRadius * 0.78f, -dotU * cubeRadius * 0.78f };
        }

        // 깊이 오름차순으로 정렬해 먼 것부터 그린다
        std::sort( std::begin( arrAxis ), std::end( arrAxis ),
                   []( const AxisItem& a, const AxisItem& b )
        { return a._depth < b._depth; } );

        const ImVec2 mousePos = ImGui::GetMousePos();

        for ( uint32 axisIndex = 0; axisIndex < 6; ++axisIndex )
        {
            const AxisItem& ax = arrAxis[axisIndex];
            const ImVec2    pt( cubeCenterX + ax._screenOffset._x, cubeCenterY + ax._screenOffset._y );

            // 중심에서 뻗는 축 선
            pDrawList->AddLine( ImVec2( cubeCenterX, cubeCenterY ), pt, ax._color, 1.8f );

            // 원판 손잡이
            const float32 handleRadius = ( ax._depth > 0.0f ) ? 6.5f : 4.5f;
            const float32 distToMouse  = float2::getDistance( float2{ mousePos.x, mousePos.y }, float2{ pt.x, pt.y } );
            const bool    bHovered     = ( distToMouse <= handleRadius + 2.0f );

            pDrawList->AddCircleFilled( pt, handleRadius, bHovered ? IM_COL32( 255, 255, 255, 255 ) : ax._color );

            if ( ax._depth > -0.2f && ax._pLabel[0] != '-' )
                pDrawList->AddText( ImVec2( pt.x - 3.5f, pt.y - 6.0f ), IM_COL32( 15, 15, 20, 255 ), ax._pLabel );

            if ( bHovered && ImGui::IsMouseClicked( 0 ) )
            {
                _cameraRot                = ax._targetRot;
                const float32 newPitchRad = MathUtil::toRadian( _cameraRot._x );
                const float32 newYawRad   = MathUtil::toRadian( _cameraRot._y );
                const float3  newForward{ MathUtil::sin( newYawRad ) * MathUtil::cos( newPitchRad ),
                                         -MathUtil::sin( newPitchRad ),
                                         MathUtil::cos( newYawRad ) * MathUtil::cos( newPitchRad ) };
                _cameraPos = _orbitTarget - newForward * _orbitDistance;
            }
        }
    }

    void EditorViewportClient::drawAdaptiveGrid( ImDrawList* pDrawList, const float2& canvasPos,
                                                 const float2& canvasSize, const float32* pView, const float32* pProj )
    {
        if ( pDrawList == nullptr || pView == nullptr || pProj == nullptr )
            return;

        float4x4 viewProj{};
        EditorViewportClientInternal::loadViewProj( pView, pProj, viewProj );

        // 선 하나를 가장자리 흐림이 보이도록 이만큼 조각으로 나눠 그린다(ImGui 선은 한 색이다).
        constexpr uint32  kSegmentCount    = 8;
        constexpr float32 kMinVisibleAlpha = 0.01f;
        constexpr float32 kMinorWidth      = 1.0f;
        constexpr float32 kMajorWidth      = 1.5f;
        constexpr float32 kMinorColor[4]   = { 60.0f, 65.0f, 80.0f, 55.0f };
        constexpr float32 kMajorColor[4]   = { 90.0f, 100.0f, 120.0f, 100.0f };

        // 격자 평면의 두 축(u, v)과 평면까지의 거리. 2D 는 XY 평면(직교 뷰 — 보이는 반 높이로 단계를 고른다), 3D 는 XZ 바닥.
        const bool    b2D        = _toolbarSettings._bIs2DMode;
        const bool    bOrtho     = MathUtil::abs( pProj[15] - 1.0f ) < 0.001f;
        const float32 centerU    = _cameraPos._x;
        const float32 centerV    = b2D ? _cameraPos._y : _cameraPos._z;
        const float32 viewHeight = bOrtho && MathUtil::abs( pProj[5] ) > 1e-6f ? 2.0f / MathUtil::abs( pProj[5] ) : ( b2D ? _cameraPos._z : _cameraPos._y );

        const EditorGridLevel level   = EditorGridUtil::selectLevel( viewHeight );
        const float32         radius  = level._radius;
        const auto            toWorld = [b2D]( float32 u, float32 v )
        { return b2D ? float3{ u, v, 0.0f } : float3{ u, 0.0f, v }; };

        EditorGridStats& stats     = EditorGridStats::get();
        stats._step                = level._step;
        stats._cameraPos           = _cameraPos;
        stats._majorLineCount      = 0;
        stats._misplacedMajorCount = 0;
        stats._frame               = ImGui::GetFrameCount();

        // bAlongV: 선이 v 방향으로 뻗는다(좌표는 u). 원점 선의 색 — 3D 는 x == 0 선이 Z 축, z == 0 선이 X 축이다. 2D 는 x == 0 선이 Y 축.
        // 이 둘의 색을 바꿔 쓰면 그리드의 축 색이 오리엔테이션 큐브 · 기즈모와 달라진다.
        const auto drawFamily = [&]( bool bAlongV )
        {
            const float32 center    = bAlongV ? centerU : centerV;
            const float32 across    = bAlongV ? centerV : centerU;
            const int64   firstIdx  = static_cast<int64>( MathUtil::ceil( ( center - radius ) / level._step ) );
            const int64   lastIdx   = static_cast<int64>( MathUtil::floor( ( center + radius ) / level._step ) );
            const ImU32   axisColor = bAlongV ? ( b2D ? EditorViewportClientInternal::_s_kColorAxisY : EditorViewportClientInternal::_s_kColorAxisZ )
                                              : EditorViewportClientInternal::_s_kColorAxisX;

            for ( int64 worldIndex = firstIdx; worldIndex <= lastIdx; ++worldIndex )
            {
                const float32 coordinate = static_cast<float32>( worldIndex ) * level._step;
                const float32 offset     = coordinate - center;
                const float32 halfChord  = MathUtil::sqrt( MathUtil::max( radius * radius - offset * offset, 0.0f ) );
                if ( halfChord <= 0.0f )
                    continue;

                const bool                bOrigin = ( worldIndex == 0 );
                const EditorGridLineStyle style   = EditorGridUtil::evaluateLine( worldIndex, level );
                if ( bOrigin == false && style._visibility < kMinVisibleAlpha )
                    continue;

                float32 arrColor[4];
                for ( uint32 channel = 0; channel < 4; ++channel )
                {
                    arrColor[channel] = MathUtil::lerp( kMinorColor[channel], kMajorColor[channel], style._majorWeight );
                }
                const float32 lineAlpha = bOrigin ? 1.0f : style._visibility;
                const float32 width     = bOrigin ? kMajorWidth : MathUtil::lerp( kMinorWidth, kMajorWidth, style._majorWeight );

                if ( bOrigin == false && style._majorWeight >= 0.5f )
                {
                    ++stats._majorLineCount;
                    const float32 majorPeriod = level._step * static_cast<float32>( EditorGridUtil::kMajorEvery );
                    const float32 phase       = MathUtil::abs( coordinate - MathUtil::round( coordinate / majorPeriod ) * majorPeriod );
                    if ( phase > 0.001f * level._step )
                        ++stats._misplacedMajorCount;
                }

                for ( uint32 segment = 0; segment < kSegmentCount; ++segment )
                {
                    const float32 from     = -halfChord + 2.0f * halfChord * static_cast<float32>( segment ) / static_cast<float32>( kSegmentCount );
                    const float32 to       = -halfChord + 2.0f * halfChord * static_cast<float32>( segment + 1 ) / static_cast<float32>( kSegmentCount );
                    const float32 middle   = 0.5f * ( from + to );
                    const float32 edgeFade = EditorGridUtil::computeEdgeFade( MathUtil::sqrt( offset * offset + middle * middle ), radius );
                    const float32 alpha    = lineAlpha * edgeFade;
                    if ( alpha < kMinVisibleAlpha )
                        continue;

                    ImU32 color = 0;
                    if ( bOrigin )
                    {
                        const uint32 axisAlpha = static_cast<uint32>( static_cast<float32>( ( axisColor >> IM_COL32_A_SHIFT ) & 0xFFu ) * alpha + 0.5f );
                        color                  = ( axisColor & ~IM_COL32_A_MASK ) | ( axisAlpha << IM_COL32_A_SHIFT );
                    }
                    else
                    {
                        color = IM_COL32( static_cast<int32>( arrColor[0] ), static_cast<int32>( arrColor[1] ), static_cast<int32>( arrColor[2] ),
                                          static_cast<int32>( arrColor[3] * alpha + 0.5f ) );
                    }

                    const float3 worldA = bAlongV ? toWorld( coordinate, across + from ) : toWorld( across + from, coordinate );
                    const float3 worldB = bAlongV ? toWorld( coordinate, across + to ) : toWorld( across + to, coordinate );
                    ImVec2       screenA, screenB;
                    if ( EditorViewportProjectionUtil::projectSegment( viewProj, worldA, worldB, canvasPos, canvasSize, screenA, screenB ) )
                        pDrawList->AddLine( screenA, screenB, color, width );
                }
            }
        };

        drawFamily( true );
        drawFamily( false );
    }

    void EditorViewportClient::processRulerTool( ImDrawList* pDrawList, const float2& canvasPos,
                                                 const float2& canvasSize, const float32* pView, const float32* pProj )
    {
        if ( pDrawList == nullptr || pView == nullptr || pProj == nullptr )
            return;

        if ( ImGui::IsKeyDown( ImGuiKey_M ) == false && _toolbarSettings._bShowRuler == false )
        {
            _bRulerActive = SW_FALSE;
            return;
        }

        float4x4 viewProj{};
        EditorViewportClientInternal::loadViewProj( pView, pProj, viewProj );

        // 마우스 아래 바닥(Y = 0)의 점이 자의 끝점이다.
        EditorPickRay mouseRay{};
        float3        groundPt{};
        if ( EditorViewportClientInternal::makeMouseRay( viewProj.invert(), canvasPos, canvasSize, mouseRay ) &&
             EditorViewportPick::rayHitsAxisPlane( mouseRay, 1, groundPt ) )
        {
            if ( ImGui::IsMouseClicked( 0 ) )
            {
                _rulerStartWorld = groundPt;
                _rulerEndWorld   = groundPt;
                _bRulerActive    = SW_TRUE;
            }
            else if ( ImGui::IsMouseDown( 0 ) && _bRulerActive == SW_TRUE )
            {
                _rulerEndWorld = groundPt;
            }
        }

        if ( _bRulerActive == SW_TRUE )
        {
            ImVec2 sStart, sEnd;
            if ( EditorViewportProjectionUtil::projectPoint( viewProj, _rulerStartWorld, canvasPos, canvasSize, sStart ) &&
                 EditorViewportProjectionUtil::projectPoint( viewProj, _rulerEndWorld, canvasPos, canvasSize, sEnd ) )
            {
                // 측정선
                pDrawList->AddLine( sStart, sEnd, IM_COL32( 255, 215, 40, 240 ), 2.5f );
                pDrawList->AddCircleFilled( sStart, 5.0f, IM_COL32( 255, 230, 80, 255 ) );
                pDrawList->AddCircleFilled( sEnd, 5.0f, IM_COL32( 255, 230, 80, 255 ) );

                const float3  delta = _rulerEndWorld - _rulerStartWorld;
                const float32 dist  = delta.getLength();

                fixed_string<constant::kMaxBuffer64> arrDistText;
                formatstring( arrDistText.data(), arrDistText.capacity(), "%# m (dX: %#, dZ: %#)",
                              Fmt( static_cast<float64>( dist ), Format().precision( 2 ) ), Fmt( static_cast<float64>( delta._x ), Format().precision( 2 ) ),
                              Fmt( static_cast<float64>( delta._z ), Format().precision( 2 ) ) );

                const ImVec2 mid( ( sStart.x + sEnd.x ) * 0.5f, ( sStart.y + sEnd.y ) * 0.5f - 16.0f );
                pDrawList->AddRectFilled( ImVec2( mid.x - 4.0f, mid.y - 2.0f ),
                                          ImVec2( mid.x + 160.0f, mid.y + 18.0f ), IM_COL32( 20, 24, 32, 220 ), 4.0f );
                pDrawList->AddText( mid, IM_COL32( 255, 230, 80, 255 ), arrDistText.c_str() );
            }
        }
    }

    void EditorViewportClient::handleViewportAssetDrop( const utf8* pAssetPath, const float2& canvasPos,
                                                        const float2& canvasSize, const float32* pView,
                                                        const float32* pProj )
    {
        if ( pAssetPath == nullptr || pView == nullptr || pProj == nullptr )
            return;
        if ( EditorUtil::areSceneEditsAllowed() == false )
            return;

        Scene* pScene = editor::getActiveScene();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;

        GameObjectManager* pManager = pScene->getObjectManager();

        // 마우스 아래의 바닥에 놓는다(2D 는 Z = 0 평면, 3D 는 Y = 0). 캔버스 밖이거나 평면과 나란하면 원점에 놓는다.
        float4x4 viewProj{};
        EditorViewportClientInternal::loadViewProj( pView, pProj, viewProj );

        float3        spawnPos{ 0.0f, 0.0f, 0.0f };
        EditorPickRay mouseRay{};
        if ( EditorViewportClientInternal::makeMouseRay( viewProj.invert(), canvasPos, canvasSize, mouseRay ) )
        {
            const uint32 planeAxis = _toolbarSettings._bIs2DMode ? 2u : 1u;
            float3       hitPt{};
            if ( EditorViewportPick::rayHitsAxisPlane( mouseRay, planeAxis, hitPt ) )
                spawnPos = hitPt;
        }

        EditorAssetCommands::dropAt( pManager, pAssetPath, spawnPos );
    }
} // namespace sw::editor
