#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/Ray.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Catalog/BattleRoyaleCatalog.h"
#include "GameFramework/Kits/Genre/Action/BattleRoyale/Rule/BattleRoyaleGear.h"
#include "GameFramework/Kits/Genre/Action/BattleRoyale/Rule/BattleRoyaleLoot.h"
#include "GameFramework/Runtime/Character/Combat/Weapon/Weapon.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/LootTable.h"

#include <limits>

// BattleRoyaleLoadoutTest — 장비: 부위별 방어구 감소와 내구도, 부서지는 한 방, 가방 무게 한도와 교체 거절, 탄약 재장전, 왕복. 결함: 내구도 0 인 방어구를 입혀 공짜로 막고 세이브를 깼다.
// BattleRoyaleCatalogTest — 카탈로그. 결함: 맵 크기가 깨진 글을 앞의 크기로 읽어 나갔다 · 슬롯 오타가 말없이 조끼가 됐다.
// BattleRoyaleLootTest — 전리품 배치: 확률 0 과 1, 모르는 지점, 같은 아이템 합치기와 이름순, 끝에 지점을 더해도 앞 지점 그대로(결함은 없었고 설명만 고쳤다). 키트 GF_BattleRoyale(nogpu).

namespace
{
    constexpr const utf8* kGearXML = R"(<BattleRoyaleCatalog mapSize="500">
  <Player carryWeight="20"/>
  <Zone><Phase wait="1" shrink="1" ratio="0.5" damage="1"/></Zone>
  <Armor id="helmet" slot="Helmet" reduction="0.5" durability="30"/>
  <Armor id="vest" slot="Vest" reduction="0.25" durability="100"/>
  <Backpack id="bag" capacity="30"/>
  <LootSpot id="house" table="gold" chance="1" rolls="2" rollsMax="2"/>
  <LootSpot id="never" table="gold" chance="0"/>
  <LootSpot id="chancy" table="mixed" chance="0.5" rolls="1" rollsMax="3"/>
  <SupplyDrop table="gold" times="9,3,6"/>
</BattleRoyaleCatalog>)";

    constexpr const utf8* kGearItemXML = R"(<ItemCatalog>
  <Item id="rock" weight="15" maxStack="5"/>
  <Item id="ammo" maxStack="100"/>
</ItemCatalog>)";

    constexpr const utf8* kGearLootXML = R"(<LootCatalog>
  <Table id="gold"><Entry item="gold" min="2"/></Table>
  <Table id="sorted"><Always item="zinc"/><Always item="amber"/></Table>
  <Table id="mixed"><Entry item="red"/><Entry item="green"/><Entry item="blue"/></Table>
</LootCatalog>)";

    const sw::hashed_string kHelmet{ "helmet" };
    const sw::hashed_string kVest{ "vest" };
    const sw::hashed_string kBag{ "bag" };
    const sw::hashed_string kRock{ "rock" };
    const sw::hashed_string kAmmo{ "ammo" };

    constexpr float32 kGearTolerance = 1.0e-3f;

    struct BattleRoyaleGearTestUtil
    {
        static void writeLoadoutBytes( sw::Archive& outArchive, const sw::hashed_string& helmet, float32 helmetDurability, const sw::hashed_string& vest, float32 vestDurability, const sw::hashed_string& bag )
        {
            sw::StateIO::writeName( outArchive, helmet );
            outArchive << helmetDurability;
            sw::StateIO::writeName( outArchive, vest );
            outArchive << vestDurability;
            sw::StateIO::writeName( outArchive, bag );
        }

        /** @brief 쏴서 탄창을 @p shots 발 비웁니다. */
        static void shoot( sw::WeaponState& weapon, int32 shots )
        {
            sw::Ray aim;
            aim._direction = sw::float3{ 1.0f, 0.0f, 0.0f };
            sw::WeaponShot shot;
            for ( int32 index = 0; index < shots; ++index )
            {
                (void)weapon.pullTrigger( aim, true, shot );
                weapon.update( 1.0f );
            }
        }

        static sw::WeaponDef makeRifle( int32 maxReserve )
        {
            sw::WeaponDef weapon;
            weapon._id             = sw::hashed_string( "rifle" );
            weapon._ammoID         = kAmmo;
            weapon._magazineSize   = 10;
            weapon._maxReserveAmmo = maxReserve;
            return weapon;
        }
    };
} // namespace

