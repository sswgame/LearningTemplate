// BattleRoyaleGameModeTest — 배틀로얄 디렉터: 명령은 판이 열린 동안만 받고, 카탈로그가 없으면 열지 않으며, 낙하 → 전투 → 구역이 점이 되어 판이 끝나는 한 바퀴를 돌고 도중 상태 바이트를 왕복한다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Component/BattleRoyaleGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

using namespace sw;

namespace
{
    // 구역은 곧바로 점이 되어 원 밖 피해(초당 100)로 판이 끝난다 — 사격(명중 0)과 상관없다.
    constexpr const utf8* kBattleRoyaleDirectorXML = R"(<BattleRoyaleCatalog mapSize="50">
  <Player health="50" downedHealth="20" bleedout="10" reviveTime="2" slots="2"/>
  <Flight speed="50" altitude="20" jumpStart="0" jumpEnd="0.5" offset="0.2"/>
  <Fall freeFallSpeed="40" freeFallHorizontal="5" parachuteSpeed="20" parachuteHorizontal="5" autoOpenHeight="5"/>
  <Zone startRadius="0"><Phase wait="0.5" shrink="0.5" ratio="0" damage="100"/></Zone>
</BattleRoyaleCatalog>)";

    constexpr const utf8* kBattleRoyaleGameModeXML = R"(<BattleRoyaleGameModeComponent _squadCount="2" _squadSize="1" _seed="7" _playerHitChance="0" _botHitChance="0" />)";

    constexpr float32 kBattleRoyaleGameModeFrame      = 1.0f / 60.0f;
    constexpr int32   kBattleRoyaleGameModeFrameLimit = 600;

    /** @brief 카탈로그를 건 게임 범위와 디렉터 하나입니다. */
    struct BattleRoyaleDirectorTest
    {
        BattleRoyaleCatalog            _catalog{};
        SubsystemScope                 _scope{};
        GameObjectManager              _manager{};
        BattleRoyaleGameModeComponent* _pGameMode{ nullptr };

        [[nodiscard]] bool open()
        {
            if ( _catalog.loadFromXMLText( kBattleRoyaleDirectorXML, "BattleRoyaleGameModeTest" ) == false || _scope.bind<const BattleRoyaleCatalog>( &_catalog ) == false )
                return false;
            _pGameMode = _manager.createGameObject( hashed_string( "Director" ) )->addComponent<BattleRoyaleGameModeComponent>();
            return _pGameMode != nullptr && XMLSerializer::deserialize( _pGameMode, *BattleRoyaleGameModeComponent::StaticType(), kBattleRoyaleGameModeXML );
        }

        /** @brief 단계가 @p phase 에 닿을 때까지 프레임을 돌립니다. 상한 안에 닿았으면 true 입니다. */
        [[nodiscard]] bool runUntil( BattleRoyalePhase phase )
        {
            for ( int32 frame = 0; frame < kBattleRoyaleGameModeFrameLimit && _pGameMode->getMatchPhase() != phase; ++frame )
            {
                _manager.update( kBattleRoyaleGameModeFrame );
            }
            return _pGameMode->getMatchPhase() == phase;
        }
    };
} // namespace

/** @brief [BattleRoyaleGameModeTest] 판이 열리기 전의 입력 명령은 거절한다 — 클라이언트가 먼저 돌아도 판을 바꾸지 않는다 */
SW_TEST_CASE( BattleRoyaleGameModeTest, InputBeforeTheMatchOpensIsRefused )
{
    GameObjectManager              manager;
    BattleRoyaleGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<BattleRoyaleGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    SW_EXPECT_FALSE( pGameMode->isAcceptingInput() );
    SW_EXPECT_FALSE( pGameMode->submitPlayerInput( float2{ 1.0f, 0.0f }, true, true, true ) );
    SW_EXPECT_EQUAL( 0, pGameMode->getRemaining() );
}

/** @brief [BattleRoyaleGameModeTest] 규칙 카탈로그가 범위에 없으면 오류를 남기고 판을 열지 않는다 */
SW_TEST_CASE( BattleRoyaleGameModeTest, MatchWithoutTheCatalogDoesNotOpen )
{
    SW_EXPECT_LOG( "GameMode.BoundCatalogMissing" );
    GameObjectManager              manager;
    BattleRoyaleGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<BattleRoyaleGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    manager.beginPlay();
    SW_EXPECT_FALSE( pGameMode->isStarted() );
    manager.endPlay();
}

/** @brief [BattleRoyaleGameModeTest] 쓰기 명령으로 비행기에서 내리고, 모두 닿으면 전투가 시작되며, 구역이 점이 되어 마지막 분대가 남으면 판이 끝난다 */
SW_TEST_CASE( BattleRoyaleGameModeTest, DropThenZoneEndsTheMatch )
{
    BattleRoyaleDirectorTest test;
    SW_REQUIRE_TRUE( test.open() );
    game::ScopedGameScope scopedScope( test._scope );
    test._manager.beginPlay();
    SW_REQUIRE_TRUE( test._pGameMode->isStarted() );
    SW_EXPECT_EQUAL( 0, test._pGameMode->getPlayerDropStage() );
    SW_EXPECT_TRUE( test._pGameMode->getPersonPosition( BattleRoyaleGameModeComponent::kPlayer )._value._y > 0.0f );
    test._manager.update( kBattleRoyaleGameModeFrame );
    SW_REQUIRE_TRUE( test._pGameMode->submitPlayerInput( float2{ 0.0f, 0.0f }, false, false, true ) );
    for ( int32 frame = 0; frame < 2; ++frame )
    {
        test._manager.update( kBattleRoyaleGameModeFrame );
    }
    SW_EXPECT_EQUAL( 1, test._pGameMode->getPlayerDropStage() );

    // 낙하 도중의 상태 바이트가 같은 판으로 되살아난다.
    Archive written;
    test._pGameMode->writeState( written );
    vector<uint8> bytes( written.getData(), written.getData() + written.getSize() );
    test._pGameMode->restoreState( std::move( bytes ) );
    SW_EXPECT_EQUAL( 1, test._pGameMode->getPlayerDropStage() );
    SW_EXPECT_EQUAL( 2, test._pGameMode->getRemaining() );
    Archive rewritten;
    test._pGameMode->writeState( rewritten );
    SW_EXPECT_EQUAL( written.getSize(), rewritten.getSize() );

    SW_REQUIRE_TRUE( test.runUntil( BattleRoyalePhase::Battle ) );
    SW_EXPECT_EQUAL( 2, test._pGameMode->getPlayerDropStage() );
    SW_EXPECT_EQUAL( 0.0f, test._pGameMode->getPersonPosition( BattleRoyaleGameModeComponent::kPlayer )._value._y );
    SW_REQUIRE_TRUE( test.runUntil( BattleRoyalePhase::Ended ) );
    SW_EXPECT_FALSE( test._pGameMode->isAcceptingInput() );
    SW_EXPECT_TRUE( test._pGameMode->getRemaining() <= 1 );
    test._manager.endPlay();
}
