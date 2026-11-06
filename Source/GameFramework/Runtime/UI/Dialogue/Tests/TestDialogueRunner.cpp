// DialogueRunnerTest — 대화 러너: 줄 · 선택지 · 조건 분기 · 행동 · 끝, 플래그 명령, 핸들러 안의 재진입, 순환과 끊긴 그래프(nogpu). 그래프 형식과 커서는 DialogueGraphTest 가 본다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Dialogue/DialogueGraphAsset.h"

#include "GameFramework/Runtime/UI/Dialogue/DialogueRunnerComponent.h"
#include "GameFramework/Runtime/World/Query/GameFlags.h"

using namespace sw;

namespace
{
    constexpr int32 kStart  = 1;
    constexpr int32 kLine   = 2;
    constexpr int32 kChoice = 3;
    constexpr int32 kLeft   = 4;
    constexpr int32 kRight  = 5;
    constexpr int32 kBranch = 6;
    constexpr int32 kAction = 7;
    constexpr int32 kEnd    = 8;

    /** @brief 코드로 그래프를 만듭니다. */
    struct DialogueGraphBuilder
    {
        DialogueGraphAsset _asset{};
        int32              _nextLinkID{ 1 };

        DialogueAssetNode& add( int32 id, DialogueAssetNodeType type )
        {
            DialogueAssetNode node{};
            node._id   = id;
            node._type = type;
            _asset._listNode.push_back( node );
            return _asset._listNode.back();
        }

        void connect( int32 from, int32 to ) { _asset._listLink.push_back( DialogueAssetLink{ _nextLinkID++, DialogueGraphAsset::encodePin( from, DialogueGraphAsset::kPinOffsetOut ), DialogueGraphAsset::encodePin( to, DialogueGraphAsset::kPinOffsetIn ) } ); }

        void connectChoice( int32 from, int32 choice, int32 to ) { _asset._listLink.push_back( DialogueAssetLink{ _nextLinkID++, DialogueGraphAsset::encodeChoicePin( from, choice ), DialogueGraphAsset::encodePin( to, DialogueGraphAsset::kPinOffsetIn ) } ); }

        void connectBranch( int32 from, bool bTrue, int32 to )
        {
            const int32 pinOffset = bTrue ? DialogueGraphAsset::kPinOffsetTrue : DialogueGraphAsset::kPinOffsetFalse;
            _asset._listLink.push_back( DialogueAssetLink{ _nextLinkID++, DialogueGraphAsset::encodePin( from, pinOffset ), DialogueGraphAsset::encodePin( to, DialogueGraphAsset::kPinOffsetIn ) } );
        }
    };

    /** @brief Start → 줄 → 선택지 → { 왼쪽 → 끝, 오른쪽(이어지지 않음) } */
    DialogueGraphAsset makeConversation()
    {
        DialogueGraphBuilder builder;
        (void)builder.add( kStart, DialogueAssetNodeType::Start );
        DialogueAssetNode& line                                     = builder.add( kLine, DialogueAssetNodeType::Dialogue );
        line._speaker                                               = "Elder";
        line._text                                                  = "Hello";
        DialogueAssetNode& ask                                      = builder.add( kChoice, DialogueAssetNodeType::Choice );
        ask._text                                                   = "Where?";
        ask._listChoice                                             = { "Left", "Right" };
        builder.add( kLeft, DialogueAssetNodeType::Dialogue )._text = "Went left";
        (void)builder.add( kEnd, DialogueAssetNodeType::End );
        builder.connect( kStart, kLine );
        builder.connect( kLine, kChoice );
        builder.connectChoice( kChoice, 0, kLeft );
        builder.connect( kLeft, kEnd );
        return builder._asset;
    }

    /** @brief 알림을 적어 둡니다. 시험에 따라 받은 자리에서 러너를 더 부릅니다. */
    struct DialogueProbe
    {
        DialogueRunnerComponent* _pRunner{ nullptr };
        vector<string>           _listLine{};
        vector<string>           _listEvent{};
        vector<string>           _listChoice{};
        Subscription             _lineSubscription{};
        Subscription             _choiceSubscription{};
        Subscription             _eventSubscription{};
        Subscription             _finishSubscription{};
        int32                    _finishCount{ 0 };
        int32                    _stopOnEventCount{ 0 };
        uint8                    _bAdvanceOnLine{ SW_FALSE };

