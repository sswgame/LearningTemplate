#include "pch.h"

#include "Editor/Viewport/Model/EditorPreviewPick.h"

#include "Editor/Viewport/Model/EditorViewportRayPick.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    bool EditorPreviewPickUtil::pickIntoSelection( EditorSelection& selection, const EditorPickRay& ray, SelectionMode mode )
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
