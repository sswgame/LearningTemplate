#include "pch.h"

#include "Engine/Resource/ResourceIO.h"

#include "GameFramework/Base/Actor/AI/BehaviorTaskRegistry.h"
#include "GameFramework/Base/Actor/AI/BehaviorTreeAsset.h"
#include "GameFramework/PlugIn/Editor/Base/AI/BehaviorTreeEditModel.h"

#include "TestFramework/TestFramework.h"

// BehaviorTreeEditModelTest — 행동 트리 편집 창의 화면 없는 모델: 읽기 · 쓰기 왕복(로더가 같은 트리를 읽는다), 노드 더하기 · 지우기 · 순서 · 부모 바꾸기 규칙, 저장 전 문제 모으기, 깨진 글 거절.

using namespace sw;
using namespace sw::editor;

namespace
{
    BehaviorStatus editModelTask( BehaviorContext& /*context*/ )
    {
        return BehaviorStatus::Success;
    }

    constexpr const utf8* kEditModelTreeXML = R"(
        <BehaviorTree>
            <Selector name="Root" reactive="true">
                <BlackboardCondition key="Alert" compare="Greater" value="2.5" abort="LowerPriority">
                    <Cooldown seconds="3">
                        <Action name="Fight" task="test.btedit.task"/>
                    </Cooldown>
                </BlackboardCondition>
                <Sequence name="Idle">
                    <MoveTo key="Home" repathDistance="1.5"/>
                    <Repeat count="2"><Wait seconds="0.5"/></Repeat>
                </Sequence>
            </Selector>
        </BehaviorTree>)";
} // namespace

namespace sw
{
    SW_BEHAVIOR_TASK( TestBTEditTask, "test.btedit.task", &editModelTask );
} // namespace sw

/**
 * @brief [BehaviorTreeEditModelTest] 읽은 트리를 되쓴 글을 다시 읽으면 같은 글이고, 로더(`BehaviorTreeAsset`)가 읽은 트리와 노드 수 · 종류 · 깊이 우선 순서가 같다
 * @details 변이: 모델이 `abort` 나 `repathDistance` 를 쓰지 않으면 되읽은 글이 달라지고, 노드 순서를 뒤집어 쓰면 로더의 노드 종류 순서가 어긋난다.
 */
SW_TEST_CASE( BehaviorTreeEditModelTest, RoundTripsAndTheLoaderReadsTheSameTree )
{
    BehaviorTreeEditModel model;
    string                error;
    SW_ASSERT_TRUE_MSG( model.parse( kEditModelTreeXML, error ), error.c_str() );
    SW_EXPECT_EQUAL( 8u, model.getNodeCount() );

    const string          written = model.write();
    BehaviorTreeEditModel reread;
    SW_ASSERT_TRUE_MSG( reread.parse( written, error ), error.c_str() );
    SW_EXPECT_TRUE( reread.write() == written );

    BehaviorTreeAsset original;
    BehaviorTreeAsset fromModel;
    SW_ASSERT_TRUE( original.loadFromXMLText( kEditModelTreeXML, "original" ) );
    SW_ASSERT_TRUE( fromModel.loadFromXMLText( written, "fromModel" ) );
    const BehaviorTree& treeA = original.getTree();
    const BehaviorTree& treeB = fromModel.getTree();
    SW_ASSERT_EQUAL( treeA.getNodeCount(), treeB.getNodeCount() );
    vector<int32> listPreorder;
    model.collectPreorder( listPreorder );
    SW_ASSERT_EQUAL( static_cast<size_t>( treeA.getNodeCount() ), listPreorder.size() );
    for ( int32 index = 0; index < treeA.getNodeCount(); ++index )
    {
        SW_EXPECT_TRUE( treeA.getNode( index )._type == treeB.getNode( index )._type );
        const BehaviorTreeEditNode* pNode = model.find( listPreorder[static_cast<size_t>( index )] );
        SW_ASSERT_TRUE( pNode != nullptr );
        SW_EXPECT_TRUE( pNode->_type == treeA.getNode( index )._type ); // 깊이 우선 번호가 런타임 노드 번호와 같다
    }
}

/**
 * @brief [BehaviorTreeEditModelTest] 지휘관 트리 파일(`game/starskirmish/data/commander.bt.xml`)을 모델이 읽고 되써도 로더가 같은 노드 수를 읽는다
 */
SW_TEST_CASE( BehaviorTreeEditModelTest, CommanderTreeFileSurvivesAnEditRound )
{
    SW_TEST_REQUIRES_FEATURE( SourceResources );
    string fileText;
    SW_ASSERT_TRUE( ResourceIO::readTextResource( sw::ResourceID{ "game/starskirmish/data/commander.bt.xml" }, fileText ) );
    BehaviorTreeEditModel model;
    string                error;
    SW_ASSERT_TRUE_MSG( model.parse( fileText, error ), error.c_str() );
    vector<BehaviorTreeEditIssue> listIssue;
    model.collectIssues( listIssue );
    SW_EXPECT_TRUE( listIssue.empty() ); // 키트가 등록한 rts.* 작업을 다 안다
    BehaviorTreeAsset before;
    BehaviorTreeAsset after;
    SW_ASSERT_TRUE( before.loadFromResource( "game/starskirmish/data/commander.bt.xml" ) );
    SW_ASSERT_TRUE( after.loadFromXMLText( model.write(), "rewritten" ) );
    SW_EXPECT_EQUAL( before.getTree().getNodeCount(), after.getTree().getNodeCount() );
}

