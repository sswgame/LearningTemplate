#include "pch.h"

#include "Editor/Viewport/EditorPreviewGizmo.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/Ray.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/GUI/Panel/EditorPreviewPanel.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/Model/EditorViewportRayPick.h"

#include "Engine/Input/RawInputEvent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

#include <imgui.h>

// EditorPreviewPick — 이 파일만 쓴다(한 파일 전용 도우미는 따로 헤더를 두지 않는다)
namespace sw::editor
{

    /**
     * @struct EditorPreviewPickUtil
     * @brief 미리보기 씬의 레이 피킹을 창 선택으로 잇습니다.
     * @details 메인 뷰포트의 피킹(`EditorViewportPicker`)은 메인 선택에 놓는다. 미리보기 창의 클릭이 그 길을 쓰면 활성 씬을 집어 메인 선택을 고치므로,
     *          창은 이것으로 자기 씬 · 자기 선택에만 놓는다(언리얼 애셋 에디터 뷰포트는 툴킷의 선택에 놓는다).
     */
    struct EditorPreviewPickUtil
    {
        /**
         * @brief 창 선택 @p selection 의 씬에서 @p ray 가 처음 맞는 오브젝트를 그 선택에 @p mode 로 놓습니다. 맞은 것이 없고 @p mode 가 Replace 면 선택을 비웁니다.
         * @return 오브젝트를 맞혔으면 true 입니다. 미리보기 씬에 묶이지 않은 선택(메인)이면 아무것도 하지 않고 false 입니다.
         */
        static bool pickIntoSelection( EditorSelection& selection, const Ray& ray, SelectionMode mode );
    };
} // namespace sw::editor

namespace sw::editor
{
    bool EditorPreviewPickUtil::pickIntoSelection( EditorSelection& selection, const Ray& ray, SelectionMode mode )
    {
        Scene* pScene = selection.isPreviewScope() ? selection.getScene() : nullptr;
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return false;
        GameObjectManager* pManager = pScene->getObjectManager();
        pManager->flushSceneTransforms(); // 레이가 읽는 월드 경계를 이번 프레임 값으로

        EditorPickResult result{};
        const bool       bHit = EditorViewportRayPick::pick( pManager, ray, false, result ) && result._pObject != nullptr && result._pObject->isRenderHidden() == false;
        if ( bHit )
        {
            selection.selectObject( result._pObject, mode );
            return true;
        }
        if ( mode == SelectionMode::Replace )
            selection.clearObjectSelection();
        return false;
    }
} // namespace sw::editor

namespace sw::editor
{
    EditorPreviewGizmo::EditorPreviewGizmo()
        : _gizmo{}
        , _settings{}
    {
    }

    EditorPreviewGizmo::~EditorPreviewGizmo() = default;

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
        const EditorPointerState pointer        = EditorInputGUI::readPointer();
        const float2&            imagePos       = viewport.getImagePosition();
        const float2&            mouse          = pointer._position;
        const bool               bInsideImage   = imagePos._x <= mouse._x && mouse._x < imagePos._x + imageSize._x && imagePos._y <= mouse._y && mouse._y < imagePos._y + imageSize._y;
        const bool               bOverGizmo     = hasMouse();
        const bool               bClickToPick   = pointer.wasClickedWithoutDrag( EditorMouseButton::Left ) && bOverGizmo == false; // 끌었으면 오빗이었다(입력 맵의 끌기 문턱)
        const bool               bPickThisClick = bClickToPick && bInsideImage && ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows );
        if ( bPickThisClick == false )
            return;
        const float4x4 invViewProj = ( view * projection ).invert();
        Ray            ray{};
        if ( EditorViewportProjectionUtil::makeMouseRay( invViewProj, imagePos, imageSize, ray ) )
            (void)EditorPreviewPickUtil::pickIntoSelection( selection, ray, pointer.hasModifier( ModifierKey::Ctrl ) ? SelectionMode::Toggle : SelectionMode::Replace ); // 빈 곳이면 비운다 — 결과는 선택에 남는다
    }
} // namespace sw::editor
