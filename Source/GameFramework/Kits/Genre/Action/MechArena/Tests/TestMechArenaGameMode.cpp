// MechArenaGameModeTest — 기체 대전 디렉터: 명령은 판이 열린 동안만 받고, 카탈로그가 없거나 모르는 기체면 열지 않으며, 플레이어의 사격으로 적 게이지가 바닥나면 이겨 결과 값을 내고 상태 바이트를 왕복한다(nogpu).
#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

#include "GameFramework/Kits/Genre/Action/MechArena/Component/MechArenaGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Foundation/Presentation/Tests/RecordingSoundPlayer.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

using namespace sw;

namespace
{
    constexpr const utf8* kMechDirectorWeaponXML = R"(<WeaponCatalog>
  <Weapon id="rifle" fireSound="RifleFire" projectile="beam" accepts="Ammo.Beam" fireInterval="0.1" reloadTime="0.2" minSpread="0" maxSpread="0" spreadPerShot="0" magazineSize="3" maxReserveAmmo="3"/>
</WeaponCatalog>)";

    constexpr const utf8* kMechDirectorProjectileXML = R"(<ProjectileCatalog>
  <Projectile id="beam" tags="Ammo.Beam" damage="10" range="100" speed="0" gravityScale="0"/>
</ProjectileCatalog>)";

    constexpr const utf8* kMechDirectorCatalogXML = R"(<MechCatalog>
  <Mech id="gunner" name="Gunner" cost="100" hp="100" lockOnBreak="1.2"><Weapon id="rifle" kind="Shot" weapon="rifle" down="0"/></Mech>
  <Mech id="fencer" name="Fencer" cost="100" hp="100" lockOnBreak="1.2"><Weapon id="saber" kind="Melee" moves="mech_slash,mech_cleave" range="5" down="0"/></Mech>
  <Mech id="target" name="Target" cost="100" hp="10" lockOnBreak="1.2"/>
</MechCatalog>)";

    // 적은 무기가 없고 제자리(가까이 붙지 않는 거리)라 한 발에 격추된다 — 게이지 100 이 코스트 100 에 바닥난다.
    constexpr const utf8* kMechDirectorXML = R"(<MechArenaGameModeComponent _moveDataPath="game/playground/mecharena/data/mecharena.moves.xml" _playerSpawn="0,0,0" _botDashChance="0" _victoryMoney="50">
  <_rules _teamGauge="100" />
  <_listPlayerMech><item>gunner</item></_listPlayerMech>
  <_listEnemyMech><item>target</item></_listEnemyMech>
  <_listEnemySpawn><item>0,0,10</item></_listEnemySpawn>
</MechArenaGameModeComponent>)";

    // 적이 닿는 거리 안에 서 있어 한 번 베면 격추된다. 기술의 소리는 게임의 기술 표다.
    constexpr const utf8* kMechMeleeDirectorXML = R"(<MechArenaGameModeComponent _moveDataPath="game/playground/mecharena/data/mecharena.moves.xml" _playerSpawn="0,0,0" _botDashChance="0" _victoryMoney="50">
  <_rules _teamGauge="100" />
  <_listPlayerMech><item>fencer</item></_listPlayerMech>
  <_listEnemyMech><item>target</item></_listEnemyMech>
  <_listEnemySpawn><item>0,0,3</item></_listEnemySpawn>
