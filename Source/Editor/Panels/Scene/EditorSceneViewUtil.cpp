#include "pch.h"

#include "Editor/Panels/Scene/EditorSceneViewUtil.h"

#include "Core/Math/VectorMath.h"

#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Panels/Scene/SceneViewPanel.h"

namespace sw::editor
{
    bool EditorSceneViewUtil::focusOn( const float3& target, float32 radius )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return false;
        SceneViewPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<SceneViewPanel>();
        if ( pPanel == nullptr )
            return false;
        pPanel->getViewportClient().focusOnPoint( target, radius );
        return true;
    }
} // namespace sw::editor