/**
 * @brief [BehaviorTreeEditModelTest] 노드 더하기 규칙 - 루트는 하나, 잎 아래와 이미 찬 데코레이터 아래는 거절, 컴포짓은 여럿, 지우면 아래까지 지워지고 루트를 지우면 빈 문서
 */
SW_TEST_CASE( BehaviorTreeEditModelTest, AddAndRemoveFollowTheNodeKindRules )
{
    BehaviorTreeEditModel model;
    const int32           root = model.addNode( -1, BehaviorNodeType::Sequence );
    SW_ASSERT_TRUE( root >= 0 );
    SW_EXPECT_EQUAL( -1, model.addNode( -1, BehaviorNodeType::Selector ) ); // 루트는 하나
    const int32 decorator = model.addNode( root, BehaviorNodeType::Inverter );
    const int32 leaf      = model.addNode( decorator, BehaviorNodeType::Wait );
    SW_ASSERT_TRUE( decorator >= 0 && leaf >= 0 );
    SW_EXPECT_EQUAL( -1, model.addNode( decorator, BehaviorNodeType::Wait ) ); // 데코레이터는 자식 하나
    SW_EXPECT_EQUAL( -1, model.addNode( leaf, BehaviorNodeType::Wait ) );      // 잎은 자식을 못 가진다
    SW_EXPECT_EQUAL( -1, model.addNode( 9999, BehaviorNodeType::Wait ) );      // 없는 부모
    SW_EXPECT_TRUE( model.addNode( root, BehaviorNodeType::Wait ) >= 0 );      // 컴포짓은 여럿
    SW_EXPECT_EQUAL( 4u, model.getNodeCount() );

    SW_EXPECT_TRUE( model.removeNode( decorator ) );
    SW_EXPECT_EQUAL( 2u, model.getNodeCount() ); // 데코레이터와 그 아래 잎이 같이 지워졌다
    SW_EXPECT_TRUE( model.find( leaf ) == nullptr );
    SW_EXPECT_FALSE( model.removeNode( decorator ) ); // 이미 없다
    SW_EXPECT_TRUE( model.removeNode( root ) );
    SW_EXPECT_EQUAL( 0u, model.getNodeCount() );
    SW_EXPECT_EQUAL( -1, model.getRootID() );
    SW_EXPECT_TRUE( model.addNode( -1, BehaviorNodeType::Selector ) >= 0 ); // 비면 새 루트를 둘 수 있다
}

/**
 * @brief [BehaviorTreeEditModelTest] 순서 바꾸기와 부모 바꾸기 - 형제 사이를 끝에서 멈추며 옮기고, 루트 · 제 자손 아래 · 잎 아래 · 찬 데코레이터 아래 · 같은 부모는 거절한다
 */
SW_TEST_CASE( BehaviorTreeEditModelTest, MoveAndReparentRejectBrokenShapes )
{
    BehaviorTreeEditModel model;
    const int32           root = model.addNode( -1, BehaviorNodeType::Selector );
    const int32           a    = model.addNode( root, BehaviorNodeType::Sequence );
    const int32           b    = model.addNode( root, BehaviorNodeType::Sequence );
    const int32           c    = model.addNode( root, BehaviorNodeType::Wait );
    const int32           deco = model.addNode( a, BehaviorNodeType::Inverter );
    const int32           leaf = model.addNode( deco, BehaviorNodeType::Wait );
    SW_ASSERT_TRUE( root >= 0 && a >= 0 && b >= 0 && c >= 0 && deco >= 0 && leaf >= 0 );

    SW_EXPECT_FALSE( model.moveChild( a, -1 ) ); // 이미 맨 앞
    SW_EXPECT_TRUE( model.moveChild( a, 5 ) );   // 끝에서 멈춘다
    SW_EXPECT_EQUAL( a, model.find( root )->_listChild.back() );
    SW_EXPECT_FALSE( model.moveChild( root, 1 ) ); // 루트는 형제가 없다

    SW_EXPECT_FALSE( model.reparent( root, a ) ); // 루트는 못 옮긴다
    SW_EXPECT_FALSE( model.reparent( a, deco ) ); // 제 자손 아래
    SW_EXPECT_FALSE( model.reparent( b, leaf ) ); // 잎 아래
    SW_EXPECT_FALSE( model.reparent( b, deco ) ); // 이미 자식이 있는 데코레이터 아래
    SW_EXPECT_FALSE( model.reparent( b, root ) ); // 같은 부모
    SW_EXPECT_TRUE( model.reparent( c, b ) );     // 컴포짓 아래로
    SW_EXPECT_EQUAL( b, model.find( c )->_parent );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( model.find( b )->_listChild.size() ) );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( model.find( root )->_listChild.size() ) ); // c 가 빠졌다
}

