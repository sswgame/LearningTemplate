#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Dialogue/DialogueCursor.h"
#include "Engine/Dialogue/DialogueGraphAsset.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"

#include "GameFramework/Base/UI/UI/DialogueRunnerComponent.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// ------------------------------------------------------------------------------
// 1) DialogueGraphTest — 핀 번호 계약, 노드 타입 왕복, 그래프 따라가기
//
//    여기서 잘못되면 증상이 "가끔 다른 대사가 나온다" 라서, 재현도 추적도 가장 어려운 종류다.
// ------------------------------------------------------------------------------

namespace
{
    /**
     * @brief Start(1) → Choice(2) → {A:3, B:4} 그래프를 만듭니다.
     * @details 선택지 핀과 기본 출력 핀이 함께 있는 가장 작은 모양입니다.
     */
    DialogueGraphAsset makeChoiceGraph()
    {
        DialogueGraphAsset asset;

        DialogueAssetNode startNode{};
        startNode._id   = 1;
        startNode._type = DialogueAssetNodeType::Start;
        DialogueAssetNode choiceNode{};
        choiceNode._id         = 2;
        choiceNode._type       = DialogueAssetNodeType::Choice;
        choiceNode._text       = "어디로 갈까?";
        choiceNode._listChoice = { "왼쪽", "오른쪽" };
        DialogueAssetNode leftNode{};
        leftNode._id   = 3;
        leftNode._type = DialogueAssetNodeType::Dialogue;
        leftNode._text = "왼쪽으로 갔다.";
        DialogueAssetNode rightNode{};
        rightNode._id   = 4;
        rightNode._type = DialogueAssetNodeType::Dialogue;
        rightNode._text = "오른쪽으로 갔다.";

        asset._listNode = { startNode, choiceNode, leftNode, rightNode };

        asset._listLink.push_back( DialogueAssetLink{ 1,
                                                      DialogueGraphAsset::encodePin( 1, DialogueGraphAsset::kPinOffsetOut ),
                                                      DialogueGraphAsset::encodePin( 2, DialogueGraphAsset::kPinOffsetIn ) } );
        asset._listLink.push_back( DialogueAssetLink{ 2,
                                                      DialogueGraphAsset::encodeChoicePin( 2, 0 ),
                                                      DialogueGraphAsset::encodePin( 3, DialogueGraphAsset::kPinOffsetIn ) } );
        asset._listLink.push_back( DialogueAssetLink{ 3,
                                                      DialogueGraphAsset::encodeChoicePin( 2, 1 ),
                                                      DialogueGraphAsset::encodePin( 4, DialogueGraphAsset::kPinOffsetIn ) } );
        return asset;
    }

    /** @brief `makeProbeGraph` 의 노드 2 를 지날 때 쓰는 입력입니다(선택지 1, 조건 참). */
    DialogueStepInput makeProbeInput()
    {
        DialogueStepInput input{};
        input._choiceIndex   = 1;
        input._bConditionMet = true;
        return input;
    }

    /**
     * @brief 노드 2(종류 `type`)의 모든 출력 핀을 서로 다른 대사 노드로 잇습니다.
     * @details Out → 10, True → 11, False → 12, 선택지 0 → 13, 선택지 1 → 14. 대사 노드들은 끝(나가는 링크 없음)입니다.
     *          조건식은 비어 있어 러너가 참으로 평가합니다(`makeProbeInput` 과 같은 입력).
     */
    DialogueGraphAsset makeProbeGraph( DialogueAssetNodeType type )
    {
        DialogueGraphAsset asset;

        DialogueAssetNode probeNode{};
        probeNode._id            = 2;
        probeNode._type          = type;
        probeNode._text          = "probe";
        probeNode._actionCommand = "noop";
        probeNode._listChoice    = { "a", "b" };
        asset._listNode.push_back( probeNode );

        const int32 arrTargetId[] = { 10, 11, 12, 13, 14 };
        for ( int32 targetId : arrTargetId )
        {
            DialogueAssetNode target{};
            target._id   = targetId;
            target._type = DialogueAssetNodeType::Dialogue;
            target._text = "target";
            asset._listNode.push_back( target );
        }

        const int32 arrFromPin[] = {
            DialogueGraphAsset::encodePin( 2, DialogueGraphAsset::kPinOffsetOut ),
            DialogueGraphAsset::encodePin( 2, DialogueGraphAsset::kPinOffsetTrue ),
            DialogueGraphAsset::encodePin( 2, DialogueGraphAsset::kPinOffsetFalse ),
            DialogueGraphAsset::encodeChoicePin( 2, 0 ),
            DialogueGraphAsset::encodeChoicePin( 2, 1 ),
        };
        for ( int32 linkIndex = 0; linkIndex < static_cast<int32>( SW_COUNT_OF( arrFromPin ) ); ++linkIndex )
        {
            const int32 toPin = DialogueGraphAsset::encodePin( arrTargetId[linkIndex], DialogueGraphAsset::kPinOffsetIn );
            asset._listLink.push_back( DialogueAssetLink{ linkIndex + 1, arrFromPin[linkIndex], toPin } );
        }
        return asset;
    }
} // namespace