/**
 * @brief [BattleRoyaleLoadoutTest] 머리는 헬멧, 몸은 조끼가 막고 팔다리는 막지 않는다. 내구도는 줄이기 전 피해만큼 닳고, 0 이 되는 한 방까지 막은 뒤 부서진다
 */
SW_TEST_CASE( BattleRoyaleLoadoutTest, ArmorAbsorbsByZoneAndBreaks )
{
    sw::BattleRoyaleCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kGearXML, "BattleRoyaleLoadoutTest" ) );
    sw::BattleRoyaleLoadout loadout;
    loadout.initialize( &catalog );
    SW_EXPECT_NEAR( 20.0f, loadout.absorbDamage( 20.0f, sw::BattleRoyaleHitZone::Head )._damage, kGearTolerance ); // 입기 전

    SW_REQUIRE_TRUE( loadout.tryEquipArmor( kHelmet ) );
    SW_REQUIRE_TRUE( loadout.tryEquipArmor( kVest ) );
    SW_EXPECT_NEAR( 30.0f, loadout.getHelmet()._durability, kGearTolerance );

    sw::BattleRoyaleArmorResult result = loadout.absorbDamage( 20.0f, sw::BattleRoyaleHitZone::Limb );
    SW_EXPECT_NEAR( 20.0f, result._damage, kGearTolerance );
    SW_EXPECT_NEAR( 0.0f, result._absorbed, kGearTolerance );
    SW_EXPECT_NEAR( 100.0f, loadout.getVest()._durability, kGearTolerance );

    result = loadout.absorbDamage( 40.0f, sw::BattleRoyaleHitZone::Body );
    SW_EXPECT_NEAR( 30.0f, result._damage, kGearTolerance );
    SW_EXPECT_NEAR( 60.0f, loadout.getVest()._durability, kGearTolerance );

    result = loadout.absorbDamage( 20.0f, sw::BattleRoyaleHitZone::Head ); // 30 → 10
    SW_EXPECT_NEAR( 10.0f, result._damage, kGearTolerance );
    SW_EXPECT_NEAR( 10.0f, result._absorbed, kGearTolerance );
    SW_EXPECT_TRUE( result._bBroken == SW_FALSE );
    result = loadout.absorbDamage( 20.0f, sw::BattleRoyaleHitZone::Head ); // 부서지는 한 방도 막는다
    SW_EXPECT_NEAR( 10.0f, result._damage, kGearTolerance );
    SW_EXPECT_TRUE( result._bBroken == SW_TRUE );
    SW_EXPECT_TRUE( loadout.getHelmet().isEmpty() );
    result = loadout.absorbDamage( 20.0f, sw::BattleRoyaleHitZone::Head );
    SW_EXPECT_NEAR( 20.0f, result._damage, kGearTolerance );

    result = loadout.absorbDamage( 0.0f, sw::BattleRoyaleHitZone::Body );
    SW_EXPECT_NEAR( 0.0f, result._damage, kGearTolerance );
    result = loadout.absorbDamage( -5.0f, sw::BattleRoyaleHitZone::Body );
    SW_EXPECT_NEAR( 0.0f, result._damage, kGearTolerance );
    SW_EXPECT_NEAR( 60.0f, loadout.getVest()._durability, kGearTolerance ); // 음수 피해가 내구도를 채우지 않는다
}

/**
 * @brief [BattleRoyaleLoadoutTest] 음수 내구도는 새것이고 최대를 넘으면 최대다. 결함: 내구도 0 을 받아 다음 한 방을 공짜로 막았고, 저장한 상태는 읽기가 거절해 세이브가 깨졌다
 */