</MechArenaGameModeComponent>)";

    constexpr float32 kMechArenaGameModeFrame      = 1.0f / 60.0f;
    constexpr int32   kMechArenaGameModeFrameLimit = 60;

    /** @brief 카탈로그를 건 게임 범위와 디렉터 하나입니다. */
    struct MechDirectorTest
    {
        MechCatalog                 _mechs{};
        WeaponCatalog               _weapons{};
        ProjectileCatalog           _projectiles{};
        RecordingSoundPlayer        _soundPlayer{};
        SubsystemScope              _scope{};
        GameObjectManager           _manager{};
        MechArenaGameModeComponent* _pGameMode{ nullptr };

        [[nodiscard]] bool open( const utf8* pDirectorXML )
        {
            const bool bLoaded = _mechs.loadFromXMLText( kMechDirectorCatalogXML, "MechArenaGameModeTest" ) && _weapons.loadFromXMLText( kMechDirectorWeaponXML, "MechArenaGameModeTest" ) &&
                                 _projectiles.loadFromXMLText( kMechDirectorProjectileXML, "MechArenaGameModeTest" );
            const bool bBound = bLoaded && _scope.bind<const MechCatalog>( &_mechs ) && _scope.bind<const WeaponCatalog>( &_weapons ) && _scope.bind<const ProjectileCatalog>( &_projectiles ) && _scope.bind<ISoundPlayer>( &_soundPlayer );
            if ( bBound == false )
                return false;
            _pGameMode = _manager.createGameObject( hashed_string( "Director" ) )->addComponent<MechArenaGameModeComponent>();
            return _pGameMode != nullptr && XMLSerializer::deserialize( _pGameMode, *MechArenaGameModeComponent::StaticType(), pDirectorXML );
        }
    };
} // namespace

/** @brief [MechArenaGameModeTest] 판이 열리기 전의 명령은 거절한다 — 클라이언트가 먼저 돌아도 판을 바꾸지 않는다 */
SW_TEST_CASE( MechArenaGameModeTest, CommandsBeforeTheMatchOpensAreRefused )
{
    GameObjectManager           manager;
    MechArenaGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<MechArenaGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    SW_EXPECT_FALSE( pGameMode->isAcceptingInput() );
    SW_EXPECT_FALSE( pGameMode->submitPlayerInput( MechInput{} ) );
    SW_EXPECT_FALSE( pGameMode->requestNextMech() );
    SW_EXPECT_EQUAL( 0, pGameMode->getPlayerState() );
}

/** @brief [MechArenaGameModeTest] 기체 카탈로그가 범위에 없으면 오류를 남기고 판을 열지 않는다 */
SW_TEST_CASE( MechArenaGameModeTest, MatchWithoutTheCatalogDoesNotOpen )
{
    SW_EXPECT_LOG( "GameMode.BoundCatalogMissing" );
    GameObjectManager           manager;
    MechArenaGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<MechArenaGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    manager.beginPlay();
    SW_EXPECT_FALSE( pGameMode->isStarted() );
    manager.endPlay();
}