        void attach( DialogueRunnerComponent& runner )
        {
            _pRunner            = &runner;
            _lineSubscription   = runner.bindOnDialogueLine( OnDialogueLineDelegate::create<&DialogueProbe::onLine>( this ) );
            _choiceSubscription = runner.bindOnDialogueChoices( OnDialogueChoicesDelegate::create<&DialogueProbe::onChoices>( this ) );
            _eventSubscription  = runner.bindOnDialogueEvent( OnDialogueEventDelegate::create<&DialogueProbe::onEvent>( this ) );
            _finishSubscription = runner.bindOnDialogueFinished( OnDialogueFinishedDelegate::create<&DialogueProbe::onFinished>( this ) );
        }

        void onLine( const string& speaker, const string& text )
        {
            _listLine.push_back( speaker + ":" + text );
            if ( _bAdvanceOnLine == SW_TRUE )
            {
                (void)_pRunner->advance();
                _listLine.push_back( "after:" + text ); // 받은 글은 그대로여야 한다
            }
        }

        void onChoices( const vector<string>& listChoice ) { _listChoice = listChoice; }

        void onEvent( const string& command )
        {
            _listEvent.push_back( command );
            if ( _stopOnEventCount > 0 )
            {
                --_stopOnEventCount;
                _pRunner->stopDialogue();
            }
        }

        void onFinished() { ++_finishCount; }
    };
} // namespace

/** @brief [DialogueRunnerTest] 줄 → 선택지 → 고른 갈래 → 끝. 상태 · 지금 글 · 알림이 차례로 나온다 */
SW_TEST_CASE( DialogueRunnerTest, WalksLinesAndChoices )
{
    DialogueRunnerComponent runner;
    DialogueProbe           probe;
    probe.attach( runner );
    runner.setGraph( makeConversation() );
    SW_EXPECT_TRUE( runner.getState() == DialogueRunnerState::Idle );
    SW_EXPECT_FALSE( runner.advance() );
    SW_EXPECT_FALSE( runner.selectChoice( 0 ) );

    SW_REQUIRE_TRUE( runner.startDialogue() );
    SW_EXPECT_TRUE( runner.getState() == DialogueRunnerState::ShowingDialogue );
    SW_EXPECT_EQUAL( kLine, runner.getCurrentNodeID() );
    SW_EXPECT_EQUAL( string( "Hello" ), runner.getCurrentText() );
    SW_REQUIRE_EQUAL( size_t( 1 ), probe._listLine.size() );
    SW_EXPECT_EQUAL( string( "Elder:Hello" ), probe._listLine[0] );
    SW_EXPECT_FALSE( runner.selectChoice( 0 ) ); // 아직 선택지가 아니다

    SW_REQUIRE_TRUE( runner.advance() );
    SW_EXPECT_TRUE( runner.getState() == DialogueRunnerState::WaitingForChoice );
    SW_EXPECT_EQUAL( size_t( 2 ), probe._listChoice.size() );
    SW_EXPECT_EQUAL( size_t( 2 ), runner.getCurrentChoices().size() );
    SW_EXPECT_FALSE( runner.advance() ); // 선택을 기다린다

    SW_REQUIRE_TRUE( runner.selectChoice( 0 ) );
    SW_EXPECT_EQUAL( string( "Went left" ), runner.getCurrentText() );
    SW_REQUIRE_TRUE( runner.advance() );
    SW_EXPECT_TRUE( runner.getState() == DialogueRunnerState::Finished );
    SW_EXPECT_EQUAL( 1, probe._finishCount );
}

