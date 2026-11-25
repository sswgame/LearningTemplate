#include "pch.h"

#include "Editor/Panels/Dialogue/Model/DialogueGraphEdit.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"
#include "Editor/Common/GUI/Panel/Model/EditorGraphEditSession.h"

#include "Engine/Dialogue/DialogueCursor.h"

namespace sw::editor
{
    ToolAssetLoadResult DialogueGraphEdit::loadGraph( DialogueGraphAsset& outGraph, const string_view path )
    {
        return EditorToolAssetCommands::loadDialogueGraph( outGraph, path );
    }

    bool DialogueGraphEdit::saveGraph( AssetWriter& writer, const DialogueGraphAsset& graph, const string_view path )
    {
        return EditorToolAssetCommands::saveDialogueGraph( writer, graph, path );
    }

    void DialogueGraphEdit::makeDefaultGraph( DialogueGraphAsset& outGraph )
    {
        outGraph._listSpeaker.clear();
        outGraph._listNode.clear();
        outGraph._listLink.clear();
        outGraph._listSpeaker.push_back( DialogueSpeaker{ 1, "Elder" } );

        DialogueAssetNode startNode{};
        startNode._id          = 1;
        startNode._type        = DialogueAssetNodeKind::Start;
        startNode._position._x = 50.0f;
        startNode._position._y = 100.0f;
        outGraph._listNode.push_back( startNode );

        DialogueAssetNode dialogueNode{};
        dialogueNode._id          = 2;
        dialogueNode._type        = DialogueAssetNodeKind::Dialogue;
        dialogueNode._speaker     = 1;
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
        DialogueAssetNode       node  = DialogueCursor::makeNode( type, nodeID );
        const DialogueNodeInfo* pInfo = DialogueGraphAsset::findNodeInfo( type );
        if ( pInfo != nullptr && pInfo->_bHasSpeaker && inoutGraph._listSpeaker.empty() == false )
            node._speaker = inoutGraph._listSpeaker.front()._id;
        node._position._x = 200.0f + static_cast<float32>( ( node._id % 5 ) * 80 );
        node._position._y = 150.0f + static_cast<float32>( ( node._id % 5 ) * 60 );
        inoutGraph._listNode.push_back( std::move( node ) );
    }

    uint32 DialogueGraphEdit::addSpeaker( DialogueGraphAsset& inoutGraph, string_view name )
    {
        const uint32 speakerID = inoutGraph.makeNextSpeakerID();
        inoutGraph._listSpeaker.push_back( DialogueSpeaker{ speakerID, string{ name } } );
        return speakerID;
    }

    void DialogueGraphEdit::removeSpeaker( DialogueGraphAsset& inoutGraph, uint32 speakerID )
    {
        VectorUtil::removeIf( inoutGraph._listSpeaker, [speakerID]( const DialogueSpeaker& speaker )
        { return speaker._id == speakerID; } );
        for ( DialogueAssetNode& node : inoutGraph._listNode )
        {
            if ( node._speaker == speakerID )
                node._speaker = kDialogueNoSpeaker;
        }
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

    void DialogueGraphEdit::commitNodeAdded( const EditorGraphEditSession& session )
    {
        session.recordDocumentEdit( "Add Dialogue Node" );
    }

    void DialogueGraphEdit::commitGraphReset( const EditorGraphEditSession& session )
    {
        session.recordDocumentEdit( "Reset Dialogue Graph" );
    }

    void DialogueGraphEdit::linkPins( DialogueGraphAsset& inoutGraph, const EditorGraphEditSession& session, int32 linkID, int32 fromPin, int32 toPin )
    {
        decltype( inoutGraph._listLink )::value_type newLink{};
        newLink._id      = linkID;
        newLink._fromPin = fromPin;
        newLink._toPin   = toPin;
        inoutGraph._listLink.push_back( newLink );
        session.recordDocumentEdit( "Link Dialogue Nodes" );
    }

    bool DialogueGraphEdit::renameSpeaker( DialogueGraphAsset& inoutGraph, const EditorGraphEditSession& session, uint32 speakerID, string_view name )
    {
        for ( DialogueSpeaker& speaker : inoutGraph._listSpeaker )
        {
            if ( speaker._id != speakerID )
                continue;
            speaker._name = string{ name };
            session.recordDocumentEdit( "Rename Dialogue Speaker" );
            return true;
        }
        return false;
    }

    uint32 DialogueGraphEdit::addSpeaker( DialogueGraphAsset& inoutGraph, const EditorGraphEditSession& session, string_view name )
    {
        const uint32 speakerID = addSpeaker( inoutGraph, name );
        session.recordDocumentEdit( "Add Dialogue Speaker" );
        return speakerID;
    }

    void DialogueGraphEdit::removeSpeaker( DialogueGraphAsset& inoutGraph, const EditorGraphEditSession& session, uint32 speakerID )
    {
        removeSpeaker( inoutGraph, speakerID );
        session.recordDocumentEdit( "Remove Dialogue Speaker" );
    }

    void DialogueGraphEdit::setNodeText( DialogueAssetNode& inoutNode, const EditorGraphEditSession& session, string_view text )
    {
        inoutNode._text = string{ text };
        session.recordDocumentEdit( "Edit Dialogue Node" );
    }

    void DialogueGraphEdit::setNodeCondition( DialogueAssetNode& inoutNode, const EditorGraphEditSession& session, string_view condition )
    {
        inoutNode._condition = string{ condition };
        session.recordDocumentEdit( "Edit Dialogue Node" );
    }

    bool DialogueGraphEdit::setChoiceText( DialogueAssetNode& inoutNode, const EditorGraphEditSession& session, size_t choiceIndex, string_view text )
    {
        if ( choiceIndex >= inoutNode._listChoice.size() )
            return false;
        inoutNode._listChoice[choiceIndex] = string{ text };
        session.recordDocumentEdit( "Edit Dialogue Node" );
        return true;
    }

    void DialogueGraphEdit::setNodeSpeaker( DialogueAssetNode& inoutNode, const EditorGraphEditSession& session, uint32 speakerID )
    {
        inoutNode._speaker = speakerID;
        session.recordDocumentEdit( "Edit Dialogue Node" );
    }

    void DialogueGraphEdit::setNodeActionCommand( DialogueAssetNode& inoutNode, const EditorGraphEditSession& session, string_view command )
    {
        inoutNode._actionCommand = string{ command };
        session.recordDocumentEdit( "Edit Dialogue Node" );
    }

    void DialogueGraphEdit::removeChoice( DialogueGraphAsset& inoutGraph, const EditorGraphEditSession& session, DialogueAssetNode& inoutNode, size_t choiceIndex )
    {
        removeChoice( inoutGraph, inoutNode, choiceIndex );
        session.recordDocumentEdit( "Edit Dialogue Node" );
    }

    void DialogueGraphEdit::addChoice( DialogueAssetNode& inoutNode, const EditorGraphEditSession& session, string_view text )
    {
        inoutNode._listChoice.push_back( string{ text } );
        session.recordDocumentEdit( "Edit Dialogue Node" );
    }
} // namespace sw::editor
