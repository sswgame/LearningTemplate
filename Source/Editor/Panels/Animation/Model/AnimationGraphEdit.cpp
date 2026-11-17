#include "pch.h"

#include "Editor/Panels/Animation/Model/AnimationGraphEdit.h"

#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"
#include "Editor/Common/GUI/Panel/Model/EditorGraphEditSession.h"
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

    void AnimationGraphEdit::commitLayerEdit( const EditorGraphEditSession& session, const utf8* pLayerEditLabel )
    {
        session.recordDocumentEdit( pLayerEditLabel );
    }

    void AnimationGraphEdit::commitNodeAdded( const EditorGraphEditSession& session )
    {
        session.recordDocumentEdit( "Add Animation Graph Node" );
    }

    void AnimationGraphEdit::addLink( AnimationGraphAsset& inoutGraph, const EditorGraphEditSession& session, const AnimationGraphLink& link )
    {
        inoutGraph._listLink.push_back( link );
        session.recordDocumentEdit( "Link Animation Graph Nodes" );
    }

    void AnimationGraphEdit::renameState( string& inoutStateName, const EditorGraphEditSession& session, string_view name )
    {
        inoutStateName = string{ name };
        session.recordDocumentEdit( "Rename Animation State" );
    }

    void AnimationGraphEdit::setStateLoop( optional<bool>& inoutLoop, const EditorGraphEditSession& session, const optional<bool>& loop )
    {
        inoutLoop = loop;
        session.recordDocumentEdit( "Edit Animation State" );
    }

    void AnimationGraphEdit::setPoseNode( AnimationGraphPoseNodeDef& inoutPoseNode, const EditorGraphEditSession& session, AnimationGraphPoseNodeDef&& edited )
    {
        inoutPoseNode = std::move( edited );
        session.recordDocumentEdit( "Edit Animation Pose Node" );
    }

    void AnimationGraphEdit::commitTransitionEdit( const EditorGraphEditSession& session )
    {
        session.recordDocumentEdit( "Edit Animation Transition" );
    }
} // namespace sw::editor
