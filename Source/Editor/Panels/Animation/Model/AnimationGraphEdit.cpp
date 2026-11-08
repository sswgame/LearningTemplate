#include "pch.h"

#include "Editor/Panels/Animation/Model/AnimationGraphEdit.h"

#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"

namespace sw::editor
{
    ToolAssetLoadResult AnimationGraphEdit::loadGraph( AnimationGraphAsset& outGraph, const string_view path )
    {
        return EditorToolAssetCommands::loadAnimationGraph( outGraph, path );
    }

    bool AnimationGraphEdit::saveGraph( const AnimationGraphAsset& graph, const string_view path )
    {
        return EditorToolAssetCommands::saveAnimationGraph( graph, path );
    }
} // namespace sw::editor
