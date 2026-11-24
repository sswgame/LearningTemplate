// FightingGameModeTest — 격투 디렉터: 명령은 대전이 열린 동안만 받고, 카탈로그가 없거나 모르는 캐릭터면 열지 않으며, 플레이어의 하단으로 이긴 대전은 결과 값을 내고 상태 바이트를 왕복한다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

#include "GameFramework/Kits/Genre/Action/Fighting/Component/FightingGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameStack.h"

using namespace sw;

namespace
{
    constexpr const utf8* kFightingDirectorMoveXML = R"(<MoveCatalog>
  <Move id="low" startup="3" active="2" recovery="6" damage="10" hitstun="12" blockstun="6" height="Low"><Hitbox x="0.6" y="0.8" w="1.4" h="1.6"/></Move>
</MoveCatalog>)";

    constexpr const utf8* kFightingDirectorCatalogXML = R"(<FighterCatalog buttons="1,2,3,4">
  <Fighter id="basic" name="Basic" health="10" sidestep="u" sidestepDown="d,n" jump="u/f"><Move id="low" command="3"/></Fighter>
</FighterCatalog>)";

    // 상대는 닿는 거리가 짧아 걸어오기만 한다(가드하지 않는다) — 난수와 상관없이 하단 한 방에 진다.
    constexpr const utf8* kFightingDirectorXML = R"(<FightingGameModeComponent _playerFighter="basic" _opponentFighter="basic" _cpuThinkFrames="1" _cpuAttackRange="0.1" _victoryMoney="30">
  <_rules _roundsToWin="1" _startDistance="1.2" />
</FightingGameModeComponent>)";

    constexpr uint8   kNeutralDirection = 5;
    constexpr uint16  kLowButton        = 1 << 2; ///< 버튼 "3"
    constexpr float32 kFrame            = 1.0f / 60.0f;
    constexpr int32   kFrameLimit       = 30;

    /** @brief 카탈로그를 건 게임 범위와 디렉터 하나입니다. */
    struct FightingDirectorTest
    {
        MoveCatalog                _moves{};
        FighterCatalog             _fighters{};
        SubsystemScope             _scope{};
        GameObjectManager          _manager{};
        FightingGameModeComponent* _pGameMode{ nullptr };

        [[nodiscard]] bool open( const utf8* pDirectorXML )
        {
            if ( _moves.loadFromXMLText( kFightingDirectorMoveXML, "FightingGameModeTest" ) == false || _fighters.loadFromXMLText( kFightingDirectorCatalogXML, _moves, "FightingGameModeTest" ) == false )
                return false;
            if ( _scope.bind<const FighterCatalog>( &_fighters ) == false )
                return false;
            _pGameMode = _manager.createGameObject( hashed_string( "Director" ) )->addComponent<FightingGameModeComponent>();
            return _pGameMode != nullptr && XMLSerializer::deserialize( _pGameMode, *FightingGameModeComponent::StaticType(), pDirectorXML );
        }
    };
} // namespace

/** @brief [FightingGameModeTest] 대전이 열리기 전의 입력 명령은 거절한다 — 클라이언트가 먼저 돌아도 대전을 바꾸지 않는다 */
SW_TEST_CASE( FightingGameModeTest, InputBeforeTheMatchOpensIsRefused )
{
    GameObjectManager          manager;
    FightingGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<FightingGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    SW_EXPECT_FALSE( pGameMode->isAcceptingInput() );
    SW_EXPECT_FALSE( pGameMode->submitPlayerInput( kNeutralDirection, kLowButton ) );
    SW_EXPECT_EQUAL( -1, pGameMode->getMatchWinner() );
}

/** @brief [FightingGameModeTest] 캐릭터 카탈로그가 범위에 없으면 오류를 남기고 대전을 열지 않는다 */
SW_TEST_CASE( FightingGameModeTest, MatchWithoutTheCatalogDoesNotOpen )
{
    SW_EXPECT_LOG( "GameMode.BoundCatalogMissing" );
    GameObjectManager          manager;
    FightingGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<FightingGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    manager.beginPlay();
    SW_EXPECT_FALSE( pGameMode->isStarted() );
    manager.endPlay();
}

/** @brief [FightingGameModeTest] 카탈로그에 없는 캐릭터를 적은 씬은 대전을 열지 않는다 */
SW_TEST_CASE( FightingGameModeTest, UnknownFighterDoesNotOpen )
{
    SW_EXPECT_LOG( "Fighting.UnknownFighter" );
    FightingDirectorTest test;
    SW_REQUIRE_TRUE( test.open( R"(<FightingGameModeComponent _playerFighter="basic" _opponentFighter="ghost" />)" ) );
    game::ScopedGameScope scopedScope( test._scope );
    test._manager.beginPlay();
    SW_EXPECT_FALSE( test._pGameMode->isStarted() );
    test._manager.endPlay();
}

/** @brief [FightingGameModeTest] 플레이어의 하단 명령이 걸어오는 상대를 쓰러뜨리면 대전이 끝나고 상금이 결과 값이 되며, 그 판의 상태 바이트가 같은 판으로 되살아난다 */
SW_TEST_CASE( FightingGameModeTest, PlayerLowWinsTheMatchAndReportsTheMoney )
{
    FightingDirectorTest test;
    SW_REQUIRE_TRUE( test.open( kFightingDirectorXML ) );
    game::ScopedGameScope scopedScope( test._scope );
    test._manager.beginPlay();
    SW_REQUIRE_TRUE( test._pGameMode->isStarted() );
    SW_EXPECT_FALSE( test._pGameMode->submitPlayerInput( 0, kLowButton ) ); // 넘패드에 없는 방향
    test._manager.update( kFrame );
    SW_REQUIRE_TRUE( test._pGameMode->submitPlayerInput( kNeutralDirection, kLowButton ) );
    for ( int32 frame = 0; frame < kFrameLimit && test._pGameMode->getMatchWinner() < 0; ++frame )
    {
        test._manager.update( kFrame );
    }
    SW_EXPECT_EQUAL( FightingGameModeComponent::kPlayer, test._pGameMode->getMatchWinner() );
    SW_EXPECT_EQUAL( 0, test._pGameMode->getOpponentHealth() );
    SW_EXPECT_FALSE( test._pGameMode->isAcceptingInput() );

    vector<MinigameResultValue> listValue;
    static_cast<const GameModeComponent*>( test._pGameMode )->collectMinigameResult( listValue );
    SW_REQUIRE_EQUAL( size_t( 1 ), listValue.size() );
    SW_EXPECT_TRUE( listValue[0]._name == hashed_string( MinigameResultValue::kMoneyName ) );
    SW_EXPECT_EQUAL( int64{ 30 }, listValue[0]._amount );

    Archive written;
    test._pGameMode->writeState( written );
    vector<uint8> bytes( written.getData(), written.getData() + written.getSize() );
    test._pGameMode->restoreState( std::move( bytes ) );
    SW_EXPECT_EQUAL( FightingGameModeComponent::kPlayer, test._pGameMode->getMatchWinner() );
    Archive rewritten;
    test._pGameMode->writeState( rewritten );
    SW_EXPECT_EQUAL( written.getSize(), rewritten.getSize() );
    test._manager.endPlay();
}