SW_TEST_CASE( BattleRoyaleLoadoutTest, EquipRejectsBrokenArmor )
{
    sw::BattleRoyaleCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kGearXML, "BattleRoyaleLoadoutTest" ) );
    sw::BattleRoyaleLoadout loadout;
    SW_EXPECT_FALSE( loadout.tryEquipArmor( kHelmet ) ); // 카탈로그 없음
    loadout.initialize( &catalog );
    SW_EXPECT_FALSE( loadout.tryEquipArmor( sw::hashed_string( "hat" ) ) );

    SW_EXPECT_FALSE( loadout.tryEquipArmor( kHelmet, 0.0f ) );
    SW_EXPECT_FALSE( loadout.tryEquipArmor( kHelmet, std::numeric_limits<float32>::quiet_NaN() ) );
    SW_EXPECT_TRUE( loadout.getHelmet().isEmpty() );

    SW_EXPECT_TRUE( loadout.tryEquipArmor( kHelmet, -1.0f ) );
    SW_EXPECT_NEAR( 30.0f, loadout.getHelmet()._durability, kGearTolerance );
    SW_EXPECT_TRUE( loadout.tryEquipArmor( kHelmet, 12.0f ) );
    SW_EXPECT_NEAR( 12.0f, loadout.getHelmet()._durability, kGearTolerance );
    SW_EXPECT_TRUE( loadout.tryEquipArmor( kHelmet, 999.0f ) );
    SW_EXPECT_NEAR( 30.0f, loadout.getHelmet()._durability, kGearTolerance );
}

/**
 * @brief [BattleRoyaleLoadoutTest] 가방은 무게 한도를 늘리고, 지금 무게가 새 한도를 넘는 교체 · 벗기는 거절해 물건이 사라지지 않는다. 모르는 가방은 받지 않는다
 */
SW_TEST_CASE( BattleRoyaleLoadoutTest, BackpackControlsCarryLimit )
{
    sw::BattleRoyaleCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kGearXML, "BattleRoyaleLoadoutTest" ) );
    sw::ItemCatalog items;
    SW_REQUIRE_TRUE( items.loadFromXMLText( kGearItemXML, "items" ) );
    sw::BattleRoyaleLoadout loadout;
    SW_EXPECT_NEAR( 0.0f, loadout.computeCarryLimit(), kGearTolerance );
    loadout.initialize( &catalog );
    SW_EXPECT_NEAR( 20.0f, loadout.computeCarryLimit(), kGearTolerance );
    SW_EXPECT_NEAR( 50.0f, loadout.computeCarryLimit( kBag ), kGearTolerance );

    sw::Inventory inventory;
    SW_REQUIRE_TRUE( inventory.initialize( &items, 4, loadout.computeCarryLimit() ) );
    SW_EXPECT_EQUAL( 1, inventory.addItem( kRock, 2 ) ); // 무게 15 하나만 든다

    SW_EXPECT_FALSE( loadout.tryEquipBackpack( sw::hashed_string( "sack" ), inventory ) );
    SW_EXPECT_TRUE( loadout.getBackpackID().empty() );
    SW_EXPECT_TRUE( loadout.tryEquipBackpack( kBag, inventory ) );
    SW_EXPECT_EQUAL( kBag, loadout.getBackpackID() );
    SW_EXPECT_EQUAL( 2, inventory.addItem( kRock, 2 ) ); // 45

    SW_EXPECT_FALSE( loadout.tryEquipBackpack( sw::hashed_string{}, inventory ) );
    SW_EXPECT_EQUAL( kBag, loadout.getBackpackID() );
    SW_EXPECT_EQUAL( 3, inventory.getItemCount( kRock ) );

    SW_REQUIRE_TRUE( inventory.consumeItem( kRock, 2 ) ); // 15
    SW_EXPECT_TRUE( loadout.tryEquipBackpack( sw::hashed_string{}, inventory ) );
    SW_EXPECT_TRUE( loadout.getBackpackID().empty() );
}

/**
 * @brief [BattleRoyaleLoadoutTest] 재장전은 탄창을 채울 만큼만 가방에서 옮긴다(이미 있는 예비탄 · 예비 상한을 뺀다). 가득 찼거나 재장전 중이거나 탄약이 없으면 아무것도 옮기지 않는다
 */
