#include "pch.h"

#include "Editor/Viewport/EditorViewportPicker.h"

#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Commands/EditorViewportPick.h"
#include "Editor/Common/EditorProfile.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Viewport/EditorViewportBillboard.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportToolbar.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

#include <imgui.h>
#include <ImGuizmo.h>

namespace sw::editor
{
    namespace
    {
        struct EditorViewportPickerInternal
        {
            static constexpr float64 kSlowHoverPickSeconds  = 0.002; ///< 이보다 오래 걸린 호버 피킹은 비싸다고 판단한다
            static constexpr float64 kSlowHoverPickInterval = 0.1;   ///< 비싸면 이만큼 간격을 둔다(초)
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorViewportPicker::EditorViewportPicker()
        : _hoveredObjectID{ 0 }
        , _lastHoverPickSeconds{ 0.0 }
        , _lastHoverPickTime{ 0.0 }
        , _hoverMousePos{ -1.0f, -1.0f }
    {
    }

    void EditorViewportPicker::processClick( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera, bool bCanvasClicked,
                                             const ViewportToolbarSettings& settings )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr || pCamera == nullptr )
            return;
        if ( bCanvasClicked == false )
            return;
        if ( ImGuizmo::IsOver() || ImGuizmo::IsUsing() )
            return;
        if ( ImGui::GetIO().KeyAlt )
            return;
        GameObject* pObject    = nullptr;
        Component*  pComponent = nullptr;
        if ( findObjectUnderMouse( canvasPos, canvasSize, pCamera, settings, pObject, pComponent ) )
            pContext->getWorkspace().selectComponent( pObject, pComponent );
        else
            pContext->getWorkspace().clearSelection();
    }

    void EditorViewportPicker::processHover( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera, bool bCanvasHovered, bool bGizmoDragging,
                                             const ViewportToolbarSettings& settings )
    {
        // 호버는 마우스가 씬 뷰 위에만 있고(다른 패널이나 팝업이 마우스를 가지고 있지 않고) 드래그, 비행, 궤도, 기즈모 위가 아닐 때만 처리한다(언리얼과 유니티의 호버 강조).
        const ImGuiIO& io       = ImGui::GetIO();
        const bool     bBlocked = bCanvasHovered == false || pCamera == nullptr || io.KeyAlt || io.MouseDown[0] || io.MouseDown[1] || io.MouseDown[2] ||
                              ImGui::IsAnyItemActive() || ImGuizmo::IsOver() || ImGuizmo::IsUsing() || bGizmoDragging;
        if ( bBlocked )
        {
            _hoveredObjectID = 0;
            _hoverMousePos   = float2{ -1.0f, -1.0f };
            return;
        }
        // 마우스가 그대로면 지난 결과를 쓴다. 프레임당 많아야 한 번이고 대개는 0번이다. 카메라를 움직이는 동안은 위에서 막힌다.
        const float2 mouse{ io.MousePos.x, io.MousePos.y };
        if ( mouse._x == _hoverMousePos._x && mouse._y == _hoverMousePos._y )
            return;
        // 피킹이 비싼 씬은 마우스가 움직이는 동안 0.1초에 한 번만 찾는다. 레이 피킹은 오브젝트 수에 비례하며, 8000개이면 Debug 에서 약 20 ms 가 걸린다.
        // 이때 마우스 위치를 기록하지 않고 반환하므로, 마우스가 멈춘 뒤 다음 기회에 마지막 위치로 다시 찾는다.
        const float64 now = ImGui::GetTime();
        if ( _lastHoverPickSeconds > EditorViewportPickerInternal::kSlowHoverPickSeconds && now - _lastHoverPickTime < EditorViewportPickerInternal::kSlowHoverPickInterval )
            return;
        _hoverMousePos = mouse;
        SW_EDITOR_PROFILE_SCOPE( "GT.Editor.hoverPick" );
        const int64 startNanos = MonotonicClock::nowNanoseconds();
        GameObject* pObject    = nullptr;
        Component*  pComponent = nullptr;
        _hoveredObjectID       = findObjectUnderMouse( canvasPos, canvasSize, pCamera, settings, pObject, pComponent ) ? pObject->getObjectID() : 0;
        // 이미 선택한 오브젝트는 호버로 취급하지 않는다. 선택 상자와 기즈모가 이미 있다. 그 위의 기즈모 핸들과 겹치는 판정도 여기서 없어진다.
        EditorContext* pContext = EditorContext::get();
        if ( pObject != nullptr && pContext != nullptr && pContext->getEditorSelection().hasObject( pObject ) )
            _hoveredObjectID = 0;
        _lastHoverPickSeconds = static_cast<float64>( MonotonicClock::nowNanoseconds() - startNanos ) * 1e-9;
        _lastHoverPickTime    = now;
    }

