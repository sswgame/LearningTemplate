#include "pch.h"

#include "Engine/Dialogue/DialogueCursor.h"

#include "Core/String/StringBuilder.h"

namespace sw
{
    int32 DialogueCursor::step( const DialogueGraphAsset& asset, const DialogueAssetNode& node, const DialogueStepInput& input )
    {
        const DialogueNodeTraits* pTraits = DialogueGraphAsset::findNodeTraits( node._type );
        if ( pTraits == nullptr )
            return 0;

        switch ( pTraits->_output )
        {
            case DialogueNodeOutput::None:
            {
                return 0;
            }
            case DialogueNodeOutput::Next:
            {
                return asset.findDefaultNextNodeId( node._id );
            }
            case DialogueNodeOutput::Branch:
            {
                const int32 branchNextId = asset.findBranchNextNodeId( node._id, input._bConditionMet );
                return branchNextId > 0 ? branchNextId : asset.findDefaultNextNodeId( node._id );
            }
            case DialogueNodeOutput::Choice:
            {
                return asset.findChoiceNextNodeId( node._id, input._choiceIndex );
            }
        }
        return 0;
    }

    DialogueAssetNode DialogueCursor::makeNode( DialogueAssetNodeType type, int32 nodeId )
    {
        DialogueAssetNode node{};
        node._id   = nodeId;
        node._type = type;

        const DialogueNodeTraits* pTraits = DialogueGraphAsset::findNodeTraits( type );
        if ( pTraits == nullptr )
            return node;

        node._speaker = pTraits->_pDefaultSpeaker;
        switch ( pTraits->_body )
        {
            case DialogueNodeBody::Text:
            {
                node._text = pTraits->_pDefaultBody;
                break;
            }
            case DialogueNodeBody::Condition:
            {
                node._condition = pTraits->_pDefaultBody;
                break;
            }
            case DialogueNodeBody::Action:
            {
                node._actionCommand = pTraits->_pDefaultBody;
                break;
            }
            case DialogueNodeBody::None:
            {
                break;
            }
        }

        node._listChoice.reserve( pTraits->_defaultChoiceCount );
        for ( uint32 choiceIndex = 0; choiceIndex < pTraits->_defaultChoiceCount; ++choiceIndex )
        {
            StringBuilder<constant::kMaxBuffer32> choiceBuilder;
            choiceBuilder.append( "Option " );
            choiceBuilder.append( choiceIndex + 1 );
            node._listChoice.emplace_back( choiceBuilder.c_str() );
        }
        return node;
    }
} // namespace sw
