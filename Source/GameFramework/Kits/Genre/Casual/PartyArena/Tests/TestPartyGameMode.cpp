// PartyGameModeTest — 파티 아레나 디렉터: 열기 전 명령 거절, 조작 명령과 상태 왕복, 무대 밖으로 나간 사람의 탈락 · 순위, 라운드 상한에서 끝나기. 봇 입력(nogpu).
#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

#include "GameFramework/Kits/Genre/Casual/PartyArena/Component/PartyGameModeComponent.h"
#include "GameFramework/Kits/Genre/Casual/PartyArena/TrampolineBot.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/Tests/RecordingSoundPlayer.h"

using namespace sw;

namespace
{
    constexpr float32 kPartyFrame     = 1.0f / 60.0f;
    constexpr int32   kMaxPartyFrames = 60 * 60; ///< 라운드 상한까지 하고도 남는 시뮬레이션 시간(프레임) — 벽시계와 무관하다

    constexpr const utf8* kPartyRoundXML = R"(<PartySeries winScore="100" placementPoints="3,2,1"><Round id="trampoline" time="3"/></PartySeries>)";

    /** @brief 3 초 라운드 시리즈를 건 게임 범위와 디렉터 하나입니다. */
    struct PartyGameModeTest
    {
        PartyRoundSeries                  _rounds{};
        RecordingSoundPlayer              _soundPlayer{};
        SubsystemScope                    _scope{};
        unique_ptr<game::ScopedGameScope> _pScopeBinding{};
        GameObjectManager                 _manager{};
        PartyGameModeComponent*           _pGameMode{ nullptr };

        PartyGameModeTest()
        {
            SW_REQUIRE_TRUE( _rounds.loadFromXMLText( kPartyRoundXML, "PartyGameModeTest" ) );
            SW_REQUIRE_TRUE( _scope.bind<ISoundPlayer>( &_soundPlayer ) && _scope.bind<const PartyRoundSeries>( &_rounds ) );
            _pScopeBinding = make_unique<game::ScopedGameScope>( _scope );
            _pGameMode     = _manager.createGameObject( hashed_string( "Director" ) )->addComponent<PartyGameModeComponent>();
            SW_REQUIRE_NOT_NULL( _pGameMode );
            set( "_roundLimit", "2" );
            set( "_intermissionSeconds", "0.5" );
            set( "_resultHoldSeconds", "0.5" );
        }

        void set( const utf8* pProperty, string_view text )
        {
            const PropertyInfo* const pInfo = PartyGameModeComponent::StaticType()->findPropertyInHierarchy( hashed_string( pProperty ) );
            SW_REQUIRE_NOT_NULL( pInfo );
            SW_REQUIRE_TRUE( SerializerUtil::applyPropertyText( *pInfo, MutableObjectAddress{ _pGameMode }, text, SerializeContext::getDefault( SerializePurpose::Authoring ) ) );
        }

        void advance( int32 frameCount )
        {
            for ( int32 frame = 0; frame < frameCount; ++frame )
            {
                _manager.update( kPartyFrame );
            }
        }

        vector<uint8> saveState() const
        {
            Archive saved;
            _pGameMode->writeState( saved );
            vector<uint8> bytes;
            saved.writeData( bytes );
            return bytes;
        }
    };
} // namespace

/** @brief [PartyGameModeTest] 판이 열리기 전의 조작 명령은 거절한다 — 클라이언트가 먼저 와도 판을 바꾸지 않는다 */
SW_TEST_CASE( PartyGameModeTest, CommandsBeforeTheSeriesOpensAreRefused )
{
    GameObjectManager       manager;
    PartyGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<PartyGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    SW_EXPECT_FALSE( pGameMode->isAcceptingInput() );
    SW_EXPECT_FALSE( pGameMode->submitPlayerInput( TrampolineInput{} ) );
    SW_EXPECT_EQUAL( PartyGameModeComponent::kNoWinner, pGameMode->getWinner() );
}