/** @brief [DialogueRunnerTest] 없는 선택지 번호는 거절하고 선택을 계속 기다린다. 이어지지 않은 선택지는 대화를 끝낸다 */
SW_TEST_CASE( DialogueRunnerTest, BadChoiceKeepsWaiting )
{
    DialogueRunnerComponent runner;
    DialogueProbe           probe;
    probe.attach( runner );
    runner.setGraph( makeConversation() );
    SW_REQUIRE_TRUE( runner.startDialogue( kChoice ) );
    SW_REQUIRE_TRUE( runner.getState() == DialogueRunnerState::WaitingForChoice );

    SW_EXPECT_FALSE( runner.selectChoice( 2 ) );
    SW_EXPECT_FALSE( runner.selectChoice( -1 ) );
    SW_EXPECT_TRUE( runner.getState() == DialogueRunnerState::WaitingForChoice );
    SW_EXPECT_EQUAL( 0, probe._finishCount );

    SW_EXPECT_FALSE( runner.selectChoice( 1 ) ); // 있는 선택지인데 잇지 않았다
    SW_EXPECT_TRUE( runner.getState() == DialogueRunnerState::Idle );
    SW_EXPECT_EQUAL( 1, probe._finishCount );
}

/** @brief [DialogueRunnerTest] 조건 분기는 월드 플래그로 갈린다. 플래그가 없으면 모든 이름이 0 이다 */
SW_TEST_CASE( DialogueRunnerTest, BranchReadsFlags )
{
    DialogueGraphBuilder builder;
    (void)builder.add( kStart, DialogueAssetNodeType::Start );
    builder.add( kBranch, DialogueAssetNodeType::Branch )._condition = "hasKey";
    builder.add( kLeft, DialogueAssetNodeType::Dialogue )._text      = "yes";
    builder.add( kRight, DialogueAssetNodeType::Dialogue )._text     = "no";
    builder.connect( kStart, kBranch );
    builder.connectBranch( kBranch, true, kLeft );
    builder.connectBranch( kBranch, false, kRight );

    DialogueRunnerComponent runner;
    runner.setGraph( builder._asset );
    SW_REQUIRE_TRUE( runner.startDialogue() );
    SW_EXPECT_EQUAL( string( "no" ), runner.getCurrentText() );

    GameFlags flags;
    flags.setFlag( "hasKey" );
    runner.setFlags( &flags );
    SW_REQUIRE_TRUE( runner.startDialogue() );
    SW_EXPECT_EQUAL( string( "yes" ), runner.getCurrentText() );
}

/** @brief [DialogueRunnerTest] 행동: 알림을 내고 `set_flag:` 를 적용한다. 값이 없으면 1, 0 은 지우기, 숫자가 아니면 경고하고 1 이다 */
SW_TEST_CASE( DialogueRunnerTest, ActionsSetFlags )
{
    SW_EXPECT_LOG( "DialogueRunnerComponent.DialogueActionSetsValue" );
    DialogueGraphBuilder builder;
    (void)builder.add( kStart, DialogueAssetNodeType::Start );
    builder.add( 10, DialogueAssetNodeType::Action )._actionCommand = "set_flag:plain";
    builder.add( 11, DialogueAssetNodeType::Action )._actionCommand = "set_flag:count:5";
    builder.add( 12, DialogueAssetNodeType::Action )._actionCommand = "set_flag:gone:0";
    builder.add( 13, DialogueAssetNodeType::Action )._actionCommand = "set_flag:odd:abc";
    builder.add( 14, DialogueAssetNodeType::Action )._actionCommand = "play_sound";
    (void)builder.add( kEnd, DialogueAssetNodeType::End );
    builder.connect( kStart, 10 );
    builder.connect( 10, 11 );
    builder.connect( 11, 12 );
    builder.connect( 12, 13 );
    builder.connect( 13, 14 );
    builder.connect( 14, kEnd );

    DialogueRunnerComponent runner;
    DialogueProbe           probe;
    probe.attach( runner );
    GameFlags flags;
    flags.setFlag( "gone" );
    runner.setFlags( &flags );
    runner.setGraph( builder._asset );
    SW_REQUIRE_TRUE( runner.startDialogue() );

    SW_EXPECT_EQUAL( size_t( 5 ), probe._listEvent.size() );
    SW_EXPECT_EQUAL( 1, flags.getFlag( "plain" ) );
    SW_EXPECT_EQUAL( 5, flags.getFlag( "count" ) );
    SW_EXPECT_FALSE( flags.hasFlag( "gone" ) );
    SW_EXPECT_EQUAL( 1, flags.getFlag( "odd" ) );
    SW_EXPECT_TRUE( runner.getState() == DialogueRunnerState::Finished );
}

