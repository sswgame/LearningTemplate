#include "pch.h"

#include "Editor/Viewport/EditorPreviewGizmo.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/EditorPreviewPanel.h"
#include "Editor/Viewport/EditorPreviewPick.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportRayPick.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

#include <imgui.h>
#include <ImGuizmo.h>

namespace sw::editor
{
    EditorPreviewGizmo::EditorPreviewGizmo()
        : _gizmo{}
        , _settings{}
    {
    }

    void EditorPreviewGizmo::draw( EditorPreviewPanel& panel )
    {
        EditorSelection&             selection = panel.getPreviewSelection();
        const EditorPreviewViewport& viewport  = panel.getPreviewViewport();
        const float2&                imageSize = viewport.getImageSize();
        Scene*                       pScene    = viewport.getScene();
        const bool                   bNoTarget = selection.isPreviewScope() == false || pScene == nullptr || imageSize._x <= 1.0f;
        if ( bNoTarget )
            return;

        float4x4 view{};
        float4x4 projection{};
        float3   eye{};
        viewport.computeViewAndProjection( imageSize._x / MathUtil::max( 1.0f, imageSize._y ), view, projection, eye );
        float32 arrView[16];
        float32 arrProj[16];
        EditorViewportProjectionUtil::storeGizmoMatrix( arrView, view );
        EditorViewportProjectionUtil::storeGizmoMatrix( arrProj, projection );
        pScene->getObjectManager()->flushSceneTransforms(); // 기즈모가 읽는 월드 행렬을 이번 프레임 값으로
        _gizmo.draw( selection, arrView, arrProj, viewport.getImagePosition(), imageSize, _settings );

        // 클릭 피킹 — 왼쪽 끌기는 오빗이라 끌지 않고 놓은 클릭만 집는다. 기즈모 위의 클릭은 기즈모의 것이다.
        const ImGuiIO& io             = ImGui::GetIO();
        const float2&  imagePos       = viewport.getImagePosition();
        const bool     bInsideImage   = imagePos._x <= io.MousePos.x && io.MousePos.x < imagePos._x + imageSize._x && imagePos._y <= io.MousePos.y && io.MousePos.y < imagePos._y + imageSize._y;
        const bool     bDragged       = io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] > io.MouseDragThreshold * io.MouseDragThreshold;
        const bool     bOverGizmo     = hasMouse() || ImGuizmo::IsOver() || ImGuizmo::IsUsing();
        const bool     bClickToPick   = ImGui::IsMouseReleased( ImGuiMouseButton_Left ) && bDragged == false && bOverGizmo == false;
        const bool     bPickThisClick = bClickToPick && bInsideImage && ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows );
        if ( bPickThisClick == false )
            return;
        const float4x4 invViewProj = ( view * projection ).invert();
        EditorPickRay  ray{};
        if ( EditorViewportProjectionUtil::makeMouseRay( invViewProj, imagePos, imageSize, ray ) )
            (void)EditorPreviewPickUtil::pickIntoSelection( selection, ray, io.KeyCtrl ? SelectionMode::Toggle : SelectionMode::Replace ); // 빈 곳이면 비운다 — 결과는 선택에 남는다
    }
} // namespace sw::editor