SW_TEST_CASE( BattleRoyaleLoadoutTest, ReloadTakesOnlyWhatItNeeds )
{
    sw::ItemCatalog items;
    SW_REQUIRE_TRUE( items.loadFromXMLText( kGearItemXML, "items" ) );
    sw::Inventory inventory;
    SW_REQUIRE_TRUE( inventory.initialize( &items, 4 ) );
    SW_REQUIRE_EQUAL( 100, inventory.addItem( kAmmo, 100 ) );
    const sw::BattleRoyaleLoadout loadout;

    sw::WeaponState full;
    SW_REQUIRE_TRUE( full.equip( BattleRoyaleGearTestUtil::makeRifle( 50 ), sw::ProjectileDef{}, 0 ) );
    SW_EXPECT_FALSE( loadout.tryReload( full, inventory ) );
    SW_EXPECT_EQUAL( 100, inventory.getItemCount( kAmmo ) );

    sw::WeaponState weapon;
    SW_REQUIRE_TRUE( weapon.equip( BattleRoyaleGearTestUtil::makeRifle( 50 ), sw::ProjectileDef{}, 1 ) );
    BattleRoyaleGearTestUtil::shoot( weapon, 3 ); // 탄창 7, 예비 1 — 2 발만 더 있으면 찬다
    SW_EXPECT_EQUAL( 7, weapon.getMagazineAmmo() );
    SW_EXPECT_TRUE( loadout.tryReload( weapon, inventory ) );
    SW_EXPECT_EQUAL( 98, inventory.getItemCount( kAmmo ) );
    SW_EXPECT_EQUAL( 3, weapon.getReserveAmmo() );
    SW_EXPECT_FALSE( loadout.tryReload( weapon, inventory ) ); // 재장전 중
    SW_EXPECT_EQUAL( 98, inventory.getItemCount( kAmmo ) );

    sw::WeaponState capped;
    SW_REQUIRE_TRUE( capped.equip( BattleRoyaleGearTestUtil::makeRifle( 2 ), sw::ProjectileDef{}, 0 ) );
    BattleRoyaleGearTestUtil::shoot( capped, 5 );
    SW_EXPECT_TRUE( loadout.tryReload( capped, inventory ) );
    SW_EXPECT_EQUAL( 96, inventory.getItemCount( kAmmo ) ); // 예비 상한 2 만큼만
    SW_EXPECT_EQUAL( 2, capped.getReserveAmmo() );

    sw::Inventory empty;
    SW_REQUIRE_TRUE( empty.initialize( &items, 4 ) );
    sw::WeaponState dry;
    SW_REQUIRE_TRUE( dry.equip( BattleRoyaleGearTestUtil::makeRifle( 50 ), sw::ProjectileDef{}, 0 ) );
    BattleRoyaleGearTestUtil::shoot( dry, 1 );
    SW_EXPECT_FALSE( loadout.tryReload( dry, empty ) );
    SW_EXPECT_FALSE( dry.isReloading() );
}

/**
 * @brief [BattleRoyaleLoadoutTest] 쓰고 읽으면 이어지고, 모르는 id · 슬롯이 다른 방어구 · 범위 밖 내구도 · 모르는 가방 · 잘린 바이트는 거절해 그대로 둔다
 */
