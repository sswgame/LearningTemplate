#include "pch.h"

#include "Editor/Panels/Animation/Model/AnimationGraphEdit.h"

#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Animation/Graph/AnimationGraphAsset.h"
#include "Engine/Animation/Skeletal/AnimationPoseNode.h"

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

    vector<hashed_string> AnimationGraphEdit::getPoseNodeTypeNames()
    {
        return getSubsystem<AnimationPoseNodeRegistry>().getTypeNames();
    }

    bool AnimationGraphEdit::makeDefaultPoseNode( const hashed_string& typeName, AnimationGraphPoseNodeDef& outDef )
    {
        return getSubsystem<AnimationPoseNodeRegistry>().makeDefaultDef( typeName, outDef );
    }

    const AnimationPoseNodeType* AnimationGraphEdit::findPoseNodeType( const hashed_string& typeName )
    {
        return getSubsystem<AnimationPoseNodeRegistry>().findType( typeName );
    }
} // namespace sw::editor