/**
 * @brief [DialogueGraphTest] 핀 번호 인코딩·디코딩이 서로 되돌리는지 검증
 * @details 핀 번호는 **디스크에 저장되는 계약**이다. 에디터의 인코딩과 애셋의 디코딩을 각자 적으면 한쪽만 바뀔 때
 *          대화가 조용히 엉뚱한 분기를 탄다.
 */
SW_TEST_CASE( DialogueGraphTest, PinEncodeAndDecodeRoundTrip )
{
    const int32 outPin = DialogueGraphAsset::encodePin( 7, DialogueGraphAsset::kPinOffsetOut );
    SW_EXPECT_EQUAL( 7, DialogueGraphAsset::decodePinNodeId( outPin ) );
    SW_EXPECT_EQUAL( DialogueGraphAsset::kPinOffsetOut, DialogueGraphAsset::decodePinOffset( outPin ) );

    const int32 choicePin = DialogueGraphAsset::encodeChoicePin( 12, 3 );
    SW_EXPECT_EQUAL( 12, DialogueGraphAsset::decodePinNodeId( choicePin ) );
    SW_EXPECT_EQUAL( DialogueGraphAsset::kPinOffsetChoiceBase + 3, DialogueGraphAsset::decodePinOffset( choicePin ) );

    // kPinScale 보다 작은 값은 핀이 아니다(노드 id 는 1 부터) — 노드 id * 10 으로 짐작해 읽지 않는다.
    SW_EXPECT_EQUAL( 0, DialogueGraphAsset::decodePinNodeId( 12 ) );
    SW_EXPECT_EQUAL( 0, DialogueGraphAsset::decodePinOffset( 12 ) );
}

/**
 * @brief [DialogueGraphTest] 자릿수에 담기지 않는 핀은 만들지 않는지 검증
 * @details 오프셋이 `kPinScale` 을 넘으면 **노드 id 를 오염시켜** 링크가 다른 노드를 가리킨다.
 *          조용히 그런 번호를 만드느니 "없는 핀"(0)을 돌려준다.
 */
SW_TEST_CASE( DialogueGraphTest, PinRefusesOffsetsThatWouldOverflowTheNodeId )
{
    SW_EXPECT_EQUAL( 0, DialogueGraphAsset::encodePin( 5, DialogueGraphAsset::kPinScale ) );
    SW_EXPECT_EQUAL( 0, DialogueGraphAsset::encodePin( 5, DialogueGraphAsset::kPinScale + 1 ) );
    SW_EXPECT_EQUAL( 0, DialogueGraphAsset::encodePin( 0, DialogueGraphAsset::kPinOffsetOut ) );

    // 선택지도 같다 — 마지막으로 담기는 것은 되고, 그 다음은 안 된다.
    const int32 lastChoiceIndex = DialogueGraphAsset::getMaxChoiceCount() - 1;
    SW_EXPECT_TRUE( DialogueGraphAsset::encodeChoicePin( 5, lastChoiceIndex ) > 0 );
    SW_EXPECT_EQUAL( 0, DialogueGraphAsset::encodeChoicePin( 5, lastChoiceIndex + 1 ) );
    SW_EXPECT_EQUAL( 0, DialogueGraphAsset::encodeChoicePin( 5, -1 ) );

    // 담기는 범위 안에서는 노드 id 가 절대 흔들리지 않는다.
    for ( int32 choiceIndex = 0; choiceIndex <= lastChoiceIndex; ++choiceIndex )
    {
        const int32 pin = DialogueGraphAsset::encodeChoicePin( 5, choiceIndex );
        SW_EXPECT_EQUAL( 5, DialogueGraphAsset::decodePinNodeId( pin ) );
    }
}

