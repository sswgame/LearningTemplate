#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/GUI/Panel/Model/EditorGraphEditSession.h"
#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"
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

/** @brief [DialogueGraphEditTest] 그래프 편집 명령 한 번은 공용 그래프 입구로 알림 한 번이고 되돌리기 이름은 명령이 정한다(감사 E-16) */
SW_TEST_CASE( DialogueGraphEditTest, SessionCommandNotifiesOnceWithItsName )
{
    uint32                       count{ 0 };
    string                       lastLabel;
    const DocumentEditRecorder   recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, [&count, &lastLabel]( string_view label )
      {
        ++count;
        lastLabel = string{ label };
    } ) };
    const EditorGraphEditSession session{ recorder };
    DialogueGraphAsset           graph;
    const uint32                 speakerID = DialogueGraphEdit::addSpeaker( graph, session, "Guard" );
    SW_EXPECT_TRUE( speakerID != kDialogueNoSpeaker );
    SW_EXPECT_EQUAL( count, 1u );
    SW_EXPECT_TRUE( lastLabel == "Add Dialogue Speaker" );
}

/** @brief [DialogueGraphEditTest] 노드 칸 고치기(대사 · 조건 · 선택지 · 화자 이름)는 모델 연산이 값을 쓰고 알린다 — 창이 노드 칸을 직접 고치지 않는다 */
SW_TEST_CASE( DialogueGraphEditTest, FieldOpsWriteAndNotify )
{
    uint32                       count{ 0 };
    const DocumentEditRecorder   recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, [&count]( string_view /*label*/ )
      { ++count; } ) };
    const EditorGraphEditSession session{ recorder };
    DialogueGraphAsset           graph;
    DialogueGraphEdit::makeDefaultGraph( graph );
    DialogueAssetNode& node = graph._listNode[1];
    DialogueGraphEdit::setNodeText( node, session, "Hello" );
    DialogueGraphEdit::setNodeCondition( node, session, "flag.met" );
    DialogueGraphEdit::addChoice( node, session, "Yes" );
    SW_EXPECT_TRUE( DialogueGraphEdit::setChoiceText( node, session, node._listChoice.size() - 1, "No" ) );
    SW_EXPECT_FALSE( DialogueGraphEdit::setChoiceText( node, session, node._listChoice.size(), "Out" ) );
    const uint32 speakerID = DialogueGraphEdit::addSpeaker( graph, session, "Guard" );
    SW_EXPECT_TRUE( DialogueGraphEdit::renameSpeaker( graph, session, speakerID, "Captain" ) );
    SW_EXPECT_TRUE( node._text == "Hello" );
    SW_EXPECT_TRUE( node._condition == "flag.met" );
    SW_EXPECT_TRUE( node._listChoice.back() == "No" );
    SW_EXPECT_TRUE( graph.findSpeaker( speakerID )->_name == "Captain" );
    SW_EXPECT_EQUAL( count, 6u ); // 범위 밖 선택지는 알리지 않는다
}
