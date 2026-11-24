// MetroidvaniaGameModeTest — 메트로배니아 디렉터: 명령은 판이 열린 동안만 받고, 카탈로그가 없거나 배치가 틀리면 열지 않으며, 쉬기 · 줍기 · 보스 격파로 이기고 통화를 결과 값으로 내며, 상태 바이트를 왕복하고, 죽으면 쉰 곳에서 되살아난다(nogpu).
#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Component/MetroidvaniaGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameStack.h"

using namespace sw;

namespace
{
    constexpr const utf8* kMetroidvaniaDirectorCatalogXML = R"(<Metroidvania><BaseMotor halfExtents="0.35,0.45" runSpeed="7" groundAcceleration="70" airAcceleration="40" jumpHeight="3.2" timeToApex="0.38" fallGravityScale="1.6" jumpCutRatio="0.45" maxFallSpeed="18" coyoteTime="0.1" jumpBufferTime="0.12" wallSlideSpeed="2.5" wallJumpSpeedX="7.5" wallJumpSpeedY="12" wallJumpLockTime="0.15" dashSpeed="18" dashDuration="0.15" dashCooldown="0.3" climbSpeed="4" dropThroughTime="0.25" dashEndSpeedScale="0.4" climbJumpScale="0.8" climbSideScale="0.5" moveDeadzone="0.1" extraJumpCount="0" airDashCount="1" wallJump="true"/>
  <Health max="10"/>
  <Parry><Window grade="Perfect" early="0.1" late="0.1"/></Parry>
  <Ability id="wings" name="Wings" flag="ability_wings"><Motor extraJumpCount="1"/></Ability>
  <Site id="bench" area="hall" rest="true"/>
  <Pickup id="wings" area="hall" ability="wings"/>
  <Enemy id="warden" boss="true" flag="boss_warden" currency="60"/>
</Metroidvania>)";

    // 시작 자리에 쉬는 곳 · 날개가 있고 보스가 사거리 안 오른쪽에 선다. 보스는 준비 동작이 길어 한 방 전에 쓰러진다.
    constexpr const utf8* kMetroidvaniaDirectorXML = R"(<MetroidvaniaGameModeComponent _areaDataPath="game/playground/metroidvania/data/caverns.areas.xml" _startPosition="2.5,1.5" _goalFlag="boss_warden" _playerDamage="10" _playerReach="2" _bossDamageTakenScale="1" _enemyStartup="100">
  <_listTerrainRow><item>##########</item><item>#........#</item><item>#........#</item><item>##########</item></_listTerrainRow>
  <_listEnemyKind><item>warden</item></_listEnemyKind>
  <_listEnemyPosition><item>4,1.5</item></_listEnemyPosition>
  <_listPickup><item>wings</item></_listPickup>
  <_listPickupPosition><item>2.5,1.5</item></_listPickupPosition>
  <_listSite><item>bench</item></_listSite>
  <_listSitePosition><item>2.5,1.5</item></_listSitePosition>
</MetroidvaniaGameModeComponent>)";

    constexpr float32 kFrame = 1.0f / 60.0f;

    /** @brief 카탈로그를 건 게임 범위와 디렉터 하나입니다. */
    struct MetroidvaniaDirectorTest
    {
        MetroidvaniaCatalog            _catalog{};
        SubsystemScope                 _scope{};
        GameObjectManager              _manager{};
        MetroidvaniaGameModeComponent* _pGameMode{ nullptr };

        [[nodiscard]] bool open( const utf8* pDirectorXML )
        {
            if ( _catalog.loadFromXMLText( kMetroidvaniaDirectorCatalogXML, "MetroidvaniaGameModeTest" ) == false || _scope.bind<const MetroidvaniaCatalog>( &_catalog ) == false )
                return false;
            _pGameMode = _manager.createGameObject( hashed_string( "Director" ) )->addComponent<MetroidvaniaGameModeComponent>();
            return _pGameMode != nullptr && XMLSerializer::deserialize( _pGameMode, *MetroidvaniaGameModeComponent::StaticType(), pDirectorXML );
        }

        void run( int32 frameCount )
        {
            for ( int32 frame = 0; frame < frameCount; ++frame )
            {
                _manager.update( kFrame );
            }
        }
    };
} // namespace

