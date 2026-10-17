#include "pch.h"

#include "Editor/Viewport/EditorPreviewGizmo.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/EditorPreviewPanel.h"
#include "Editor/Viewport/EditorViewportProjection.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

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
    }
} // namespace sw::editor