/** @brief [MechArenaGameModeTest] 카탈로그에 없는 기체를 적은 씬은 판을 열지 않는다 */
SW_TEST_CASE( MechArenaGameModeTest, UnknownMechDoesNotOpen )
{
    SW_EXPECT_LOG( "MechArena.PilotRefused" );
    MechDirectorTest test;
    SW_REQUIRE_TRUE( test.open( R"(<MechArenaGameModeComponent _moveDataPath="game/playground/mecharena/data/mecharena.moves.xml">
  <_listPlayerMech><item>ghost</item></_listPlayerMech>
  <_listEnemyMech><item>target</item></_listEnemyMech>
  <_listEnemySpawn><item>0,0,10</item></_listEnemySpawn>
</MechArenaGameModeComponent>)" ) );
    game::ScopedGameScope scopedScope( test._scope );
    test._manager.beginPlay();
    SW_EXPECT_FALSE( test._pGameMode->isStarted() );
    test._manager.endPlay();
}

/** @brief [MechArenaGameModeTest] 플레이어의 사격 명령이 적을 격추해 적 게이지가 바닥나면 이기고 상금이 결과 값이 되며, 그 판의 상태 바이트가 같은 판으로 되살아난다 */
SW_TEST_CASE( MechArenaGameModeTest, PlayerFireEmptiesTheEnemyGaugeAndReportsTheMoney )
{
    MechDirectorTest test;
    SW_REQUIRE_TRUE( test.open( kMechDirectorXML ) );
    game::ScopedGameScope scopedScope( test._scope );
    test._manager.beginPlay();
    SW_REQUIRE_TRUE( test._pGameMode->isStarted() );
    SW_EXPECT_EQUAL( 100, test._pGameMode->getEnemyGauge() );
    SW_EXPECT_EQUAL( 3, test._pGameMode->getPlayerAmmo() );
    SW_EXPECT_FALSE( test._pGameMode->requestNextMech() ); // 격추되지 않았고 덱도 하나다
    test._manager.update( kMechArenaGameModeFrame );
    MechInput fire;
    fire._bFire = SW_TRUE;
    SW_REQUIRE_TRUE( test._pGameMode->submitPlayerInput( fire ) );
    for ( int32 frame = 0; frame < kMechArenaGameModeFrameLimit && test._pGameMode->getEnded() == 0; ++frame )
    {
        test._manager.update( kMechArenaGameModeFrame );
    }
    SW_REQUIRE_EQUAL( 1, test._pGameMode->getEnded() );
    SW_EXPECT_TRUE( test._pGameMode->hasWon() );
    SW_EXPECT_TRUE( test._soundPlayer.countPosted( "RifleFire" ) >= 1 ); // 무기 표의 소리
    SW_EXPECT_EQUAL( 1, test._soundPlayer.countPosted( "MechArenaHit" ) );
    SW_EXPECT_FALSE( test._pGameMode->isAcceptingInput() );

    vector<MinigameResultValue> listValue;
    static_cast<const GameModeComponent*>( test._pGameMode )->collectMinigameResult( listValue );
    SW_REQUIRE_EQUAL( size_t( 1 ), listValue.size() );
    SW_EXPECT_TRUE( listValue[0]._name == hashed_string( MinigameResultValue::kMoneyName ) );
    SW_EXPECT_EQUAL( int64{ 50 }, listValue[0]._amount );

    Archive written;
    test._pGameMode->writeState( written );
    vector<uint8> bytes( written.getData(), written.getData() + written.getSize() );
    test._pGameMode->restoreState( std::move( bytes ) );
    SW_EXPECT_TRUE( test._pGameMode->hasWon() );
    Archive rewritten;
    test._pGameMode->writeState( rewritten );
    SW_EXPECT_EQUAL( written.getSize(), rewritten.getSize() );
    test._manager.endPlay();
}

/** @brief [MechArenaGameModeTest] 근접의 시작 · 맞음은 기술 표의 소리를 내고 탄의 맞음 소리는 내지 않는다 */
SW_TEST_CASE( MechArenaGameModeTest, MeleeSoundsComeFromTheMoveTable )
{
    MechDirectorTest test;
    SW_REQUIRE_TRUE( test.open( kMechMeleeDirectorXML ) );
    game::ScopedGameScope scopedScope( test._scope );
    test._manager.beginPlay();
    SW_REQUIRE_TRUE( test._pGameMode->isStarted() );
    test._manager.update( kMechArenaGameModeFrame );
    MechInput melee;
    melee._bMelee = SW_TRUE;
    SW_REQUIRE_TRUE( test._pGameMode->submitPlayerInput( melee ) );
    for ( int32 frame = 0; frame < kMechArenaGameModeFrameLimit && test._pGameMode->getEnded() == 0; ++frame )
    {
        test._manager.update( kMechArenaGameModeFrame );
    }
    SW_REQUIRE_EQUAL( 1, test._pGameMode->getEnded() );
    SW_EXPECT_EQUAL( 1, test._soundPlayer.countPosted( "MechArenaSlash" ) );
    SW_EXPECT_EQUAL( 1, test._soundPlayer.countPosted( "MechArenaSlashHit" ) );
    SW_EXPECT_EQUAL( 0, test._soundPlayer.countPosted( "MechArenaHit" ) );
    test._manager.endPlay();
}