/**
 * @brief [DialogueGraphTest] 노드 타입 이름이 저장과 해석에서 같은 표를 보는지 검증
 * @details 이름을 짓는 쪽과 읽는 쪽이 목록을 따로 들고 있으면, 열거자를 하나 더하고 한쪽만
 *          고쳤을 때 그 노드가 조용히 Dialogue 로 떨어진다 — 파일은 멀쩡한데 대화만 달라진다.
 */
SW_TEST_CASE( DialogueGraphTest, NodeTypeNameRoundTripsForEveryValue )
{
    const DialogueAssetNodeType arrType[] = {
        DialogueAssetNodeType::Start,
        DialogueAssetNodeType::Dialogue,
        DialogueAssetNodeType::Choice,
        DialogueAssetNodeType::Branch,
        DialogueAssetNodeType::Action,
        DialogueAssetNodeType::End,
    };

    for ( DialogueAssetNodeType type : arrType )
    {
        const utf8* pName = DialogueGraphAsset::nodeTypeName( type );
        SW_EXPECT_TRUE( string_view( pName ) != string_view( "Unknown" ) );
        SW_EXPECT_TRUE( DialogueGraphAsset::parseNodeType( pName ) == type );
    }

    // 모르는 이름은 Dialogue 로 떨어진다 — 그것이 계약이다.
    SW_EXPECT_TRUE( DialogueGraphAsset::parseNodeType( "NoSuchType" ) == DialogueAssetNodeType::Dialogue );
}

/**
 * @brief [DialogueGraphTest] 선택지·기본 출력 핀을 따라 다음 노드를 찾는지 검증
 */
SW_TEST_CASE( DialogueGraphTest, FollowsChoiceAndDefaultPins )
{
    const DialogueGraphAsset asset = makeChoiceGraph();

    const DialogueAssetNode* pStart = asset.findStartNode();
    SW_EXPECT_NOT_NULL( pStart );
    SW_EXPECT_EQUAL( 1, pStart->_id );

    SW_EXPECT_EQUAL( 2, asset.findDefaultNextNodeId( 1 ) );
    SW_EXPECT_EQUAL( 3, asset.findChoiceNextNodeId( 2, 0 ) );
    SW_EXPECT_EQUAL( 4, asset.findChoiceNextNodeId( 2, 1 ) );

    // 연결되지 않은 선택지는 기본 출력으로 떨어진다 — 여기서는 그것도 없으므로 0 이다.
    SW_EXPECT_EQUAL( 0, asset.findChoiceNextNodeId( 2, 5 ) );
    // 범위를 벗어난 선택지 번호도 기본 출력을 볼 뿐, 엉뚱한 노드로 가지 않는다.
    SW_EXPECT_EQUAL( 0, asset.findChoiceNextNodeId( 2, DialogueGraphAsset::getMaxChoiceCount() + 10 ) );
}

/**
 * @brief [DialogueGraphTest] JSON 왕복과 loadFromFile 이 같은 결과를 내는지 검증
 * @details loadFromFile 은 읽어 둔 문서를 다시 문자열로 덤프하지 않고 한 번만 파싱한다. 두 경로의 결과가 같음을 못박아 둔다.
 */