SW_TEST_CASE( BattleRoyaleLoadoutTest, StateRoundTripAndRejection )
{
    sw::BattleRoyaleCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kGearXML, "BattleRoyaleLoadoutTest" ) );
    sw::ItemCatalog items;
    SW_REQUIRE_TRUE( items.loadFromXMLText( kGearItemXML, "items" ) );
    sw::Inventory inventory;
    SW_REQUIRE_TRUE( inventory.initialize( &items, 4, 100.0f ) );

    sw::BattleRoyaleLoadout original;
    original.initialize( &catalog );
    SW_REQUIRE_TRUE( original.tryEquipArmor( kHelmet, 12.0f ) );
    SW_REQUIRE_TRUE( original.tryEquipBackpack( kBag, inventory ) );
    sw::Archive written;
    original.writeState( written );

    sw::BattleRoyaleLoadout noCatalog;
    sw::Archive             withoutCatalog( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( noCatalog.readState( withoutCatalog ) );

    sw::BattleRoyaleLoadout restored;
    restored.initialize( &catalog );
    SW_REQUIRE_TRUE( restored.tryEquipArmor( kVest ) );
    const auto expectRejected = [&restored]( const sw::Archive& bytes )
    {
        sw::Archive reading( bytes.getData(), bytes.getSize() );
        SW_EXPECT_FALSE( restored.readState( reading ) );
        SW_EXPECT_FALSE( restored.getVest().isEmpty() );
        SW_EXPECT_TRUE( restored.getHelmet().isEmpty() );
    };
    const sw::hashed_string none{};
    sw::Archive             unknownArmor;
    BattleRoyaleGearTestUtil::writeLoadoutBytes( unknownArmor, sw::hashed_string( "hat" ), 1.0f, none, 0.0f, none );
    expectRejected( unknownArmor );
    sw::Archive wrongSlot;
    BattleRoyaleGearTestUtil::writeLoadoutBytes( wrongSlot, kVest, 1.0f, none, 0.0f, none );
    expectRejected( wrongSlot );
    sw::Archive wrongSlotVest;
    BattleRoyaleGearTestUtil::writeLoadoutBytes( wrongSlotVest, none, 0.0f, kHelmet, 1.0f, none );
    expectRejected( wrongSlotVest );
    sw::Archive zeroDurability;
    BattleRoyaleGearTestUtil::writeLoadoutBytes( zeroDurability, kHelmet, 0.0f, none, 0.0f, none );
    expectRejected( zeroDurability );
    sw::Archive overDurability;
    BattleRoyaleGearTestUtil::writeLoadoutBytes( overDurability, kHelmet, 31.0f, none, 0.0f, none );
    expectRejected( overDurability );
    sw::Archive nanDurability;
    BattleRoyaleGearTestUtil::writeLoadoutBytes( nanDurability, kHelmet, std::numeric_limits<float32>::quiet_NaN(), none, 0.0f, none );
    expectRejected( nanDurability );
    sw::Archive unknownBag;
    BattleRoyaleGearTestUtil::writeLoadoutBytes( unknownBag, none, 0.0f, none, 0.0f, sw::hashed_string( "sack" ) );
    expectRejected( unknownBag );
    sw::Archive truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( restored.readState( truncated ) );

    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_NEAR( 12.0f, restored.getHelmet()._durability, kGearTolerance );
    SW_EXPECT_TRUE( restored.getVest().isEmpty() );
    SW_EXPECT_EQUAL( kBag, restored.getBackpackID() );
    SW_EXPECT_NEAR( 50.0f, restored.computeCarryLimit(), kGearTolerance );
}

/**
 * @brief [BattleRoyaleCatalogTest] 설정 · 방어구 · 가방 · 지점을 읽고, 보급 시각은 오름차순으로 놓는다
 */
SW_TEST_CASE( BattleRoyaleCatalogTest, ReadsSettingsAndSortsDropTimes )
{
    sw::BattleRoyaleCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kGearXML, "BattleRoyaleCatalogTest" ) );
    SW_EXPECT_NEAR( 500.0f, catalog.getMapSize(), kGearTolerance );
    SW_EXPECT_EQUAL( size_t( 1 ), catalog.getZoneSettings()._listPhase.size() );
    const sw::vector<float32>& listTime = catalog.getSupplyDropSettings()._listTime;
    SW_REQUIRE_EQUAL( size_t( 3 ), listTime.size() );
    SW_EXPECT_NEAR( 3.0f, listTime[0], kGearTolerance );
    SW_EXPECT_NEAR( 9.0f, listTime[2], kGearTolerance );
    SW_EXPECT_TRUE( catalog.findArmor( kVest ) != nullptr );
    SW_EXPECT_TRUE( catalog.findBackpack( kBag ) != nullptr );
    SW_EXPECT_TRUE( catalog.findLootSpot( sw::hashed_string( "house" ) ) != nullptr );
    SW_EXPECT_TRUE( catalog.findArmor( kBag ) == nullptr );
}

