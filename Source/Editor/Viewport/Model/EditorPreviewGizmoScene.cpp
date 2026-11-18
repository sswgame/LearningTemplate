// 미리보기 기즈모의 Model 쪽(ImGui 없음) — 창 선택의 대상 모으기, 기즈모 명령 적용, 클릭 피킹을 창 선택에 놓기. 그리기 · 입력은 `EditorPreviewGizmo.cpp`(View)다.
#include "pch.h"

#include "Core/Math/Ray.h"

#include "Editor/Common/GUI/Panel/EditorPreviewPanel.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Viewport/EditorPreviewGizmo.h"
#include "Editor/Viewport/Model/EditorViewportRayPick.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    bool EditorPreviewGizmo::gatherTargets( EditorPreviewPanel& panel )
    {
        EditorSelection& selection = panel.getPreviewSelection();
        Scene*           pScene    = panel.getPreviewViewport().getScene();
        if ( selection.isPreviewScope() == false || pScene == nullptr || pScene->getObjectManager() == nullptr )
            return false;
        pScene->getObjectManager()->flushSceneTransforms(); // 기즈모가 읽는 월드 행렬을 이번 프레임 값으로
        EditorGizmoEdit::gather( selection, _state );
        return true;
    }

    void EditorPreviewGizmo::applyCommands( EditorPreviewPanel& panel )
    {
        _edit.apply( panel.getPreviewSelection(), _gizmo.getCommands() );
    }

    void EditorPreviewGizmo::pick( EditorPreviewPanel& panel, const Ray& ray, bool bToggle )
    {
        // 메인 뷰포트의 피킹(`EditorViewportPicker`)은 메인 선택에 놓는다. 창은 자기 씬 · 자기 선택에만 놓는다(언리얼 애셋 에디터 뷰포트는 툴킷의 선택에 놓는다).
        EditorSelection& selection = panel.getPreviewSelection();
        Scene*           pScene    = selection.isPreviewScope() ? selection.getScene() : nullptr;
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;
        GameObjectManager* pManager = pScene->getObjectManager();
        pManager->flushSceneTransforms(); // 레이가 읽는 월드 경계를 이번 프레임 값으로
        const SelectionMode mode = bToggle ? SelectionMode::Toggle : SelectionMode::Replace;
        EditorPickResult    result{};
        const bool          bHit = EditorViewportRayPick::pick( pManager, ray, false, result ) && result._pObject != nullptr && result._pObject->isRenderHidden() == false;
        if ( bHit )
            selection.selectObject( result._pObject, mode );
        else if ( mode == SelectionMode::Replace )
            selection.clearObjectSelection();
    }
} // namespace sw::editor
