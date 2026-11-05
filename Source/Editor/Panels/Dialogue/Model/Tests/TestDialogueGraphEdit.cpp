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