SW_TEST_CASE( DialogueGraphTest, JsonRoundTripAndLoadFromFileAgree )
{
    const DialogueGraphAsset source = makeChoiceGraph();

    DialogueGraphAsset parsed;
    SW_EXPECT_TRUE( parsed.parseJson( source.toJson() ) );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( parsed._listNode.size() ) );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( parsed._listLink.size() ) );
    SW_EXPECT_TRUE( parsed._listNode[1]._type == DialogueAssetNodeType::Choice );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( parsed._listNode[1]._listChoice.size() ) );
    SW_EXPECT_EQUAL( 3, parsed.findChoiceNextNodeId( 2, 0 ) );

    const string filePath = test::makeTempPath( "test_dialogue_graph.json" );
    SW_EXPECT_TRUE( source.saveToFile( filePath ) );

    DialogueGraphAsset loaded;
    SW_EXPECT_TRUE( loaded.loadFromFile( filePath ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( parsed._listNode.size() ), static_cast<uint32>( loaded._listNode.size() ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( parsed._listLink.size() ), static_cast<uint32>( loaded._listLink.size() ) );
    SW_EXPECT_EQUAL( parsed._listNode[1]._text, loaded._listNode[1]._text );
    SW_EXPECT_EQUAL( 4, loaded.findChoiceNextNodeId( 2, 1 ) );
}

/**
 * @brief [DialogueGraphTest] 로컬라이즈 키가 아닌 원문은 그대로 돌려주는지 검증
 * @details 이 조회는 **키가 아닐 수도 있는 텍스트**로 물어본다. 그 원문으로 `hashed_string` 을 만들어 물으면
 *          대사가 intern 아레나에 영구히 남는다.
 */
SW_TEST_CASE( DialogueGraphTest, PlainTextResolvesToItself )
{
    const string plainText = "이건 로컬라이즈 키가 아니라 그냥 대사다.";
    SW_EXPECT_EQUAL( plainText, DialogueGraphAsset::resolveLocalizedText( plainText ) );
    SW_EXPECT_TRUE( DialogueGraphAsset::resolveLocalizedText( "" ).empty() );
}

// ------------------------------------------------------------------------------
// 2) 노드 종류 표 · DialogueCursor · 러너 — 진행 규칙이 한 곳에 있는지
// ------------------------------------------------------------------------------

/**
 * @brief [DialogueGraphTest] 특성 표가 노드 타입마다 한 줄이고 순서가 값 순서인지 검증
 */
SW_TEST_CASE( DialogueGraphTest, NodeInfoCoversEveryType )
{
    for ( uint32 typeIndex = 0; typeIndex < static_cast<uint32>( DialogueAssetNodeType::Count ); ++typeIndex )
    {
        const DialogueAssetNodeType type  = static_cast<DialogueAssetNodeType>( typeIndex );
        const DialogueNodeInfo*     pInfo = DialogueGraphAsset::findNodeInfo( type );
        SW_ASSERT_NOT_NULL( pInfo );
        SW_EXPECT_TRUE( pInfo->_type == type );
        SW_EXPECT_TRUE( DialogueGraphAsset::parseNodeType( pInfo->_pName ) == type );
    }
    SW_EXPECT_NULL( DialogueGraphAsset::findNodeInfo( DialogueAssetNodeType::Count ) );
    SW_EXPECT_EQUAL( string_view( "Unknown" ), string_view( DialogueGraphAsset::nodeTypeName( DialogueAssetNodeType::Count ) ) );
}

/**
 * @brief [DialogueGraphTest] 러너가 모든 노드 종류에서 `DialogueCursor::step` 과 같은 다음 노드로 가는지 검증
 * @details 에디터 미리보기는 `DialogueCursor::step` 을 그대로 부르므로, 러너가 커서와 같으면 미리보기와 게임이 같은 길을 간다.
 *          종류마다 기대 노드도 못박는다(Start · Dialogue · Action → Out, Choice → 선택지 1, Branch → 참, End → 끝).
 *          첫 대사 전의 Action 은 러너가 Idle 상태에서 돌린다 — 상태 값으로 "핸들러가 멈췄는가" 를 보면 여기서 대화가 조용히 선다.
 */
SW_TEST_CASE( DialogueGraphTest, RunnerAndCursorAgreeOnEveryType )
{
    const int32 arrExpectedNextId[] = { 10, 10, 14, 11, 10, 0 };
    static_assert( SW_COUNT_OF( arrExpectedNextId ) == static_cast<size_t>( DialogueAssetNodeType::Count ), "노드 종류마다 기대 값 하나" );

    for ( uint32 typeIndex = 0; typeIndex < static_cast<uint32>( DialogueAssetNodeType::Count ); ++typeIndex )
    {
        const DialogueAssetNodeType type   = static_cast<DialogueAssetNodeType>( typeIndex );
        const DialogueGraphAsset    asset  = makeProbeGraph( type );
        const DialogueAssetNode*    pProbe = asset.findNode( 2 );
        SW_ASSERT_NOT_NULL( pProbe );
        const int32 cursorNextId = DialogueCursor::step( asset, *pProbe, makeProbeInput() );
        SW_EXPECT_EQUAL( arrExpectedNextId[typeIndex], cursorNextId );

        DialogueRunnerComponent runner;
        runner.setGraph( asset );
        uint32 finishedCount{ 0 };
        runner.setOnDialogueFinished( [&finishedCount]()
        { ++finishedCount; } );
        SW_EXPECT_TRUE( runner.startDialogue( 2 ) );

        const DialogueNodeInfo* pInfo = DialogueGraphAsset::findNodeInfo( type );
        SW_ASSERT_NOT_NULL( pInfo );
        if ( pInfo->_flow == DialogueNodeFlow::WaitAdvance )
        {
            SW_EXPECT_EQUAL( 2, runner.getCurrentNodeId() );
            SW_EXPECT_TRUE( runner.advance() );
        }
        else if ( pInfo->_flow == DialogueNodeFlow::WaitChoice )
        {
            SW_EXPECT_EQUAL( 2, runner.getCurrentNodeId() );
            SW_EXPECT_TRUE( runner.selectChoice( 1 ) );
        }

        if ( cursorNextId > 0 )
        {
            SW_EXPECT_EQUAL( static_cast<uint8>( DialogueRunnerState::ShowingDialogue ), static_cast<uint8>( runner.getState() ) );
            SW_EXPECT_EQUAL( cursorNextId, runner.getCurrentNodeId() );
        }
        else
        {
            SW_EXPECT_EQUAL( static_cast<uint8>( DialogueRunnerState::Finished ), static_cast<uint8>( runner.getState() ) );
            SW_EXPECT_EQUAL( 1u, finishedCount );
        }
    }
}

/**
 * @brief [DialogueGraphTest] 이어지지 않은 선택지 · 분기 핀은 기본 출력 핀으로 가는지 검증
 * @details 러너와 미리보기가 이 규칙을 따로 들고 있으면, 미리보기만 "연결 없음" 으로 멈추고 게임은 기본 출력으로 간다.
 */
SW_TEST_CASE( DialogueGraphTest, UnlinkedChoiceAndBranchPinsFallBackToOut )
{
    const DialogueAssetNodeType arrType[] = { DialogueAssetNodeType::Choice, DialogueAssetNodeType::Branch };
    for ( DialogueAssetNodeType type : arrType )
    {
        DialogueGraphAsset asset = makeProbeGraph( type );
        asset._listLink.resize( 1 ); // Out(→10) 만 남긴다
        const DialogueAssetNode* pProbe = asset.findNode( 2 );
        SW_ASSERT_NOT_NULL( pProbe );
        SW_EXPECT_EQUAL( 10, DialogueCursor::step( asset, *pProbe, makeProbeInput() ) );
        SW_EXPECT_EQUAL( 10, DialogueCursor::step( asset, *pProbe, DialogueStepInput{} ) );
    }
}

/**
 * @brief [DialogueGraphTest] 표에 없는 노드 타입을 만나면 러너가 대화를 끝내는지 검증
 * @details 모르는 타입에서 아무 갈래도 타지 않으면 상태가 그대로 남고 끝 알림도 없이 대화가 멈춰 있다.
 */
SW_TEST_CASE( DialogueGraphTest, UnknownNodeTypeFinishesTheDialogue )
{
    const DialogueAssetNodeType arrUnknownType[] = { DialogueAssetNodeType::Count, static_cast<DialogueAssetNodeType>( 42 ) };
    for ( DialogueAssetNodeType unknownType : arrUnknownType )
    {
        DialogueGraphAsset       asset  = makeProbeGraph( unknownType );
        const DialogueAssetNode* pProbe = asset.findNode( 2 );
        SW_ASSERT_NOT_NULL( pProbe );
        SW_EXPECT_EQUAL( 0, DialogueCursor::step( asset, *pProbe, makeProbeInput() ) );

        DialogueRunnerComponent runner;
        runner.setGraph( std::move( asset ) );
        uint32 finishedCount{ 0 };
        runner.setOnDialogueFinished( [&finishedCount]()
        { ++finishedCount; } );
        SW_EXPECT_TRUE( runner.startDialogue( 2 ) );
        SW_EXPECT_EQUAL( static_cast<uint8>( DialogueRunnerState::Finished ), static_cast<uint8>( runner.getState() ) );
        SW_EXPECT_EQUAL( 1u, finishedCount );
    }
}

/**
 * @brief [DialogueGraphTest] 첫 대사 전의 Action 뒤로 대화가 이어지는지 검증(Start → Action → Dialogue)
 */
SW_TEST_CASE( DialogueGraphTest, ActionBeforeFirstLineContinues )
{
    const utf8* pJson = R"({
		"nodes": [
			{ "id": 1, "type": "Start" },
			{ "id": 2, "type": "Action", "action": "open_curtain" },
			{ "id": 3, "type": "Dialogue", "speaker": "NPC", "text": "Welcome" }
		],
		"links": [
			{ "from": 102, "to": 201 },
			{ "from": 202, "to": 301 }
		]
	})";

    DialogueRunnerComponent runner;
    SW_EXPECT_TRUE( runner.loadGraphJson( pJson ) );
    string eventCommand;
    runner.setOnDialogueEvent( [&eventCommand]( const string& command )
    { eventCommand = command; } );
    SW_EXPECT_TRUE( runner.startDialogue() );
    SW_EXPECT_EQUAL( "open_curtain", eventCommand );
    SW_EXPECT_EQUAL( static_cast<uint8>( DialogueRunnerState::ShowingDialogue ), static_cast<uint8>( runner.getState() ) );
    SW_EXPECT_EQUAL( "Welcome", runner.getCurrentText() );
}

