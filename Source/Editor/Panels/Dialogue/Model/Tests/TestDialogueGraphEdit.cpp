#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Dialogue/Model/DialogueGraphEdit.h"

using namespace sw;
using namespace sw::editor;

SW_TEST_CASE( DialogueGraphEditTest, DefaultGraphLinksChoicesToTheirTargets )
{
    DialogueGraphAsset graph;
    DialogueGraphEdit::makeDefaultGraph( graph );
    SW_EXPECT_EQUAL( 5u, static_cast<uint32>( graph._listNode.size() ) );
    SW_EXPECT_EQUAL( 5u, static_cast<uint32>( graph._listLink.size() ) );
    SW_EXPECT_EQUAL( DialogueGraphAsset::encodeChoicePin( 3, 1 ), graph._listLink[3]._fromPin );
}

// 화자는 id 로 더하고 지운다 — 지운 화자를 쓰던 줄은 화자 없음이 되어 그래프가 다시 읽힌다(없는 id 를 가리킨 채 남지 않는다).
SW_TEST_CASE( DialogueGraphEditTest, SpeakersAreAddedAndRemovedByID )
{
    DialogueGraphAsset graph;
    DialogueGraphEdit::makeDefaultGraph( graph );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( graph._listSpeaker.size() ) );
    SW_EXPECT_EQUAL( 1u, graph._listNode[1]._speaker );
    const uint32 guardID = DialogueGraphEdit::addSpeaker( graph, "Guard" );
    SW_EXPECT_EQUAL( 2u, guardID );
    DialogueGraphEdit::addNode( graph, DialogueAssetNodeKind::Dialogue, 6 );
    SW_EXPECT_EQUAL( 1u, graph._listNode.back()._speaker ); // 새 줄은 목록의 첫 화자

    DialogueGraphEdit::removeSpeaker( graph, 1 );
    SW_EXPECT_EQUAL( kDialogueNoSpeaker, graph._listNode[1]._speaker );
    SW_EXPECT_EQUAL( kDialogueNoSpeaker, graph._listNode.back()._speaker );
    DialogueGraphAsset reread;
    SW_EXPECT_TRUE( reread.loadFromXMLText( graph.toXML() ) );
    SW_EXPECT_EQUAL( string( "Guard" ), reread.resolveSpeakerName( guardID ) );
}

// 지운 선택지의 링크는 사라지고, 뒤 선택지의 링크는 같은 선택지를 계속 가리킨다.
SW_TEST_CASE( DialogueGraphEditTest, RemovingAChoiceKeepsTheOtherChoicesLinked )
{
    DialogueGraphAsset graph;
    DialogueGraphEdit::makeDefaultGraph( graph );
    DialogueAssetNode& choiceNode = graph._listNode[2];
    DialogueGraphEdit::removeChoice( graph, choiceNode, 0 );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( choiceNode._listChoice.size() ) );
    SW_EXPECT_TRUE( choiceNode._listChoice[0] == "Could you give me some supplies first?" );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( graph._listLink.size() ) );
    const int32 firstPin = DialogueGraphAsset::encodeChoicePin( 3, 0 );
    uint32      count    = 0;
    for ( const DialogueAssetLink& link : graph._listLink )
    {
        SW_EXPECT_FALSE( link._fromPin == DialogueGraphAsset::encodeChoicePin( 3, 1 ) );
        if ( link._fromPin == firstPin )
        {
            ++count;
            SW_EXPECT_EQUAL( DialogueGraphAsset::encodePin( 4, DialogueGraphAsset::kPinOffsetIn ), link._toPin );
        }
    }
    SW_EXPECT_EQUAL( 1u, count );
}

SW_TEST_CASE( DialogueGraphEditTest, RemovingOutOfRangeChangesNothing )
{
    DialogueGraphAsset graph;
    DialogueGraphEdit::makeDefaultGraph( graph );
    DialogueGraphEdit::removeChoice( graph, graph._listNode[2], 9 );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( graph._listNode[2]._listChoice.size() ) );
    SW_EXPECT_EQUAL( 5u, static_cast<uint32>( graph._listLink.size() ) );
}
