#include "pch.h"

#include "Editor/Panels/Scene/Model/SceneViewModel.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Config/GameTimeScale.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"

namespace sw::editor
{
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