/**
 * @brief [BattleRoyaleCatalogTest] 결함: 맵 크기가 깨진 글을 앞의 크기로 읽어 나갔다. 읽기 실패다
 */
SW_TEST_CASE( BattleRoyaleCatalogTest, BrokenMapSizeFailsTheLoad )
{
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    sw::BattleRoyaleCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<BattleRoyaleCatalog mapSize="0.5"><Armor id="a" slot="Vest"/></BattleRoyaleCatalog>)", "broken" ) );
    SW_EXPECT_TRUE( catalog.findArmor( sw::hashed_string( "a" ) ) == nullptr );
    SW_EXPECT_NEAR( 1000.0f, catalog.getMapSize(), kGearTolerance );
}

/**
 * @brief [BattleRoyaleCatalogTest] 결함: 슬롯 오타가 말없이 조끼가 됐다. 모르는 슬롯 · 모르는 원소는 오류로 넣지 않고, 단계가 깨진 자기장은 앞 설정을 지킨다
 */
SW_TEST_CASE( BattleRoyaleCatalogTest, BrokenElementsAreSkipped )
{
    SW_EXPECT_LOG( "BattleRoyaleCatalog.ArmorUnknownSlot" );
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    SW_EXPECT_LOG( "BattleRoyaleCatalog.NoZonePhasesZone" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );

    sw::BattleRoyaleCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<BattleRoyaleCatalog>
  <Armor id="typo" slot="Hat"/>
  <Armor id="fine" slot="Helmet"/>
  <Mystery/>
  <Zone><Phase ratio="2"/></Zone>
</BattleRoyaleCatalog>)",
                                              "broken" ) );
    SW_EXPECT_TRUE( catalog.findArmor( sw::hashed_string( "typo" ) ) == nullptr );
    SW_EXPECT_TRUE( catalog.findArmor( sw::hashed_string( "fine" ) ) != nullptr );
    SW_EXPECT_TRUE( catalog.getZoneSettings()._listPhase.empty() );
}

/**
 * @brief [BattleRoyaleLootTest] 확률 1 지점은 굴린 수만큼 합쳐 놓고, 확률 0 과 모르는 지점은 건너뛰며 지점 번호를 지킨다. 결과 목록은 먼저 비운다
 */
SW_TEST_CASE( BattleRoyaleLootTest, PlacementHonorsChanceAndKind )
{
    sw::BattleRoyaleCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kGearXML, "BattleRoyaleLootTest" ) );
    sw::LootCatalog loot;
    SW_REQUIRE_TRUE( loot.loadFromXMLText( kGearLootXML, "loot" ) );

    sw::vector<sw::BattleRoyaleLootSpot> listSpot( 4 );
    listSpot[0]._kind     = sw::hashed_string( "house" );
    listSpot[0]._position = sw::float2{ 1.0f, 2.0f };
    listSpot[1]._kind     = sw::hashed_string( "never" );
    listSpot[2]._kind     = sw::hashed_string( "ghost" );
    listSpot[3]._kind     = sw::hashed_string( "house" );
    listSpot[3]._position = sw::float2{ 5.0f, 6.0f };

    sw::vector<sw::BattleRoyaleGroundItem> listItem( 3 );
    SW_EXPECT_EQUAL( 2, sw::BattleRoyaleGroundItem::placeMapLoot( catalog, loot, listSpot, 42, listItem ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), listItem.size() );
    SW_EXPECT_EQUAL( 0, listItem[0]._spotIndex );
    SW_EXPECT_EQUAL( 4, listItem[0]._count ); // 2 번 굴림 × 2 개
    SW_EXPECT_NEAR( 2.0f, listItem[0]._position._y, kGearTolerance );
    SW_EXPECT_EQUAL( 3, listItem[1]._spotIndex );
    SW_EXPECT_NEAR( 5.0f, listItem[1]._position._x, kGearTolerance );

    const sw::vector<sw::BattleRoyaleLootSpot> listNone;
    SW_EXPECT_EQUAL( 0, sw::BattleRoyaleGroundItem::placeMapLoot( catalog, loot, listNone, 42, listItem ) );
    SW_EXPECT_TRUE( listItem.empty() );
}

