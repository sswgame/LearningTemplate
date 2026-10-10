#include "pch.h"

#include "Engine/Dialogue/DialogueCursor.h"

#include "Core/String/StringBuilder.h"

namespace sw
{
    int32 DialogueCursor::step( const DialogueGraphAsset& asset, const DialogueAssetNode& node, const DialogueStepInput& input )
    {
        const DialogueNodeInfo* pInfo = DialogueGraphAsset::findNodeInfo( node._type );
        if ( pInfo == nullptr )
            return 0;

        switch ( pInfo->_output )
        {
            case DialogueNodeOutput::None:
            {
                return 0;
            }
            case DialogueNodeOutput::Next:
            {
                return asset.findDefaultNextNodeID( node._id );
            }
            case DialogueNodeOutput::Branch:
            {
                const int32 branchNextID = asset.findBranchNextNodeID( node._id, input._bConditionMet );
                return branchNextID > 0 ? branchNextID : asset.findDefaultNextNodeID( node._id );
            }
            case DialogueNodeOutput::Choice:
            {
                return asset.findChoiceNextNodeID( node._id, input._choiceIndex );
            }
        }
        return 0;
    }

    DialogueAssetNode DialogueCursor::makeNode( DialogueAssetNodeType type, int32 nodeID )
    {
        DialogueAssetNode node{};
        node._id   = nodeID;
        node._type = type;

        const DialogueNodeInfo* pInfo = DialogueGraphAsset::findNodeInfo( type );
        if ( pInfo == nullptr )
            return node;

        node._speaker = pInfo->_pDefaultSpeaker;
        switch ( pInfo->_body )
        {
            case DialogueNodeBody::Text:
            {
                node._text = pInfo->_pDefaultBody;
                break;
            }
            case DialogueNodeBody::Condition:
            {
                node._condition = pInfo->_pDefaultBody;
                break;
            }
            case DialogueNodeBody::Action:
            {
                node._actionCommand = pInfo->_pDefaultBody;
                break;
            }
            case DialogueNodeBody::None:
            {
                break;
            }
        }

        node._listChoice.reserve( pInfo->_defaultChoiceCount );
        for ( uint32 choiceIndex = 0; choiceIndex < pInfo->_defaultChoiceCount; ++choiceIndex )
        {
            StringBuilder<constant::kMaxBuffer32> choiceBuilder;
            choiceBuilder.append( "Option " );
            choiceBuilder.append( choiceIndex + 1 );
            node._listChoice.emplace_back( choiceBuilder.c_str() );
        }
        return node;
    }
} // namespace sw