/**
 * @brief [BehaviorTreeEditModelTest] 저장 전 문제 - 루트 없음, 자식 없는 컴포짓, 자식 수가 틀린 데코레이터, 빈 작업 · 없는 작업 이름, 빈 키, 음수 시간
 * @details 변이: 레지스트리를 보지 않으면 `test.btedit.nothing` 줄이 빠져 로더가 거절할 파일을 저장하게 된다.
 */
SW_TEST_CASE( BehaviorTreeEditModelTest, CollectIssuesFindsWhatTheLoaderWouldReject )
{
    BehaviorTreeEditModel         model;
    vector<BehaviorTreeEditIssue> listIssue;
    model.collectIssues( listIssue );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listIssue.size() ) ); // 루트 없음

    const int32 root      = model.addNode( -1, BehaviorNodeType::Selector );
    const int32 deco      = model.addNode( root, BehaviorNodeType::Cooldown ); // 자식 없는 데코레이터
    const int32 action    = model.addNode( root, BehaviorNodeType::Action );   // 작업 이름 빔
    const int32 action2   = model.addNode( root, BehaviorNodeType::Action );
    const int32 moveTo    = model.addNode( root, BehaviorNodeType::MoveTo );   // 키 빔
    const int32 composite = model.addNode( root, BehaviorNodeType::Sequence ); // 자식 없는 컴포짓
    SW_ASSERT_TRUE( deco >= 0 && action >= 0 && action2 >= 0 && moveTo >= 0 && composite >= 0 );
    model.find( action2 )->_task = "test.btedit.nothing";
    const int32 wait             = model.addNode( root, BehaviorNodeType::Wait );
    model.find( wait )->_seconds = -1.0f;
    model.collectIssues( listIssue );
    auto countFor = [&listIssue]( int32 nodeID )
    {
        uint32 count = 0;
        for ( const BehaviorTreeEditIssue& issue : listIssue )
        {
            count += issue._nodeID == nodeID ? 1u : 0u;
        }
        return count;
    };
    SW_EXPECT_EQUAL( 1u, countFor( deco ) );
    SW_EXPECT_EQUAL( 1u, countFor( action ) );
    SW_EXPECT_EQUAL( 1u, countFor( action2 ) );
    SW_EXPECT_EQUAL( 1u, countFor( moveTo ) );
    SW_EXPECT_EQUAL( 1u, countFor( composite ) );
    SW_EXPECT_EQUAL( 1u, countFor( wait ) );

    model.find( action )->_task = "test.btedit.task";
    model.collectIssues( listIssue );
    SW_EXPECT_EQUAL( 0u, countFor( action ) ); // 고르면 문제가 사라진다
}

/**
 * @brief [BehaviorTreeEditModelTest] 깨진 글은 모델을 바꾸지 않고 거절한다 - 모르는 노드 · 속성 · 비교 이름, 잎의 자식, 둘 이상의 루트, 빠진 필수 속성
 */
SW_TEST_CASE( BehaviorTreeEditModelTest, BrokenTextIsRejectedAndKeepsTheModel )
{
    SW_TEST_DEFENSIVE_SCOPE( "the model logs one error per broken attribute value; the texts below are broken on purpose" );
    BehaviorTreeEditModel model;
    string                error;
    SW_ASSERT_TRUE_MSG( model.parse( kEditModelTreeXML, error ), error.c_str() );
    const string before = model.write();

    const utf8* arrBroken[] = {
        "<BehaviorTree><Fly/></BehaviorTree>",
        "<BehaviorTree><Sequence nam=\"x\"><Wait seconds=\"1\"/></Sequence></BehaviorTree>",
        "<BehaviorTree><BlackboardCondition key=\"k\" compare=\"Huge\"><Wait seconds=\"1\"/></BlackboardCondition></BehaviorTree>",
        "<BehaviorTree><Sequence><Wait seconds=\"1\"><Wait seconds=\"1\"/></Wait></Sequence></BehaviorTree>",
        "<BehaviorTree><Wait seconds=\"1\"/><Wait seconds=\"2\"/></BehaviorTree>",
        "<BehaviorTree><Cooldown><Wait seconds=\"1\"/></Cooldown></BehaviorTree>",
        "<BehaviorTree><Inverter><Wait seconds=\"1\"/><Wait seconds=\"2\"/></Inverter></BehaviorTree>",
        "<NotATree/>",
    };
    for ( const utf8* pBroken : arrBroken )
    {
        SW_EXPECT_FALSE_MSG( model.parse( pBroken, error ), pBroken );
        SW_EXPECT_FALSE_MSG( error.empty(), pBroken );
        SW_EXPECT_TRUE_MSG( model.write() == before, pBroken ); // 모델은 그대로다
    }

    // 비어 있는 트리는 새 문서로 열 수 있다(로더는 거절하지만 편집기는 거기서 시작한다).
    SW_EXPECT_TRUE( model.parse( "<BehaviorTree/>", error ) );
    SW_EXPECT_EQUAL( 0u, model.getNodeCount() );
}
