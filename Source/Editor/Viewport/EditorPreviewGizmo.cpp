#include "pch.h"

#include "Editor/Viewport/EditorPreviewGizmo.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/Ray.h"

#include "Editor/Common/GUI/Panel/EditorPreviewPanel.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Viewport/EditorViewportProjection.h"

#include "Engine/Input/RawInputEvent.h"

#include <imgui.h>

namespace sw::editor
{
    EditorPreviewGizmo::EditorPreviewGizmo()
        : _gizmo{}
        , _edit{}
        , _state{}
        , _settings{}
    {
    }

    EditorPreviewGizmo::~EditorPreviewGizmo() = default;

    void EditorPreviewGizmo::draw( EditorPreviewPanel& panel )
    {
        const EditorPreviewViewport& viewport  = panel.getPreviewViewport();
        const float2&                imageSize = viewport.getImageSize();
        if ( imageSize._x <= 1.0f || gatherTargets( panel ) == false )
            return;

        float4x4 view{};
        float4x4 projection{};
        float3   eye{};
        viewport.computeViewAndProjection( imageSize._x / MathUtil::max( 1.0f, imageSize._y ), view, projection, eye );
        float32 arrView[16];
        float32 arrProj[16];
        EditorViewportProjectionUtil::storeGizmoMatrix( arrView, view );
        EditorViewportProjectionUtil::storeGizmoMatrix( arrProj, projection );
        _gizmo.draw( _state, arrView, arrProj, viewport.getImagePosition(), imageSize, _settings, nullptr ); // 미리보기 창은 여럿이라 마크 이름이 겹친다 — 가운데 마크는 씬 뷰만
        applyCommands( panel );

        // 클릭 피킹 — 왼쪽 끌기는 오빗이라 끌지 않고 놓은 클릭만 집는다. 기즈모 위의 클릭은 기즈모의 것이다.
        const EditorPointerState pointer        = EditorInputGUI::readPointer();
        const float2&            imagePos       = viewport.getImagePosition();
        const float2&            mouse          = pointer._position;
        const bool               bInsideImage   = imagePos._x <= mouse._x && mouse._x < imagePos._x + imageSize._x && imagePos._y <= mouse._y && mouse._y < imagePos._y + imageSize._y;
        const bool               bClickToPick   = pointer.wasClickedWithoutDrag( EditorMouseButton::Left ) && hasMouse() == false; // 끌었으면 오빗이었다(입력 맵의 끌기 문턱)
        const bool               bPickThisClick = bClickToPick && bInsideImage && ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows );
        if ( bPickThisClick == false )
            return;
        const float4x4 invViewProj = ( view * projection ).invert();
        Ray            ray{};
        if ( EditorViewportProjectionUtil::makeMouseRay( invViewProj, imagePos, imageSize, ray ) )
            pick( panel, ray, pointer.hasModifier( ModifierKey::Ctrl ) ); // 빈 곳이면 비운다 — 결과는 선택에 남는다
    }
} // namespace sw::editor
