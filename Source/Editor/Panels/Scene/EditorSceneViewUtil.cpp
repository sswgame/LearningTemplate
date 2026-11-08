#include "pch.h"

#include "Editor/Panels/Scene/EditorSceneViewUtil.h"

#include "Core/Math/VectorMath.h"

#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Panels/Scene/SceneViewPanel.h"

namespace sw::editor
{
    bool EditorSceneViewUtil::focusOn( const float3& target, float32 radius )
    {
        SceneViewPanel* pPanel = EditorPanelManager::getInEditorContext().findPanel<SceneViewPanel>();
        if ( pPanel == nullptr )
            return false;
        pPanel->getViewportClient().focusOnPoint( target, radius );
        return true;
    }
} // namespace sw::editor