/** @brief [PartyGameModeTest] 사람은 명령으로 움직이고, 쓴 상태를 되살리면 같은 자리로 이어진다 */
SW_TEST_CASE_F( PartyGameModeTest, PlayerCommandsMoveAndTheStateRoundTrips )
{
    _manager.beginPlay();
    SW_REQUIRE_TRUE( _pGameMode->isStarted() );
    const float32   startX = _pGameMode->getPlayerX();
    TrampolineInput input;
    input._moveX = -1.0f;
    for ( int32 frame = 0; frame < 30; ++frame )
    {
        SW_EXPECT_TRUE( _pGameMode->submitPlayerInput( input ) );
        advance( 1 );
    }
    const float32 movedX = _pGameMode->getPlayerX();
    SW_EXPECT_TRUE( movedX < startX - 0.5f );

    vector<uint8> bytes = saveState();
    advance( 30 );
    _pGameMode->restoreState( std::move( bytes ) );
    SW_EXPECT_NEAR( movedX, _pGameMode->getPlayerX(), 1.0e-4f );
    SW_EXPECT_EQUAL( 0, _pGameMode->getRound() );
    _manager.endPlay();
}

/** @brief [PartyGameModeTest] 무대 밖으로 나가면 떨어지고, 라운드 상한까지 하면 우승 점수가 없어도 끝나며, 미니게임을 한 번 끝낸다(흐름이 없으면 오류) */
SW_TEST_CASE_F( PartyGameModeTest, RunningOffFallsAndTheRoundLimitEndsTheSeries )
{
    SW_EXPECT_LOG( "GameMode.FinishWithoutMinigame" );
    set( "_winReward", "50" );
    _manager.beginPlay();
    SW_REQUIRE_TRUE( _pGameMode->isStarted() );
    TrampolineInput input;
    input._moveX = 1.0f;
    for ( int32 frame = 0; frame < kMaxPartyFrames && _pGameMode->getGamePhase() != PartyGamePhase::Finished; ++frame )
    {
        (void)_pGameMode->submitPlayerInput( input ); // 쉬는 시간에는 받지 않는다
        advance( 1 );
    }
    SW_REQUIRE_TRUE( _pGameMode->getGamePhase() == PartyGamePhase::Finished );
    SW_EXPECT_TRUE( _pGameMode->getPlayerRingOutCount() > 0 );
    SW_EXPECT_TRUE( _pGameMode->getPlayerTotal() > 0 ); // 순위 점수는 꼴찌도 받는다
    SW_EXPECT_FALSE( _pGameMode->isAcceptingInput() );
    vector<MinigameResultValue> listValue;
    _pGameMode->collectMinigameResult( listValue );
    SW_EXPECT_EQUAL( _pGameMode->getWinner() == PartyGameModeComponent::kLocalPlayer ? size_t{ 1 } : size_t{ 0 }, listValue.size() );
    advance( 60 ); // 결과 0.5 초를 넘긴다
    _manager.endPlay();
}

/** @brief [PartyGameModeTest] 봇은 가장자리에서 가운데로 돌아오고, 면에 닿은 동안 점프를 누르며, 없는 번호에는 빈 입력을 낸다 */
SW_TEST_CASE( PartyGameModeTest, BotReturnsFromTheEdge )
{
    TrampolineArena arena;
    SW_REQUIRE_TRUE( arena.initialize( TrampolineSettings{}, 2, 0.0f, 0, 5u ) );
    arena.start();
    TrampolineInput push;
    push._moveX = 1.0f; // 사람 0 은 (4, 0) 에서 시작해 +X 로 나간다
    int32 steps = 0;
    while ( arena.findPlayer( 0 )->_position._x < 6.5f && steps < 600 )
    {
        arena.setInput( 0, push );
        arena.step();
        ++steps;
    }
    SW_REQUIRE_TRUE( arena.findPlayer( 0 )->_position._x >= 6.5f );
    SW_EXPECT_TRUE( TrampolineBot::computeInput( arena, 0 )._moveX < 0.0f );
    SW_EXPECT_NEAR( 0.0f, TrampolineBot::computeInput( arena, 7 )._moveX, 1.0e-6f );
}