    bool EditorViewportPicker::findObjectUnderMouse( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera, const ViewportToolbarSettings& settings,
                                                     GameObject*& pOutObject, Component*& pOutComponent ) const
    {
        pOutObject              = nullptr;
        pOutComponent           = nullptr;
        EditorContext* pContext = EditorContext::get();
        Scene*         pScene   = editor::getActiveScene();
        if ( pContext == nullptr || pCamera == nullptr || pScene == nullptr || pScene->getObjectManager() == nullptr )
            return false;
        const float32  aspect      = canvasSize._x / ( canvasSize._y > 0.0f ? canvasSize._y : 1.0f );
        const float4x4 viewProj    = pCamera->getViewProjectionMatrix( aspect );
        const float4x4 invViewProj = viewProj.invert();
        EditorPickRay  pickRay{};
        if ( EditorViewportProjectionUtil::makeMouseRay( invViewProj, canvasPos, canvasSize, pickRay ) == false )
            return false;
        GameObjectManager* pManager = pScene->getObjectManager();
        pManager->flushSceneTransforms();

        // 빌보드(라이트, 카메라, 오디오 아이콘)를 레이 피킹보다 먼저 검사한다. 메시가 없어 레이로는 집히지 않기 때문이다(언리얼 빌보드, 유니티 Gizmo 아이콘 클릭).
        // 잠근 오브젝트의 빌보드가 맞으면 그 위치는 "빈 곳" 으로 처리한다. 뒤의 레이 피킹으로 넘기지 않는다.
        const EditorVisualizerRegistration* pBillboard = EditorRegistry<EditorVisualizerRegistration>::find( EditorViewportBillboard::kVisualizerID );
        if ( pBillboard != nullptr && settings._visualizerToggles.isOn( *pBillboard ) )
        {
            vector<EditorViewportBillboardItem> listItem;
            EditorViewportBillboard::collect( *pManager, viewProj, canvasPos, canvasSize, pCamera, listItem );
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const uint32 index = EditorViewportBillboard::findAt( listItem, float2{ mouse.x, mouse.y } );
            if ( index != invalid_index::kUint32 )
            {
                if ( pContext->getWorkspace().isObjectLocked( listItem[index]._pObject->getObjectID() ) )
                    return false;
                pOutObject    = listItem[index]._pObject;
                pOutComponent = listItem[index]._pComponent;
                return true;
            }
        }

        // 어떤 컴포넌트 종류를 집을 수 있는지는 EditorViewportPick 의 테이블이 정한다. ImGui 없이 테스트한다.
        EditorPickResult pickResult{};
        if ( EditorViewportPick::pick( pManager, pickRay, settings._bIs2DMode, pickResult ) == false || pickResult._pObject == nullptr ||
             pContext->getWorkspace().isObjectLocked( pickResult._pObject->getObjectID() ) || pickResult._pObject->isHiddenInEditor() )
            return false;
        pOutObject    = pickResult._pObject;
        pOutComponent = pickResult._pComponent;
        return true;
    }
} // namespace sw::editor