/**
 * @brief [BattleRoyaleLootTest] 테이블 굴림은 이름순으로 뒤에 붙이고 붙인 수를 돌려준다. 모르는 테이블 · 0 번 굴림은 아무것도 붙이지 않는다
 */
SW_TEST_CASE( BattleRoyaleLootTest, RollTableAppendsSorted )
{
    sw::LootCatalog loot;
    SW_REQUIRE_TRUE( loot.loadFromXMLText( kGearLootXML, "loot" ) );
    sw::GameRandom                         random{ 3u };
    sw::vector<sw::BattleRoyaleGroundItem> listItem( 1 );
    const sw::float2                       position{ 8.0f, 9.0f };

    SW_EXPECT_EQUAL( 2, sw::BattleRoyaleGroundItem::rollTableAt( loot, sw::hashed_string( "sorted" ), position, -1, 2, random, listItem ) );
    SW_REQUIRE_EQUAL( size_t( 3 ), listItem.size() );
    SW_EXPECT_EQUAL( sw::hashed_string( "amber" ), listItem[1]._itemID );
    SW_EXPECT_EQUAL( sw::hashed_string( "zinc" ), listItem[2]._itemID );
    SW_EXPECT_EQUAL( 2, listItem[2]._count );
    SW_EXPECT_EQUAL( -1, listItem[2]._spotIndex );

    SW_EXPECT_EQUAL( 0, sw::BattleRoyaleGroundItem::rollTableAt( loot, sw::hashed_string( "ghost" ), position, 0, 5, random, listItem ) );
    SW_EXPECT_EQUAL( 0, sw::BattleRoyaleGroundItem::rollTableAt( loot, sw::hashed_string( "sorted" ), position, 0, 0, random, listItem ) );
    SW_EXPECT_EQUAL( size_t( 3 ), listItem.size() );
}

/**
 * @brief [BattleRoyaleLootTest] 같은 시드는 같은 배치이고, 목록 끝에 지점을 더해도 앞 지점의 아이템은 그대로다
 */
SW_TEST_CASE( BattleRoyaleLootTest, PlacementIsStableWhenSpotsAreAppended )
{
    sw::BattleRoyaleCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kGearXML, "BattleRoyaleLootTest" ) );
    sw::LootCatalog loot;
    SW_REQUIRE_TRUE( loot.loadFromXMLText( kGearLootXML, "loot" ) );

    sw::vector<sw::BattleRoyaleLootSpot> listSpot( 30 );
    for ( sw::BattleRoyaleLootSpot& spot : listSpot )
    {
        spot._kind = sw::hashed_string( "chancy" );
    }
    sw::vector<sw::BattleRoyaleGroundItem> listShort;
    (void)sw::BattleRoyaleGroundItem::placeMapLoot( catalog, loot, listSpot, 77, listShort );
    SW_EXPECT_FALSE( listShort.empty() );
    sw::vector<sw::BattleRoyaleGroundItem> listAgain;
    (void)sw::BattleRoyaleGroundItem::placeMapLoot( catalog, loot, listSpot, 77, listAgain );
    SW_REQUIRE_EQUAL( listShort.size(), listAgain.size() );

    listSpot.resize( 60 );
    for ( size_t index = 30; index < listSpot.size(); ++index )
    {
        listSpot[index]._kind = sw::hashed_string( "chancy" );
    }
    sw::vector<sw::BattleRoyaleGroundItem> listLong;
    (void)sw::BattleRoyaleGroundItem::placeMapLoot( catalog, loot, listSpot, 77, listLong );
    SW_REQUIRE_TRUE( listLong.size() >= listShort.size() );
    for ( size_t index = 0; index < listShort.size(); ++index )
    {
        SW_EXPECT_EQUAL( listShort[index]._spotIndex, listLong[index]._spotIndex );
        SW_EXPECT_EQUAL( listShort[index]._itemID, listLong[index]._itemID );
        SW_EXPECT_EQUAL( listShort[index]._count, listLong[index]._count );
        SW_EXPECT_EQUAL( listShort[index]._itemID, listAgain[index]._itemID );
    }
}