/** @brief [MetroidvaniaGameModeTest] 판이 열리기 전의 입력 명령은 거절한다 — 클라이언트가 먼저 돌아도 판을 바꾸지 않는다 */
SW_TEST_CASE( MetroidvaniaGameModeTest, InputBeforeTheGameOpensIsRefused )
{
    GameObjectManager              manager;
    MetroidvaniaGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<MetroidvaniaGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    SW_EXPECT_FALSE( pGameMode->isAcceptingInput() );
    SW_EXPECT_FALSE( pGameMode->submitPlayerInput( MetroidvaniaPlayerCommand{} ) );
    SW_EXPECT_EQUAL( 0, pGameMode->getCurrency() );
}

/** @brief [MetroidvaniaGameModeTest] 규칙 카탈로그가 범위에 없으면 오류를 남기고 판을 열지 않는다 */
SW_TEST_CASE( MetroidvaniaGameModeTest, GameWithoutTheCatalogDoesNotOpen )
{
    SW_EXPECT_LOG( "GameMode.BoundCatalogMissing" );
    GameObjectManager              manager;
    MetroidvaniaGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<MetroidvaniaGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    manager.beginPlay();
    SW_EXPECT_FALSE( pGameMode->isStarted() );
    manager.endPlay();
}

/** @brief [MetroidvaniaGameModeTest] 배치 목록의 길이가 다른 씬은 판을 열지 않는다 */
SW_TEST_CASE( MetroidvaniaGameModeTest, MismatchedPlacementDoesNotOpen )
{
    SW_EXPECT_LOG( "Metroidvania.LayoutMismatch" );
    MetroidvaniaDirectorTest test;
    SW_REQUIRE_TRUE( test.open( R"(<MetroidvaniaGameModeComponent _areaDataPath="game/playground/metroidvania/data/caverns.areas.xml" _goalFlag="boss_warden">
  <_listEnemyKind><item>warden</item></_listEnemyKind>
</MetroidvaniaGameModeComponent>)" ) );
    game::ScopedGameScope scopedScope( test._scope );
    test._manager.beginPlay();
    SW_EXPECT_FALSE( test._pGameMode->isStarted() );
    test._manager.endPlay();
}

/** @brief [MetroidvaniaGameModeTest] 시작 자리의 날개를 줍고, 쉬고, 앞의 보스를 한 번 베어 이기면 통화가 결과 값이 된다. 이기기 전의 상태 바이트는 같은 판으로 되살아난다 */
SW_TEST_CASE( MetroidvaniaGameModeTest, RestPickupAndBossWinTheGame )
{
    MetroidvaniaDirectorTest test;
    SW_REQUIRE_TRUE( test.open( kMetroidvaniaDirectorXML ) );
    game::ScopedGameScope scopedScope( test._scope );
    test._manager.beginPlay();
    SW_REQUIRE_TRUE( test._pGameMode->isStarted() );
    SW_EXPECT_EQUAL( 1, test._pGameMode->getAbilityCount() ); // 시작 자리의 날개
    SW_EXPECT_EQUAL( 1, test._pGameMode->getVisitedAreaCount() );
    SW_EXPECT_FALSE( test._pGameMode->isPickupLeft( 0 ) );
    SW_EXPECT_TRUE( test._pGameMode->isEnemyAlive( 0 ) );

    MetroidvaniaPlayerCommand use;
    use._bUsePressed = SW_TRUE;
    SW_REQUIRE_TRUE( test._pGameMode->submitPlayerInput( use ) );
    test.run( 3 );
    SW_EXPECT_TRUE( test._pGameMode->getDeathRest().getRespawnSite() == hashed_string( "bench" ) );

    Archive written;
    test._pGameMode->writeState( written );
    vector<uint8> bytes( written.getData(), written.getData() + written.getSize() );
    test._pGameMode->restoreState( std::move( bytes ) );
    SW_EXPECT_EQUAL( 1, test._pGameMode->getAbilityCount() );
    SW_EXPECT_EQUAL( 1, test._pGameMode->getVisitedAreaCount() );
    SW_EXPECT_TRUE( test._pGameMode->getDeathRest().getRespawnSite() == hashed_string( "bench" ) );
    Archive rewritten;
    test._pGameMode->writeState( rewritten );
    SW_EXPECT_EQUAL( written.getSize(), rewritten.getSize() );

    MetroidvaniaPlayerCommand attack;
    attack._bAttackPressed = SW_TRUE;
    SW_REQUIRE_TRUE( test._pGameMode->submitPlayerInput( attack ) );
    test.run( 3 );
    SW_EXPECT_FALSE( test._pGameMode->isEnemyAlive( 0 ) );
    SW_EXPECT_EQUAL( static_cast<int32>( MetroidvaniaPhase::Won ), test._pGameMode->getPhase() );
    SW_EXPECT_FALSE( test._pGameMode->isAcceptingInput() );
    vector<MinigameResultValue> listValue;
    static_cast<const GameModeComponent*>( test._pGameMode )->collectMinigameResult( listValue );
    SW_REQUIRE_EQUAL( size_t( 1 ), listValue.size() );
    SW_EXPECT_EQUAL( int64{ 60 }, listValue[0]._amount );
    test._manager.endPlay();
}

/** @brief [MetroidvaniaGameModeTest] 보스에게 맞아 죽으면 기다린 뒤 쉰 곳에서 되살아나고 보스는 그대로 서 있다 */
SW_TEST_CASE( MetroidvaniaGameModeTest, DeathReturnsToTheRestedSite )
{
    MetroidvaniaDirectorTest test;
    SW_REQUIRE_TRUE( test.open( kMetroidvaniaDirectorXML ) );
    SW_REQUIRE_TRUE( XMLSerializer::deserialize( test._pGameMode, *MetroidvaniaGameModeComponent::StaticType(),
                                                 R"(<MetroidvaniaGameModeComponent _enemyStartup="0.1" _bossDamage="1000" _respawnDelaySeconds="0.2" />)" ) );
    game::ScopedGameScope scopedScope( test._scope );
    test._manager.beginPlay();
    SW_REQUIRE_TRUE( test._pGameMode->isStarted() );
    MetroidvaniaPlayerCommand use;
    use._bUsePressed = SW_TRUE;
    SW_REQUIRE_TRUE( test._pGameMode->submitPlayerInput( use ) );
    test.run( 2 );
    for ( int32 frame = 0; frame < 60 && test._pGameMode->getExplorePhase() != MetroidvaniaPhase::Dead; ++frame )
    {
        test.run( 1 );
    }
    SW_REQUIRE_TRUE( test._pGameMode->getExplorePhase() == MetroidvaniaPhase::Dead );
    SW_EXPECT_TRUE( test._pGameMode->isEnemyAlive( 0 ) );
    for ( int32 frame = 0; frame < 60 && test._pGameMode->getExplorePhase() == MetroidvaniaPhase::Dead; ++frame )
    {
        test.run( 1 );
    }
    SW_EXPECT_TRUE( test._pGameMode->getExplorePhase() == MetroidvaniaPhase::Explore );
    SW_EXPECT_NEAR( 2.5f, test._pGameMode->getPlayerX(), 0.05f );
    SW_EXPECT_EQUAL( 10.0f, test._pGameMode->getPlayer().getHealthMachine().getHealth() );
    test._manager.endPlay();
}