/** @brief [DialogueRunnerTest] 행동 핸들러가 대화를 멈추면 뒤의 노드를 실행하지 않는다 */
SW_TEST_CASE( DialogueRunnerTest, HandlerMayStopDuringAction )
{
    DialogueGraphBuilder builder;
    (void)builder.add( kStart, DialogueAssetNodeType::Start );
    builder.add( kAction, DialogueAssetNodeType::Action )._actionCommand = "cut";
    builder.add( kLine, DialogueAssetNodeType::Dialogue )._text          = "never";
    builder.connect( kStart, kAction );
    builder.connect( kAction, kLine );

    DialogueRunnerComponent runner;
    DialogueProbe           probe;
    probe.attach( runner );
    probe._stopOnEventCount = 1;
    runner.setGraph( builder._asset );
    SW_REQUIRE_TRUE( runner.startDialogue() );
    SW_EXPECT_TRUE( runner.getState() == DialogueRunnerState::Idle );
    SW_EXPECT_TRUE( probe._listLine.empty() );
}

/** @brief [DialogueRunnerTest] 줄 핸들러가 바로 다음으로 넘겨도 받은 글은 그대로다 */
SW_TEST_CASE( DialogueRunnerTest, HandlerMayAdvanceDuringLine )
{
    DialogueRunnerComponent runner;
    DialogueProbe           probe;
    probe.attach( runner );
    probe._bAdvanceOnLine = SW_TRUE;
    runner.setGraph( makeConversation() );
    SW_REQUIRE_TRUE( runner.startDialogue() );
    SW_EXPECT_TRUE( runner.getState() == DialogueRunnerState::WaitingForChoice );
    SW_REQUIRE_EQUAL( size_t( 2 ), probe._listLine.size() );
    SW_EXPECT_EQUAL( string( "after:Hello" ), probe._listLine[1] );
}

/** @brief [DialogueRunnerTest] 끊긴 그래프 · 순환 · 빈 그래프는 멈춘 채 남지 않는다 */
SW_TEST_CASE( DialogueRunnerTest, BrokenGraphsFinish )
{
    SW_EXPECT_LOG( "DialogueRunnerComponent.DialogueRunnerNoValidNodes" );
    SW_EXPECT_LOG( "DialogueRunnerComponent.NodeNotFoundGraph" );
    SW_EXPECT_LOG( "DialogueRunnerComponent.DialogueRunnerCyclicNodeTransition" );

    DialogueRunnerComponent empty;
    SW_EXPECT_FALSE( empty.startDialogue() );

    DialogueRunnerComponent runner;
    DialogueProbe           probe;
    probe.attach( runner );
    runner.setGraph( makeConversation() );
    SW_REQUIRE_TRUE( runner.startDialogue( 999 ) ); // 없는 노드
    SW_EXPECT_TRUE( runner.getState() == DialogueRunnerState::Finished );

    DialogueGraphBuilder loop;
    (void)loop.add( kStart, DialogueAssetNodeType::Start );
    loop.add( 10, DialogueAssetNodeType::Action )._actionCommand = "a";
    loop.add( 11, DialogueAssetNodeType::Action )._actionCommand = "b";
    loop.connect( kStart, 10 );
    loop.connect( 10, 11 );
    loop.connect( 11, 10 );
    runner.setGraph( loop._asset );
    SW_REQUIRE_TRUE( runner.startDialogue() );
    SW_EXPECT_TRUE( runner.getState() == DialogueRunnerState::Finished );
}

/** @brief [DialogueRunnerTest] 멈추면 지금 글이 비고 끝 알림은 대화 중일 때만 한 번 나간다 */
SW_TEST_CASE( DialogueRunnerTest, StopClearsAndNotifiesOnce )
{
    DialogueRunnerComponent runner;
    DialogueProbe           probe;
    probe.attach( runner );
    runner.setGraph( makeConversation() );
    runner.stopDialogue();
    SW_EXPECT_EQUAL( 0, probe._finishCount );

    SW_REQUIRE_TRUE( runner.startDialogue() );
    runner.stopDialogue();
    SW_EXPECT_EQUAL( 1, probe._finishCount );
    SW_EXPECT_TRUE( runner.getCurrentText().empty() );
    SW_EXPECT_EQUAL( 0, runner.getCurrentNodeID() );
    runner.stopDialogue();
    SW_EXPECT_EQUAL( 1, probe._finishCount );
}
