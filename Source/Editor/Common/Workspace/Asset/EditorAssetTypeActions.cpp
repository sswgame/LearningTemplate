#include "pch.h"

#include "Editor/Common/Workspace/Asset/EditorAssetTypeActions.h"

namespace sw::editor
{
    bool IEditorAssetTypeActions::drawThumbnail( ImDrawList* /*pDrawList*/, const float2& /*minPos*/, const float2& /*maxPos*/ ) const
    {
        return false;
    }

    bool IEditorAssetTypeActions::open( string_view /*relativePath*/ ) const
    {
        return false;
    }

    bool IEditorAssetTypeActions::dropInViewport( GameObjectManager* /*pManager*/, const utf8* /*pPath*/, const float3& /*spawnPos*/ ) const
    {
        return false;
    }
} // namespace sw::editor
