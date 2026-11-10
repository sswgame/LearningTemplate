#include "pch.h"

#include "Engine/Dialogue/DialogueCursor.h"

#include "Core/String/StringBuilder.h"

namespace sw
{
    optional<int32> DialogueCursor::step( const DialogueGraphAsset& asset, const DialogueAssetNode& node, const DialogueStepInput& input )
    {
        const DialogueNodeInfo* pInfo = DialogueGraphAsset::findNodeInfo( node._type );
        if ( pInfo == nullptr )
            return nullopt;

        switch ( pInfo->_output )
        {
            case DialogueNodeOutput::None:
            {
                return nullopt;
            }
            case DialogueNodeOutput::Next:
            {
                return asset.findDefaultNextNodeID( node._id );
            }
            case DialogueNodeOutput::Branch:
            {
                const optional<int32> branchNextID = asset.findBranchNextNodeID( node._id, input._bConditionMet );
                return branchNextID.has_value() ? branchNextID : asset.findDefaultNextNodeID( node._id );
            }
            case DialogueNodeOutput::Choice:
            {
                return input._choiceIndex.has_value() ? asset.findChoiceNextNodeID( node._id, *input._choiceIndex ) : asset.findDefaultNextNodeID( node._id );
            }
        }
        return nullopt;
    }

    DialogueAssetNode DialogueCursor::makeNode( DialogueAssetNodeKind type, int32 nodeID )
    {
        DialogueAssetNode node{};
        node._id   = nodeID;
        node._type = type;

        const DialogueNodeInfo* pInfo = DialogueGraphAsset::findNodeInfo( type );
        if ( pInfo == nullptr )
            return node;

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
            StringBuilder<constant::buffer::kMax32> choiceBuilder;
            choiceBuilder.append( "Option " );
            choiceBuilder.append( choiceIndex + 1 );
            node._listChoice.emplace_back( choiceBuilder.c_str() );
        }
        return node;
    }
} // namespace sw
