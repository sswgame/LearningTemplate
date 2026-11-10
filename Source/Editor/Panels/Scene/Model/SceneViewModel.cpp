#include "pch.h"

#include "Editor/Panels/Scene/Model/SceneViewModel.h"

#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Config/GameTimeScale.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"

namespace sw::editor
{
    bool SceneViewModel::findCameraPosition( float3& outPosition )
    {
        const CameraComponent* pCamera = EditorCamera::find( editor::getActiveScene() );
        if ( pCamera == nullptr )
            return false;
        outPosition = pCamera->getWorldPosition();
        return true;
    }

    bool SceneViewModel::findViewMode( uint32& outMode )
    {
        const FrameRenderer* pRenderer = editor::findSubsystem<FrameRenderer>();
        if ( pRenderer == nullptr ) // 필수 서비스: 없으면 버그
        {
            SW_LOG_ERROR_ID( "SceneViewModel.ViewModeFrameRenderer", "[Scenario] view mode watch: FrameRenderer is missing" );
            return false;
        }
        outMode = static_cast<uint32>( pRenderer->getSceneViewMode() );
        return true;
    }

    int32 SceneViewModel::getGizmoOperation()
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().getGizmoOperation();
    }

    float32 SceneViewModel::getUnscaledDeltaTime()
    {
        return editor::getSubsystem<GameTimeScale>().getUnscaledDeltaTime();
    }

    void SceneViewModel::gatherDebugCategories( vector<SceneViewDebugCategoryRow>& outListRow )
    {
        outListRow.clear();
        const DebugDrawQueue& queue = editor::getSubsystem<DebugDrawQueue>();
        vector<hashed_string> listCategory;
        queue.collectCategories( listCategory );
        for ( const hashed_string& category : listCategory )
        {
            outListRow.push_back( SceneViewDebugCategoryRow{ category, queue.isCategoryEnabled( category ) } );
        }
    }

    void SceneViewModel::setDebugCategoryEnabled( const hashed_string& category, bool bEnabled )
    {
        editor::getSubsystem<DebugDrawQueue>().setCategoryEnabled( category, bEnabled );
    }
} // namespace sw::editor