/**
 * @brief [DialogueGraphTest] 새 노드의 기본 본문이 그 종류가 편집하는 칸에 들어가는지 검증
 * @details Branch 의 기본 조건식 · Action 의 기본 명령이 `_text` 에 들어가면 러너는 빈 조건(참) · 빈 명령을 본다.
 */
SW_TEST_CASE( DialogueGraphTest, MakeNodeFillsTheBodyField )
{
    const DialogueAssetNode branchNode = DialogueCursor::makeNode( DialogueAssetNodeType::Branch, 7 );
    SW_EXPECT_EQUAL( 7, branchNode._id );
    SW_EXPECT_FALSE( branchNode._condition.empty() );
    SW_EXPECT_TRUE( branchNode._text.empty() );

    const DialogueAssetNode actionNode = DialogueCursor::makeNode( DialogueAssetNodeType::Action, 8 );
    SW_EXPECT_FALSE( actionNode._actionCommand.empty() );
    SW_EXPECT_TRUE( actionNode._text.empty() );

    const DialogueAssetNode choiceNode = DialogueCursor::makeNode( DialogueAssetNodeType::Choice, 9 );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( choiceNode._listChoice.size() ) );
    SW_EXPECT_EQUAL( "Option 1", choiceNode._listChoice[0] );

    const DialogueAssetNode dialogueNode = DialogueCursor::makeNode( DialogueAssetNodeType::Dialogue, 10 );
    SW_EXPECT_FALSE( dialogueNode._speaker.empty() );
    SW_EXPECT_FALSE( dialogueNode._text.empty() );
}

