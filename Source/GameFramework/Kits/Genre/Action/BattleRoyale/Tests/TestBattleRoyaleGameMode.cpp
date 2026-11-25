// BattleRoyaleGameModeTest — 배틀로얄 디렉터: 명령은 판이 열린 동안만 받고, 카탈로그가 없으면 열지 않으며, 낙하 → 전투 → 구역이 점이 되어 판이 끝나는 한 바퀴를 돌고 도중 상태 바이트를 왕복한다. 표에 없는 총은 거절하고, 바닥의 더 나은 총과 탄을 주워 바꿔 든다(nogpu).
#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Component/BattleRoyaleGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/Tests/RecordingSoundPlayer.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

using namespace sw;

namespace
{
    // 구역은 곧바로 점이 되어 원 밖 피해(초당 100)로 판이 끝난다 — 사격(명중 0)과 상관없다.
    constexpr const utf8* kBattleRoyaleDirectorXML = R"(<BattleRoyaleCatalog mapSize="50">
  <Player health="50" downedHealth="20" bleedout="10" reviveTime="2" slots="2"/>
  <Flight speed="50" altitude="20" jumpStart="0" jumpEnd="0.5" offset="0.2"/>
  <Fall freeFallSpeed="40" freeFallHorizontal="5" parachuteSpeed="20" parachuteHorizontal="5" autoOpenHeight="5"/>
  <Zone startRadius="0"><Phase wait="0.5" shrink="0.5" ratio="0" damage="100"/></Zone>
  <LootSpot id="supply" table="airdrop" chance="1"/>
</BattleRoyaleCatalog>)";

    constexpr const utf8* kBattleRoyaleDirectorProjectileXML = R"(<ProjectileCatalog>
  <Projectile id="light" tags="Ammo.Light" damage="10" range="10"/>
  <Projectile id="heavy" tags="Ammo.Heavy" damage="20" range="20"/>
</ProjectileCatalog>)";

    constexpr const utf8* kBattleRoyaleDirectorWeaponXML = R"(<WeaponCatalog>
  <Weapon id="first" item="pistol" ammo="ammo_light" projectile="light" accepts="Ammo.Light" magazineSize="12" maxReserveAmmo="60"/>
  <Weapon id="better" item="scifi_rifle" ammo="ammo_rifle" projectile="heavy" accepts="Ammo.Heavy" magazineSize="24" maxReserveAmmo="120" automatic="true"/>
</WeaponCatalog>)";

    constexpr const utf8* kBattleRoyaleDirectorItemXML = R"(<ItemCatalog>
  <Item id="pistol" name="Pistol"/>
  <Item id="scifi_rifle" name="Rifle"/>
  <Item id="ammo_light" name="Light" maxStack="60" weight="0"/>
  <Item id="ammo_rifle" name="Heavy" maxStack="90" weight="0"/>
</ItemCatalog>)";

    constexpr const utf8* kBattleRoyaleGameModeXML = R"(<BattleRoyaleGameModeComponent _squadCount="2" _squadSize="1" _seed="7" _playerHitChance="0" _botHitChance="0" _startReserveAmmo="24">
  <_listWeaponID><item>first</item><item>better</item></_listWeaponID>
</BattleRoyaleGameModeComponent>)";

    // 보급 상자 표(총 · 탄을 늘 준다)의 무더기 하나가 맵 가운데에 있고 줍는 거리가 맵만 하다 — 어디에 내려도 첫 걸음에 줍는다.
    constexpr const utf8* kBattleRoyaleLootXML = R"(<BattleRoyaleGameModeComponent _squadCount="2" _squadSize="1" _seed="7" _playerHitChance="0" _botHitChance="0" _startReserveAmmo="24" _pickupRadius="100" _lootDataPath="game/playground/battleroyale/data/island.loot.xml" _lootSpotKind="supply">
  <_listWeaponID><item>first</item><item>better</item></_listWeaponID>
  <_listLootPosition><item>25,25</item></_listLootPosition>
</BattleRoyaleGameModeComponent>)";

    constexpr const utf8* kBattleRoyaleUnknownWeaponXML = R"(<BattleRoyaleGameModeComponent _squadCount="2" _squadSize="1" _seed="7">
  <_listWeaponID><item>first</item><item>missing</item></_listWeaponID>
