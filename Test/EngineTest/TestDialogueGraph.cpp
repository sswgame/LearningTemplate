#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Dialogue/DialogueGraphAsset.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// ------------------------------------------------------------------------------
// 1) DialogueGraphTest — 핀 번호 계약, 노드 타입 왕복, 그래프 따라가기
//
//    이 애셋에는 테스트가 하나도 없었다. 그런데 여기서 잘못되면 증상이 "가끔 다른 대사가
//    나온다" 라서, 재현도 추적도 가장 어려운 종류다.
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
} // namespace

/**
 * @brief [DialogueGraphTest] 핀 번호 인코딩·디코딩이 서로 되돌리는지 검증
 * @details 핀 번호는 **디스크에 저장되는 계약**이다. 예전에는 에디터가 인코딩을, 애셋이 디코딩을
 *          각자 적고 있었다 — 한쪽만 바뀌면 대화가 조용히 엉뚱한 분기를 탄다.
 */
SW_TEST_CASE( DialogueGraphTest, PinEncodeAndDecodeRoundTrip )
{
    const int32 outPin = DialogueGraphAsset::encodePin( 7, DialogueGraphAsset::kPinOffsetOut );
    SW_EXPECT_EQUAL( 7, DialogueGraphAsset::decodePinNodeId( outPin ) );
    SW_EXPECT_EQUAL( DialogueGraphAsset::kPinOffsetOut, DialogueGraphAsset::decodePinOffset( outPin ) );

    const int32 choicePin = DialogueGraphAsset::encodeChoicePin( 12, 3 );
    SW_EXPECT_EQUAL( 12, DialogueGraphAsset::decodePinNodeId( choicePin ) );
    SW_EXPECT_EQUAL( DialogueGraphAsset::kPinOffsetChoiceBase + 3, DialogueGraphAsset::decodePinOffset( choicePin ) );

    // 레거시 인코딩(노드 id * 10)도 계속 읽힌다.
    SW_EXPECT_EQUAL( 1, DialogueGraphAsset::decodePinNodeId( 12 ) );
    SW_EXPECT_EQUAL( 2, DialogueGraphAsset::decodePinOffset( 12 ) );
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
 * @details loadFromFile 은 읽어 둔 문서를 다시 문자열로 덤프해 parseJson 에 넘기고 있었다.
 *          같은 JSON 을 두 번 파싱하던 것을 한 번으로 줄였으므로 결과가 같음을 못박아 둔다.
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

    const string filePath = FileUtil::joinPath( FileUtil::getTempDirectory(), "test_dialogue_graph.json" );
    SW_EXPECT_TRUE( source.saveToFile( filePath ) );

    DialogueGraphAsset loaded;
    SW_EXPECT_TRUE( loaded.loadFromFile( filePath ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( parsed._listNode.size() ), static_cast<uint32>( loaded._listNode.size() ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( parsed._listLink.size() ), static_cast<uint32>( loaded._listLink.size() ) );
    SW_EXPECT_EQUAL( parsed._listNode[1]._text, loaded._listNode[1]._text );
    SW_EXPECT_EQUAL( 4, loaded.findChoiceNextNodeId( 2, 1 ) );

    FileUtil::removeFile( filePath );
}

/**
 * @brief [DialogueGraphTest] 로컬라이즈 키가 아닌 원문은 그대로 돌려주는지 검증
 * @details 이 조회는 **키가 아닐 수도 있는 텍스트**로 물어본다. 예전에는 그 원문으로
 *          `hashed_string` 을 만들어 물어서, 대사가 intern 아레나에 영구히 남았다.
 */
SW_TEST_CASE( DialogueGraphTest, PlainTextResolvesToItself )
{
    const string plainText = "이건 로컬라이즈 키가 아니라 그냥 대사다.";
    SW_EXPECT_EQUAL( plainText, DialogueGraphAsset::resolveLocalizedText( plainText ) );
    SW_EXPECT_TRUE( DialogueGraphAsset::resolveLocalizedText( "" ).empty() );
}
