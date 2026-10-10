#include "pch.h"

#include "Editor/Viewport/EditorSceneViewUtil.h"

#include "Core/Math/VectorMath.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Panels/SceneViewPanel.h"

namespace sw::editor
{
    bool EditorSceneViewUtil::focusOn( const float3& target, float32 radius )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return false;
        SceneViewPanel* pPanel = static_cast<SceneViewPanel*>( pContext->getPanelManager().findPanel( "scene_view" ) );
        if ( pPanel == nullptr )
            return false;
        pPanel->getViewportClient().focusOnPoint( target, radius );
        return true;
    }
} // namespace sw::editor
