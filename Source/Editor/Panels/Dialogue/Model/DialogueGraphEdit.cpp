#include "pch.h"

#include "Editor/Panels/Dialogue/Model/DialogueGraphEdit.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Dialogue/DialogueCursor.h"

namespace sw::editor
{
    void DialogueGraphEdit::makeDefaultGraph( DialogueGraphAsset& outGraph )
    {
        outGraph._listNode.clear();
        outGraph._listLink.clear();

        DialogueAssetNode startNode{};
        startNode._id          = 1;
        startNode._type        = DialogueAssetNodeKind::Start;
        startNode._position._x = 50.0f;
        startNode._position._y = 100.0f;
        outGraph._listNode.push_back( startNode );

        DialogueAssetNode dialogueNode{};
        dialogueNode._id          = 2;
        dialogueNode._type        = DialogueAssetNodeKind::Dialogue;
        dialogueNode._speaker     = "Elder";
        dialogueNode._text        = "Greetings adventurer! The ancient ruins ahead are full of peril.";
        dialogueNode._position._x = 250.0f;
        dialogueNode._position._y = 100.0f;
        outGraph._listNode.push_back( dialogueNode );

        DialogueAssetNode choiceNode{};
        choiceNode._id          = 3;
        choiceNode._type        = DialogueAssetNodeKind::Choice;
        choiceNode._text        = "How do you respond?";
        choiceNode._listChoice  = { "I am ready for any challenge!", "Could you give me some supplies first?" };
        choiceNode._position._x = 650.0f;
        choiceNode._position._y = 100.0f;
        outGraph._listNode.push_back( choiceNode );

        DialogueAssetNode actionNode{};
        actionNode._id            = 4;
        actionNode._type          = DialogueAssetNodeKind::Action;
        actionNode._actionCommand = "event(give_healing_potion)";
        actionNode._position._x   = 1050.0f;
        actionNode._position._y   = 220.0f;
        outGraph._listNode.push_back( actionNode );

        DialogueAssetNode endNode{};
        endNode._id          = 5;
        endNode._type        = DialogueAssetNodeKind::End;
        endNode._position._x = 1350.0f;
        endNode._position._y = 120.0f;
        outGraph._listNode.push_back( endNode );

        const auto pinIn = []( int32 nodeID )
        { return DialogueGraphAsset::encodePin( nodeID, DialogueGraphAsset::kPinOffsetIn ); };
        const auto pinOut = []( int32 nodeID )
        { return DialogueGraphAsset::encodePin( nodeID, DialogueGraphAsset::kPinOffsetOut ); };
        outGraph._listLink.push_back( DialogueAssetLink{ 1, pinOut( 1 ), pinIn( 2 ) } );
        outGraph._listLink.push_back( DialogueAssetLink{ 2, pinOut( 2 ), pinIn( 3 ) } );
        outGraph._listLink.push_back( DialogueAssetLink{ 3, DialogueGraphAsset::encodeChoicePin( 3, 0 ), pinIn( 5 ) } );
        outGraph._listLink.push_back( DialogueAssetLink{ 4, DialogueGraphAsset::encodeChoicePin( 3, 1 ), pinIn( 4 ) } );
        outGraph._listLink.push_back( DialogueAssetLink{ 5, pinOut( 4 ), pinIn( 5 ) } );
    }

    void DialogueGraphEdit::addNode( DialogueGraphAsset& inoutGraph, DialogueAssetNodeKind type, int32 nodeID )
    {
        DialogueAssetNode node = DialogueCursor::makeNode( type, nodeID );
        node._position._x      = 200.0f + static_cast<float32>( ( node._id % 5 ) * 80 );
        node._position._y      = 150.0f + static_cast<float32>( ( node._id % 5 ) * 60 );
        inoutGraph._listNode.push_back( std::move( node ) );
    }

    void DialogueGraphEdit::removeChoice( DialogueGraphAsset& inoutGraph, DialogueAssetNode& inoutNode, size_t choiceIndex )
    {
        if ( choiceIndex >= inoutNode._listChoice.size() )
            return;
        const int32 removedPin = DialogueGraphAsset::encodeChoicePin( inoutNode._id, static_cast<int32>( choiceIndex ) );
        VectorUtil::removeIf( inoutGraph._listLink, [removedPin]( const DialogueAssetLink& link )
        { return link._fromPin == removedPin; } );
        // 뒤 선택지의 링크는 번호가 하나 당겨진다 — 그대로 두면 다른 선택지를 가리키거나 없는 핀이 된다.
        for ( size_t index = choiceIndex + 1; index < inoutNode._listChoice.size(); ++index )
        {
            const int32 oldPin = DialogueGraphAsset::encodeChoicePin( inoutNode._id, static_cast<int32>( index ) );
            const int32 newPin = DialogueGraphAsset::encodeChoicePin( inoutNode._id, static_cast<int32>( index ) - 1 );
            for ( DialogueAssetLink& link : inoutGraph._listLink )
            {
                if ( link._fromPin == oldPin )
                    link._fromPin = newPin;
            }
        }
        inoutNode._listChoice.erase( inoutNode._listChoice.begin() + static_cast<std::ptrdiff_t>( choiceIndex ) );
    }
} // namespace sw::editor