</BattleRoyaleGameModeComponent>)";

    constexpr float32 kBattleRoyaleGameModeFrame      = 1.0f / 60.0f;
    constexpr int32   kBattleRoyaleGameModeFrameLimit = 600;

    /** @brief 카탈로그를 건 게임 범위와 디렉터 하나입니다. */
    struct BattleRoyaleDirectorTest
    {
        BattleRoyaleCatalog            _catalog{};
        WeaponCatalog                  _weapons{};
        ProjectileCatalog              _projectiles{};
        ItemCatalog                    _items{};
        RecordingSoundPlayer           _soundPlayer{};
        SubsystemScope                 _scope{};
        GameObjectManager              _manager{};
        BattleRoyaleGameModeComponent* _pGameMode{ nullptr };

        [[nodiscard]] bool open( const utf8* pDirectorXML = kBattleRoyaleGameModeXML )
        {
            const bool bLoaded = _catalog.loadFromXMLText( kBattleRoyaleDirectorXML, "BattleRoyaleGameModeTest" ) && _weapons.loadFromXMLText( kBattleRoyaleDirectorWeaponXML, "BattleRoyaleGameModeTest" ) &&
                                 _projectiles.loadFromXMLText( kBattleRoyaleDirectorProjectileXML, "BattleRoyaleGameModeTest" ) && _items.loadFromXMLText( kBattleRoyaleDirectorItemXML, "BattleRoyaleGameModeTest" );
            const bool bBound = bLoaded && _scope.bind<ISoundPlayer>( &_soundPlayer ) && _scope.bind<const BattleRoyaleCatalog>( &_catalog ) && _scope.bind<const WeaponCatalog>( &_weapons ) &&
                                _scope.bind<const ProjectileCatalog>( &_projectiles ) && _scope.bind<const ItemCatalog>( &_items );
            if ( bBound == false )
                return false;
            _pGameMode = _manager.createGameObject( hashed_string( "Director" ) )->addComponent<BattleRoyaleGameModeComponent>();
            return _pGameMode != nullptr && XMLSerializer::deserialize( _pGameMode, *BattleRoyaleGameModeComponent::StaticType(), pDirectorXML );
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
    SW_EXPECT_EQUAL( 1, test._soundPlayer.countPosted( "BattleRoyaleZoneRevealed" ) ); // 단계가 하나인 구역이다
    test._manager.endPlay();
}

/** @brief [BattleRoyaleGameModeTest] 총 목록에 표에 없는 총이 있으면 오류를 남기고 판을 열지 않는다 */
SW_TEST_CASE( BattleRoyaleGameModeTest, UnknownWeaponDoesNotOpen )
{
    SW_EXPECT_LOG( "BattleRoyale.WeaponRefused" );
    BattleRoyaleDirectorTest test;
    SW_REQUIRE_TRUE( test.open( kBattleRoyaleUnknownWeaponXML ) );
    game::ScopedGameScope scopedScope( test._scope );
    test._manager.beginPlay();
    SW_EXPECT_FALSE( test._pGameMode->isStarted() );
    test._manager.endPlay();
}

/** @brief [BattleRoyaleGameModeTest] 바닥의 더 나은 총은 탄창을 채워 바꿔 들고 그 탄은 더 넣을 탄이 되며, 든 총과 탄이 상태로 왕복한다 */
SW_TEST_CASE( BattleRoyaleGameModeTest, BetterWeaponAndAmmoArePickedUp )
{
    BattleRoyaleDirectorTest test;
    SW_REQUIRE_TRUE( test.open( kBattleRoyaleLootXML ) );
    game::ScopedGameScope scopedScope( test._scope );
    test._manager.beginPlay();
    SW_REQUIRE_TRUE( test._pGameMode->isStarted() );
    SW_EXPECT_EQUAL( 12, test._pGameMode->getPlayerMagazine() );
    SW_EXPECT_EQUAL( 24, test._pGameMode->getPlayerSpareAmmo() );
    SW_REQUIRE_TRUE( test.runUntil( BattleRoyalePhase::Battle ) );
    test._manager.update( kBattleRoyaleGameModeFrame * 2.0f );
    const ItemDef* const pItem = test._pGameMode->findPlayerWeaponItem();
    SW_REQUIRE_NOT_NULL( pItem );
    SW_EXPECT_TRUE( pItem->_id == hashed_string( "scifi_rifle" ) );
    SW_EXPECT_EQUAL( 24, test._pGameMode->getPlayerMagazine() );
    const int32 spareAmmo = test._pGameMode->getPlayerSpareAmmo();
    SW_EXPECT_TRUE( spareAmmo >= 48 );

    Archive written;
    test._pGameMode->writeState( written );
    vector<uint8> bytes( written.getData(), written.getData() + written.getSize() );
    test._pGameMode->restoreState( std::move( bytes ) );
    SW_EXPECT_EQUAL( 24, test._pGameMode->getPlayerMagazine() );
    SW_EXPECT_EQUAL( spareAmmo, test._pGameMode->getPlayerSpareAmmo() );
    test._manager.endPlay();
}