/**
 * @brief [DialogueGraphTest] 상태를 읽어 들인 러너는 플레이 전에도 그 그래프를 든다
 * @details 상태 읽기는 프로퍼티를 직접 쓰고 `onPostLoad` 만 부른다. 그래프를 onBeginPlay 에서만 열면 에디터에서 읽은 러너의
 *          Start Dialogue(CallInEditor)가 "No valid nodes" 로 끝난다.
 */
SW_TEST_CASE( DialogueGraphTest, RunnerReopensItsGraphAfterStateLoad )
{
    const string filePath = test::makeTempPath( "test_state_dialogue_graph.json" );
    SW_ASSERT_TRUE( makeChoiceGraph().saveToFile( filePath ) );

    GameObjectManager manager;
    GameObject*       pSource = manager.createGameObject( hashed_string( "Npc" ) );
    SW_ASSERT_NOT_NULL( pSource );
    DialogueRunnerComponent* pSourceRunner = pSource->addComponent<DialogueRunnerComponent>();
    SW_ASSERT_NOT_NULL( pSourceRunner );
    pSourceRunner->_graphPath = filePath;

    const string xml = ObjectStateSerializer::saveToXmlString( pSource );
    SW_ASSERT_FALSE( xml.empty() );
    GameObject* pTarget = manager.createGameObject( hashed_string( "LoadedNpc" ) );
    SW_ASSERT_NOT_NULL( pTarget );
    SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXmlString( pTarget, xml ) );

    DialogueRunnerComponent* pLoaded = pTarget->getComponent<DialogueRunnerComponent>();
    SW_ASSERT_NOT_NULL( pLoaded );
    SW_EXPECT_EQUAL( filePath, pLoaded->_graphPath );
    SW_EXPECT_FALSE( pLoaded->hasBegunPlay() );
    SW_EXPECT_TRUE( pLoaded->startDialogue() );
    SW_EXPECT_EQUAL( static_cast<uint8>( DialogueRunnerState::WaitingForChoice ), static_cast<uint8>( pLoaded->getState() ) );
}
