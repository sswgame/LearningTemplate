#include "pch.h"

#include "Editor/Common/Workspace/Asset/EditorAssetTypeActions.h"

#include "Core/Log/Logger.h"

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

    void IEditorAssetTypeActions::placeInViewport( GameObjectManager* /*pManager*/, const utf8* pPath, const EditorPlacementPose& /*pose*/ ) const
    {
        SW_LOG_ERROR_ID( "EditorAssetTypeActions.PlaceNotImplemented", "'%#': the asset type says it can be placed in the viewport but does not implement placeInViewport - nothing was placed",
                         pPath );
    }
} // namespace sw::editor
