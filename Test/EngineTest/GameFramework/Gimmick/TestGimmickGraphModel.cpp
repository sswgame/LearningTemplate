// 기믹 회로 그래프 편집 모델 — 노드 · 배선 편집(방향 · 포트 검사, 이름 바꾸기가 배선을 따라감), 매개변수 글, 저장 목록 왕복(가리키는 노드가 없는 배선 보존),
// 검증 문제(회로 검증과 같은 규칙), 층 배치, 컴포넌트로 쓰기와 읽기.
#include "pch.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuitComponent.h"
#include "GameFramework/PlugIn/Editor/Base/Gimmick/GimmickGraphBridge.h"
#include "GameFramework/PlugIn/Editor/Base/Gimmick/GimmickGraphModel.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 눌림판 → 지연 → 문 한 줄짜리 회로를 만든 모델입니다. */
    struct GraphFixture
    {
        GimmickGraphModel _model;
        int32             _plate;
        int32             _both;
        int32             _door;

        GraphFixture()
            : _model{ GimmickNodeRegistry::getDefault() }
            , _plate{ 0 }
            , _both{ 0 }
            , _door{ 0 }
        {
            _plate = _model.addNode( hashed_string( "Signal" ) );
            _both  = _model.addNode( hashed_string( "And" ) );
            _door  = _model.addNode( hashed_string( "Door" ) );
        }

        bool wire( int32 fromNode, string_view fromPort, int32 toNode, string_view toPort )
        {
            int32  wireID = 0;
            string error;
            return _model.addWire( fromNode, fromPort, toNode, toPort, wireID, error );
        }
    };
} // namespace

SW_TEST_CASE( GimmickGraphModelTest, AddNodeMakesUniqueIDsAndRejectsUnknownKinds )
{
    GimmickGraphModel model( GimmickNodeRegistry::getDefault() );
    const int32       first  = model.addNode( hashed_string( "Door" ) );
    const int32       second = model.addNode( hashed_string( "Door" ) );
    SW_ASSERT_TRUE( first != GimmickGraphModel::kNoGraphID && second != GimmickGraphModel::kNoGraphID );
    SW_EXPECT_TRUE( first != second );
    SW_EXPECT_EQUAL( string( model.getNodes()[0]._id.c_str() ), string( "door_1" ) );
    SW_EXPECT_EQUAL( string( model.getNodes()[1]._id.c_str() ), string( "door_2" ) );
    SW_EXPECT_EQUAL( model.addNode( hashed_string( "NoSuchKind" ) ), GimmickGraphModel::kNoGraphID );
    SW_EXPECT_EQUAL( model.getNodes().size(), size_t( 2 ) );
}

SW_TEST_CASE( GimmickGraphModelTest, AddWireChecksDirectionPortsAndDuplicates )
{
    GraphFixture fixture;
    int32        wireID = 0;
    string       error;
    SW_EXPECT_TRUE( fixture._model.addWire( fixture._plate, "Active", fixture._both, "A", wireID, error ) );
    SW_EXPECT_TRUE( wireID != GimmickGraphModel::kNoGraphID );
    SW_EXPECT_FALSE( fixture._model.addWire( fixture._plate, "Active", fixture._both, "A", wireID, error ) ); // 같은 배선
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( fixture._model.addWire( fixture._plate, "ACTIVE", fixture._both, "B", wireID, error ) );    // 포트 이름은 대소문자를 가리지 않는다
    SW_EXPECT_FALSE( fixture._model.addWire( fixture._both, "A", fixture._door, "Open", wireID, error ) );      // A 는 입력이라 첫 끝이 될 수 없다
    SW_EXPECT_FALSE( fixture._model.addWire( fixture._both, "Out", fixture._door, "Nothing", wireID, error ) ); // 없는 입력
    SW_EXPECT_FALSE( fixture._model.addWire( fixture._both, "Out", 9999, "Open", wireID, error ) );             // 없는 노드
    SW_EXPECT_TRUE( fixture._model.addWire( fixture._both, "Out", fixture._door, "Open", wireID, error ) );
    SW_EXPECT_EQUAL( fixture._model.getWires().size(), size_t( 3 ) );
}

SW_TEST_CASE( GimmickGraphModelTest, RenameKeepsWiresAndRemoveNodeTakesItsWires )
{
    GraphFixture fixture;
    SW_ASSERT_TRUE( fixture.wire( fixture._plate, "Active", fixture._both, "A" ) );
    SW_ASSERT_TRUE( fixture.wire( fixture._both, "Out", fixture._door, "Open" ) );
    string error;
    SW_EXPECT_FALSE( fixture._model.renameNode( fixture._both, hashed_string( "signal_1" ), error ) ); // 다른 노드가 쓰는 이름
    SW_EXPECT_FALSE( fixture._model.renameNode( fixture._both, hashed_string( "" ), error ) );
    SW_EXPECT_TRUE( fixture._model.renameNode( fixture._both, hashed_string( "both" ), error ) );

    vector<GimmickNodeDesc>  listNode;
    vector<GameObjectHandle> listTarget;
    vector<GimmickWireDesc>  listWire;
    fixture._model.exportTo( listNode, listTarget, listWire );
    SW_ASSERT_EQUAL( listWire.size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( listWire[0]._to, string( "both.A" ) ); // 배선이 새 이름을 따라간다
    SW_EXPECT_EQUAL( listWire[1]._from, string( "both.Out" ) );

    SW_EXPECT_TRUE( fixture._model.removeNode( fixture._both ) );
    SW_EXPECT_TRUE( fixture._model.getWires().empty() );
    SW_EXPECT_FALSE( fixture._model.removeNode( fixture._both ) );
}

SW_TEST_CASE( GimmickGraphModelTest, ParamsRoundTripAndEmptyValueRemoves )
{
    GraphFixture fixture;
    SW_ASSERT_TRUE( fixture._model.setNodeParam( fixture._door, "openTime", "0.5" ) );
    SW_ASSERT_TRUE( fixture._model.setNodeParam( fixture._door, "openOffset", "0 3 0" ) );
    const GimmickGraphNode& door = fixture._model.getNodes()[2];
    SW_EXPECT_EQUAL( door._params, string( "openTime=0.5; openOffset=0 3 0" ) );
    SW_EXPECT_EQUAL( GimmickGraphModel::readParam( door._params, "OPENTIME" ), string( "0.5" ) );
    SW_ASSERT_TRUE( fixture._model.setNodeParam( fixture._door, "openTime", "0.8" ) );
    SW_EXPECT_EQUAL( fixture._model.getNodes()[2]._params, string( "openTime=0.8; openOffset=0 3 0" ) );
    SW_ASSERT_TRUE( fixture._model.setNodeParam( fixture._door, "openTime", "" ) ); // 비우면 지운다(종류의 기본값)
    SW_EXPECT_EQUAL( fixture._model.getNodes()[2]._params, string( "openOffset=0 3 0" ) );
    SW_EXPECT_TRUE( GimmickGraphModel::readParam( "", "x" ).empty() );
}

SW_TEST_CASE( GimmickGraphModelTest, ExportAndPopulateRoundTripKeepsOrphanWires )
{
    vector<GimmickNodeDesc> listNode( 2 );
    listNode[0]._id                     = hashed_string( "plate" );
    listNode[0]._kind                   = hashed_string( "Signal" );
    listNode[1]._id                     = hashed_string( "door" );
    listNode[1]._kind                   = hashed_string( "Door" );
    listNode[1]._params                 = "openTime=0.5";
    vector<GameObjectHandle> listTarget = { GameObjectHandle::make( 7 ) }; // 노드보다 짧은 대상 목록은 빈 대상으로 채운다
    vector<GimmickWireDesc>  listWire( 2 );
    listWire[0]._from    = "plate.Active";
    listWire[0]._to      = "door.Open";
    listWire[0]._bInvert = true;
    listWire[1]._from    = "ghost.Out"; // 노드가 없다
    listWire[1]._to      = "door.Lock";

    GimmickGraphModel model( GimmickNodeRegistry::getDefault() );
    model.populate( listNode, listTarget, listWire );
    SW_EXPECT_EQUAL( model.getNodes().size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( model.getWires().size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( model.getOrphanWires().size(), size_t( 1 ) );
    SW_EXPECT_TRUE( model.getNodes()[0]._target == GameObjectHandle::make( 7 ) );
    SW_EXPECT_FALSE( model.getNodes()[1]._target.isValid() );

    vector<GimmickNodeDesc>  outNode;
    vector<GameObjectHandle> outTarget;
    vector<GimmickWireDesc>  outWire;
    model.exportTo( outNode, outTarget, outWire );
    SW_ASSERT_EQUAL( outNode.size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( outTarget.size(), size_t( 2 ) );
    SW_ASSERT_EQUAL( outWire.size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( outWire[0]._from, string( "plate.Active" ) );
    SW_EXPECT_TRUE( outWire[0]._bInvert );
    SW_EXPECT_EQUAL( outWire[1]._from, string( "ghost.Out" ) ); // 가리키는 노드가 없는 배선도 잃지 않는다

    vector<GimmickGraphIssue> listIssue;
    model.collectIssues( listIssue );
    bool bOrphanReported = false;
    for ( const GimmickGraphIssue& issue : listIssue )
    {
        bOrphanReported = bOrphanReported || issue._message.find( "ghost.Out" ) != string::npos;
    }
    SW_EXPECT_TRUE( bOrphanReported );
    SW_EXPECT_EQUAL( model.removeOrphanWires(), 1 );
}

SW_TEST_CASE( GimmickGraphModelTest, SignatureIgnoresPositionsButSeesEdits )
{
    GraphFixture fixture;
    const uint64 before                           = fixture._model.computeSignature();
    fixture._model.getMutableNodes()[0]._position = float2{ 500.0f, 500.0f };
    SW_EXPECT_EQUAL( fixture._model.computeSignature(), before ); // 끌어도 저장 내용은 같다
    SW_ASSERT_TRUE( fixture._model.setNodeParam( fixture._door, "openTime", "1" ) );
    SW_EXPECT_TRUE( fixture._model.computeSignature() != before );
}

SW_TEST_CASE( GimmickGraphModelTest, IssuesUseTheCircuitValidationAndPointAtTheNode )
{
    GraphFixture fixture;
    SW_ASSERT_TRUE( fixture._model.setNodeParam( fixture._door, "noSuchParam", "1" ) );
    SW_ASSERT_TRUE( fixture._model.setNodeParam( fixture._door, "openTime", "abc" ) ); // 숫자가 아닌 숫자 매개변수
    vector<GimmickGraphIssue> listIssue;
    fixture._model.collectIssues( listIssue );
    SW_ASSERT_TRUE( listIssue.empty() == false );
    for ( const GimmickGraphIssue& issue : listIssue )
    {
        SW_EXPECT_EQUAL( issue._nodeGraphID, fixture._door ); // 문제는 그 노드에 붙는다
    }

    GraphFixture clean;
    SW_ASSERT_TRUE( clean.wire( clean._plate, "Active", clean._both, "A" ) );
    SW_ASSERT_TRUE( clean.wire( clean._both, "Out", clean._door, "Open" ) );
    clean._model.collectIssues( listIssue );
    SW_EXPECT_TRUE( listIssue.empty() );

    // 지연 없는 고리는 회로 검증이 거절한다
    SW_ASSERT_TRUE( clean.wire( clean._door, "Opened", clean._both, "B" ) );
    clean._model.collectIssues( listIssue );
    SW_EXPECT_TRUE( listIssue.empty() == false );
}

SW_TEST_CASE( GimmickGraphModelTest, AutoLayoutPutsEachNodeRightOfItsSources )
{
    GraphFixture fixture;
    SW_ASSERT_TRUE( fixture.wire( fixture._plate, "Active", fixture._both, "A" ) );
    SW_ASSERT_TRUE( fixture.wire( fixture._both, "Out", fixture._door, "Open" ) );
    fixture._model.autoLayout();
    const vector<GimmickGraphNode>& listNode = fixture._model.getNodes();
    SW_EXPECT_TRUE( listNode[0]._position._x < listNode[1]._position._x );
    SW_EXPECT_TRUE( listNode[1]._position._x < listNode[2]._position._x );
    SW_EXPECT_FALSE( listNode[0]._bPlaced );

    // 순환이 있어도 끝난다
    SW_ASSERT_TRUE( fixture.wire( fixture._door, "Opened", fixture._both, "B" ) );
    fixture._model.autoLayout();
    SW_EXPECT_EQUAL( fixture._model.getNodes().size(), size_t( 3 ) );
}

SW_TEST_CASE( GimmickGraphBridgeTest, ApplyWritesTheComponentAndRebuildsTheCircuit )
{
    GameObjectManager        manager;
    GameObject*              pOwner      = manager.createGameObject( hashed_string( "Puzzle" ) );
    GameObject*              pDoorObject = manager.createGameObject( hashed_string( "DoorObject" ) );
    GimmickCircuitComponent* pCircuit    = pOwner->addComponent<GimmickCircuitComponent>();
    pCircuit->addNode( "plate", "Signal", "" );
    pCircuit->addNode( "door", "Door", "openTime=0.5", pDoorObject->getHandle() );
    pCircuit->addWire( "plate.Active", "door.Open" );
    SW_ASSERT_TRUE( pCircuit->rebuild() );

    GimmickGraphModel model( GimmickNodeRegistry::getDefault() );
    SW_ASSERT_TRUE( GimmickGraphBridge::load( *pCircuit, model ) );
    SW_ASSERT_EQUAL( model.getNodes().size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( model.getWires().size(), size_t( 1 ) );
    SW_EXPECT_TRUE( model.getNodes()[1]._target == pDoorObject->getHandle() );
    const uint64 loadedSignature = model.computeSignature();

    // 노드와 배선을 더해 쓰면 컴포넌트의 회로가 새 구조로 다시 지어진다
    const int32 notNode = model.addNode( hashed_string( "Not" ) );
    SW_ASSERT_TRUE( notNode != GimmickGraphModel::kNoGraphID );
    int32  wireID = 0;
    string error;
    SW_ASSERT_TRUE( model.addWire( model.getNodes()[0]._graphID, "Inactive", notNode, "In", wireID, error ) );
    SW_ASSERT_TRUE( GimmickGraphBridge::apply( *pCircuit, model ) );
    SW_EXPECT_TRUE( pCircuit->getErrors().empty() );
    SW_EXPECT_EQUAL( pCircuit->getCircuit().getNodeCount(), 3 );
    SW_EXPECT_TRUE( pCircuit->findNode( hashed_string( "not_1" ) ) >= 0 );

    // 다시 읽으면 같은 모델이고, 서명이 바뀐 것을 알아본다
    GimmickGraphModel reloaded( GimmickNodeRegistry::getDefault() );
    SW_ASSERT_TRUE( GimmickGraphBridge::load( *pCircuit, reloaded ) );
    SW_EXPECT_EQUAL( reloaded.computeSignature(), model.computeSignature() );
    SW_EXPECT_TRUE( reloaded.computeSignature() != loadedSignature );
    SW_EXPECT_TRUE( reloaded.getNodes()[1]._target == pDoorObject->getHandle() ); // 대상은 쓰고 읽어도 같다
}
