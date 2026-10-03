#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

#include "GameFramework/Combat/Weapon.h"
#include "GameFramework/Inventory/Inventory.h"
#include "GameFramework/Inventory/ItemCatalog.h"
#include "GameFramework/Inventory/LootTable.h"
#include "GameFramework/Kits/Action/BattleRoyale/BrCatalog.h"
#include "GameFramework/Kits/Action/BattleRoyale/BrDrop.h"
#include "GameFramework/Kits/Action/BattleRoyale/BrGear.h"
#include "GameFramework/Kits/Action/BattleRoyale/BrLoot.h"
#include "GameFramework/Kits/Action/BattleRoyale/BrMatch.h"
#include "GameFramework/Kits/Action/BattleRoyale/BrZone.h"

#include "TestFramework/TestFramework.h"

// 배틀로얄 키트 — 자기장 단계(다음 원은 지금 원 안 · 금지 지형 콜백 · 수축 보간 · 밖 피해 · 직렬화), 비행기 경로 · 낙하 착지 예측,
// 지점별 전리품 배치의 결정성, 방어구 · 가방 · 탄약 재장전, 기절 · 팀원 부활 · 팀 전멸 · 순위 · 킬 피드, 자기장 피해와 보급 상자.

using namespace sw;

namespace
{
    constexpr const utf8* kBrCatalogXml = R"(
<BattleRoyaleCatalog mapSize="1000">
  <Player health="100" downedHealth="100" bleedout="10" reviveTime="10" reviveHealthRatio="0.1" carryWeight="20" slots="20" revivers="2"/>
  <Flight speed="100" altitude="600" jumpStart="0.1" jumpEnd="0.9" offset="0.6"/>
  <Fall freeFallSpeed="50" freeFallHorizontal="20" parachuteSpeed="5" parachuteHorizontal="8" autoOpenHeight="100"/>
  <Zone startRadius="0" centerAttempts="64">
    <Phase wait="60" shrink="30" ratio="0.5" damage="2"/>
    <Phase wait="30" shrink="20" ratio="0.4" damage="5"/>
  </Zone>
  <Armor id="helmet1" slot="Helmet" tier="1" reduction="0.3" durability="50"/>
  <Armor id="helmet3" slot="Helmet" tier="3" reduction="0.55" durability="230"/>
  <Armor id="vest2" slot="Vest" tier="2" reduction="0.4" durability="150"/>
  <Backpack id="bag1" tier="1" capacity="10"/>
  <Backpack id="bag3" tier="3" capacity="50"/>
  <LootSpot id="house" table="house" chance="1" rolls="2" rollsMax="3"/>
  <LootSpot id="shed" table="house" chance="0"/>
  <SupplyDrop table="airdrop" times="40"/>
</BattleRoyaleCatalog>
)";

    constexpr const utf8* kBrItemXml = R"(
<ItemCatalog>
  <Item id="ammo556" category="Ammo" maxStack="200" weight="0.5"/>
  <Item id="bandage" category="Heal" maxStack="10" weight="1"/>
  <Item id="rifle" category="Weapon" weight="4"/>
  <Item id="awm" category="Weapon" weight="6"/>
  <Item id="ghillie" category="Gear" weight="2"/>
</ItemCatalog>
)";

    constexpr const utf8* kBrLootXml = R"(
<LootCatalog>
  <Table id="house" rolls="1">
    <Entry item="ammo556" weight="4" min="10" max="30"/>
    <Entry item="bandage" weight="3" min="1" max="3"/>
    <Entry item="rifle" weight="1"/>
  </Table>
  <Table id="airdrop" rolls="1">
    <Entry item="awm" weight="1"/>
    <Always item="ghillie" chance="1"/>
  </Table>
</LootCatalog>
)";

    /** @brief 맵의 서쪽 절반이 물입니다. */
    struct BrWestWaterTerrain : IBrZoneTerrain
    {
        float32 _mapSize{ 1000.0f };

        bool isZoneCenterAllowed( const float2& position ) const override { return position._x >= _mapSize * 0.5f; }
    };

    float32 computeBrDistance( const float2& lhs, const float2& rhs )
    {
        const float32 dx = lhs._x - rhs._x;
        const float32 dy = lhs._y - rhs._y;
        return MathUtil::sqrt( dx * dx + dy * dy );
    }

    bool containsBrEvent( const vector<BrEvent>& listEvent, BrEvent::Kind kind, int32 player = -2 )
    {
        for ( const BrEvent& event : listEvent )
        {
            if ( event._kind == kind && ( player == -2 || event._player == player ) )
                return true;
        }
        return false;
    }

    struct BrTestWorld
    {
        BrCatalog       _catalog;
        ItemCatalog     _itemCatalog;
        LootCatalog     _lootCatalog;
        BrMatch         _match;
        vector<BrEvent> _listEvent;

        bool initialize( uint32 seed )
        {
            if ( _catalog.loadFromXmlText( kBrCatalogXml, "BattleRoyaleTest" ) == false || _itemCatalog.loadFromXmlText( kBrItemXml, "BattleRoyaleTest" ) == false ||
                 _lootCatalog.loadFromXmlText( kBrLootXml, "BattleRoyaleTest" ) == false )
                return false;
            _match.initialize( &_catalog, &_itemCatalog, &_lootCatalog, seed, nullptr );
            return true;
        }

        /** @brief 두 사람씩 두 팀(0, 1 = 팀 0 · 2, 3 = 팀 1)을 원 중심에 세우고 시작합니다. */
        void startTwoSquads()
        {
            const int32 teamA = _match.addTeam( "Alpha" );
            const int32 teamB = _match.addTeam( "Bravo" );
            for ( int32 index = 0; index < 4; ++index )
            {
                const int32 player = _match.addPlayer( index < 2 ? teamA : teamB );
                _match.setPlayerPosition( player, _match.getZone().getNextCenter() );
            }
            _match.start();
        }

        void run( float32 seconds )
        {
            for ( float32 time = 0.0f; time < seconds - 1.0e-4f; time += BrMatch::kFixedStep )
                _match.update( BrMatch::kFixedStep );
            _match.drainEvents( _listEvent );
        }
    };
} // namespace

SW_TEST_CASE( BattleRoyaleTest, ZoneShrinksIntoNextCircleAndHurtsOutside )
{
    BrCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kBrCatalogXml, "BattleRoyaleTest" ) );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( catalog.getZoneSettings()._listPhase.size() ) );

    BrZone zone;
    zone.initialize( catalog.getZoneSettings(), catalog.getMapSize(), 7u, nullptr );
    const float32 startRadius = zone.getRadius();
    SW_EXPECT_NEAR_EQUAL( 500.0f * MathUtil::sqrt( 2.0f ), startRadius, 0.01f );
    SW_EXPECT_TRUE( zone.getStage() == BrZoneStage::Waiting );
    SW_EXPECT_NEAR_EQUAL( startRadius * 0.5f, zone.getNextRadius(), 0.01f );
    // 다음 원은 지금 원 안에 통째로, 그리고 맵 안에.
    SW_EXPECT_TRUE( computeBrDistance( zone.getCenter(), zone.getNextCenter() ) + zone.getNextRadius() <= zone.getRadius() + 0.01f );
    SW_EXPECT_TRUE( zone.getNextCenter()._x >= zone.getNextRadius() - 0.01f && zone.getNextCenter()._x <= 1000.0f - zone.getNextRadius() + 0.01f );

    // 기다리는 동안은 줄지 않고, 줄어드는 중간은 보간이다.
    const float2 firstCenter = zone.getCenter();
    zone.update( 59.0f );
    SW_EXPECT_NEAR_EQUAL( startRadius, zone.getRadius(), 0.001f );
    zone.update( 1.0f + 15.0f );
    SW_EXPECT_TRUE( zone.getStage() == BrZoneStage::Shrinking );
    SW_EXPECT_NEAR_EQUAL( startRadius * 0.75f, zone.getRadius(), 0.05f );
    SW_EXPECT_NEAR_EQUAL( ( firstCenter._x + zone.getNextCenter()._x ) * 0.5f, zone.getCenter()._x, 0.05f );

    // 다 줄면 다음 단계 — 원이 다음 원과 같고 새 다음 원이 드러난다.
    const float2 nextCenter = zone.getNextCenter();
    zone.update( 15.0f );
    SW_EXPECT_EQUAL( 1, zone.getPhaseIndex() );
    SW_EXPECT_NEAR_EQUAL( nextCenter._x, zone.getCenter()._x, 0.01f );
    SW_EXPECT_NEAR_EQUAL( startRadius * 0.5f * 0.4f, zone.getNextRadius(), 0.01f );

    // 피해는 원 밖에서만, 지금 단계의 값.
    SW_EXPECT_NEAR_EQUAL( 0.0f, zone.computeDamagePerSecond( zone.getCenter() ), 0.0001f );
    const float2 outside{ zone.getCenter()._x + zone.getRadius() + 5.0f, zone.getCenter()._y };
    SW_EXPECT_NEAR_EQUAL( 5.0f, zone.computeDamagePerSecond( outside ), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, zone.computeDistanceOutside( outside ), 0.01f );

    // 직렬화 — 받은 쪽이 같은 원을 본다.
    BitWriter writer;
    zone.writeState( writer );
    BrZone copy;
    copy.initialize( catalog.getZoneSettings(), catalog.getMapSize(), 99u, nullptr );
    BitReader reader( writer.getBytes().data(), writer.getByteCount() );
    SW_ASSERT_TRUE( copy.readState( reader ) );
    SW_EXPECT_NEAR_EQUAL( zone.getRadius(), copy.getRadius(), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( zone.getNextCenter()._y, copy.getNextCenter()._y, 0.0001f );

    // 끝까지 — 마지막 원.
    zone.update( 100.0f );
    SW_EXPECT_TRUE( zone.getStage() == BrZoneStage::Final );
    vector<BrZoneEvent> listEvent;
    zone.drainEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.empty() == false && listEvent.back()._kind == BrZoneEvent::Kind::FinalZone );

    // 같은 씨앗이면 같은 원.
    BrZone again;
    again.initialize( catalog.getZoneSettings(), catalog.getMapSize(), 7u, nullptr );
    SW_EXPECT_NEAR_EQUAL( firstCenter._x, again.getCenter()._x, 0.0001f );
    zone.initialize( catalog.getZoneSettings(), catalog.getMapSize(), 7u, nullptr );
    SW_EXPECT_NEAR_EQUAL( zone.getNextCenter()._x, again.getNextCenter()._x, 0.0001f );
}

SW_TEST_CASE( BattleRoyaleTest, NextZoneCenterAvoidsForbiddenTerrain )
{
    BrCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kBrCatalogXml, "BattleRoyaleTest" ) );
    BrWestWaterTerrain terrain;

    // 콜백이 없으면 서쪽(물)에도 원이 생긴다 — 이 시험이 콜백을 실제로 재는지 확인.
    int32 westWithoutCallback = 0;
    int32 westWithCallback    = 0;
    for ( uint32 seed = 1; seed <= 40; ++seed )
    {
        BrZone free;
        free.initialize( catalog.getZoneSettings(), catalog.getMapSize(), seed, nullptr );
        westWithoutCallback += free.getNextCenter()._x < 500.0f ? 1 : 0;

        BrZone dry;
        dry.initialize( catalog.getZoneSettings(), catalog.getMapSize(), seed, &terrain );
        westWithCallback += dry.getNextCenter()._x < 500.0f ? 1 : 0;
        // 보급 상자 자리도 같은 콜백을 지킨다.
        GameRandom random{ seed };
        float2     dropPosition;
        SW_EXPECT_TRUE( dry.pickPointInside( random, dropPosition ) );
        SW_EXPECT_TRUE( dropPosition._x >= 500.0f && dry.isInside( dropPosition ) );
    }
    SW_EXPECT_TRUE( westWithoutCallback > 0 );
    SW_EXPECT_EQUAL( 0, westWithCallback );
}

SW_TEST_CASE( BattleRoyaleTest, FlightPathCrossesMapAndLandingIsPredicted )
{
    BrCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kBrCatalogXml, "BattleRoyaleTest" ) );
    const BrFlightPath path  = BrFlightPath::makeRandom( catalog.getFlightSettings(), 1000.0f, 42u );
    const BrFlightPath again = BrFlightPath::makeRandom( catalog.getFlightSettings(), 1000.0f, 42u );
    SW_EXPECT_NEAR_EQUAL( path._start._x, again._start._x, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( path._end._y, again._end._y, 0.0001f );

    // 들어오고 나가는 점이 모두 맵 경계 위.
    const auto isOnEdge = []( const float2& point )
    {
        const bool bInside = point._x > -0.01f && point._x < 1000.01f && point._y > -0.01f && point._y < 1000.01f;
        const bool bEdge   = MathUtil::abs( point._x ) < 0.01f || MathUtil::abs( point._x - 1000.0f ) < 0.01f || MathUtil::abs( point._y ) < 0.01f ||
                           MathUtil::abs( point._y - 1000.0f ) < 0.01f;
        return bInside && bEdge;
    };
    SW_EXPECT_TRUE( isOnEdge( path._start ) );
    SW_EXPECT_TRUE( isOnEdge( path._end ) );
    SW_EXPECT_TRUE( path.computeLength() > 100.0f );

    // 뛰어내릴 수 있는 구간은 길이의 10 % .. 90 %.
    const float32 duration = path.computeDuration();
    SW_EXPECT_FALSE( path.canJump( duration * 0.05f ) );
    SW_EXPECT_TRUE( path.canJump( duration * 0.5f ) );
    SW_EXPECT_FALSE( path.canJump( duration * 0.95f ) );
    const float2 middle = path.computePosition( duration * 0.5f );
    SW_EXPECT_NEAR_EQUAL( ( path._start._x + path._end._x ) * 0.5f, middle._x, 0.01f );

    // 낙하 — 600 m 에서 동쪽으로: 자유 낙하 500 m 는 10 s(× 20 m/s = 200 m), 낙하산 100 m 는 20 s(× 8 m/s = 160 m) — 360 m.
    const BrFallSettings& fall  = catalog.getFallSettings();
    const float2          steer = float2{ 1.0f, 0.0f };
    BrSkydiver            diver;
    diver.initialize( fall, float3{ middle._x, 600.0f, middle._y }, 0.0f );
    const float3 predicted = diver.predictLanding( steer );
    SW_EXPECT_NEAR_EQUAL( middle._x + 360.0f, predicted._x, 0.01f );
    for ( int32 frame = 0; frame < 400 && diver.isLanded() == false; ++frame )
        diver.update( 0.13f, steer );
    SW_EXPECT_TRUE( diver.isLanded() );
    SW_EXPECT_NEAR_EQUAL( predicted._x, diver.getPosition()._x, 0.05f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, diver.getPosition()._y, 0.0001f );

    // 일찍 펴면 낙하산으로 오래 떠서 수평으로 더 멀리 간다(600 m / 5 m/s = 120 s × 8 m/s = 960 m).
    BrSkydiver early;
    early.initialize( fall, float3{ 0.0f, 600.0f, 0.0f }, 0.0f );
    SW_EXPECT_TRUE( early.tryOpenParachute() );
    SW_EXPECT_FALSE( early.tryOpenParachute() );
    SW_EXPECT_NEAR_EQUAL( 960.0f, early.predictLanding( steer )._x, 0.01f );
}

SW_TEST_CASE( BattleRoyaleTest, LootPlacementIsSeededPerSpot )
{
    BrCatalog   catalog;
    LootCatalog lootCatalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kBrCatalogXml, "BattleRoyaleTest" ) );
    SW_ASSERT_TRUE( lootCatalog.loadFromXmlText( kBrLootXml, "BattleRoyaleTest" ) );

    vector<BrLootSpot> listSpot;
    for ( int32 index = 0; index < 12; ++index )
    {
        BrLootSpot spot;
        spot._position = float2{ static_cast<float32>( index * 50 ), 100.0f };
        spot._kind     = hashed_string( index == 3 ? "shed" : ( index == 7 ? "mystery" : "house" ) );
        listSpot.push_back( spot );
    }
    vector<BrGroundItem> listFirst;
    vector<BrGroundItem> listSecond;
    SW_EXPECT_TRUE( BrLootPlacement::placeMapLoot( catalog, lootCatalog, listSpot, 2024u, listFirst ) > 0 );
    (void)BrLootPlacement::placeMapLoot( catalog, lootCatalog, listSpot, 2024u, listSecond );
    SW_ASSERT_TRUE( listFirst.size() == listSecond.size() );
    for ( size_t index = 0; index < listFirst.size(); ++index )
    {
        SW_EXPECT_TRUE( listFirst[index]._itemId == listSecond[index]._itemId );
        SW_EXPECT_EQUAL( listFirst[index]._count, listSecond[index]._count );
    }
    // 확률 0 인 창고와 모르는 지점은 비고, 집마다 2..3 번 굴려 무엇인가 놓인다.
    vector<int32> listCountPerSpot( listSpot.size(), 0 );
    for ( const BrGroundItem& item : listFirst )
        ++listCountPerSpot[static_cast<size_t>( item._spotIndex )];
    SW_EXPECT_EQUAL( 0, listCountPerSpot[3] );
    SW_EXPECT_EQUAL( 0, listCountPerSpot[7] );
    SW_EXPECT_TRUE( listCountPerSpot[0] >= 1 && listCountPerSpot[11] >= 1 );

    // 지점 하나를 빼도 다른 지점의 아이템은 그대로(지점마다 난수가 따로).
    vector<BrLootSpot> listFewer = listSpot;
    listFewer.pop_back();
    vector<BrGroundItem> listThird;
    (void)BrLootPlacement::placeMapLoot( catalog, lootCatalog, listFewer, 2024u, listThird );
    size_t sameCount = 0;
    for ( const BrGroundItem& item : listFirst )
        sameCount += item._spotIndex != 11 ? 1 : 0;
    SW_ASSERT_TRUE( listThird.size() == sameCount );
    SW_EXPECT_TRUE( listThird.back()._itemId == listFirst[sameCount - 1]._itemId );

    // 씨앗이 다르면 배치가 바뀐다.
    vector<BrGroundItem> listOther;
    (void)BrLootPlacement::placeMapLoot( catalog, lootCatalog, listSpot, 7u, listOther );
    bool bDifferent = listOther.size() != listFirst.size();
    for ( size_t index = 0; bDifferent == false && index < listFirst.size(); ++index )
        bDifferent = listFirst[index]._itemId != listOther[index]._itemId || listFirst[index]._count != listOther[index]._count;
    SW_EXPECT_TRUE( bDifferent );
}

SW_TEST_CASE( BattleRoyaleTest, ArmorBackpackAndAmmoReload )
{
    BrCatalog   catalog;
    ItemCatalog itemCatalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kBrCatalogXml, "BattleRoyaleTest" ) );
    SW_ASSERT_TRUE( itemCatalog.loadFromXmlText( kBrItemXml, "BattleRoyaleTest" ) );
    BrLoadout loadout;
    loadout.initialize( &catalog );

    // 헬멧은 머리만, 조끼는 몸만, 팔다리는 아무것도.
    SW_EXPECT_TRUE( loadout.tryEquipArmor( "helmet1" ) );
    SW_EXPECT_TRUE( loadout.tryEquipArmor( "vest2" ) );
    SW_EXPECT_FALSE( loadout.tryEquipArmor( "bandage" ) );
    SW_EXPECT_NEAR_EQUAL( 100.0f, loadout.absorbDamage( 100.0f, BrHitZone::Limb )._damage, 0.001f );
    const BrArmorResult body = loadout.absorbDamage( 50.0f, BrHitZone::Body );
    SW_EXPECT_NEAR_EQUAL( 30.0f, body._damage, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 100.0f, loadout.getVest()._durability, 0.001f );
    // 1 단계 헬멧(내구도 50)은 40 + 40 에 부서진다 — 부서지는 한 방까지는 막는다.
    SW_EXPECT_NEAR_EQUAL( 28.0f, loadout.absorbDamage( 40.0f, BrHitZone::Head )._damage, 0.001f );
    const BrArmorResult breaking = loadout.absorbDamage( 40.0f, BrHitZone::Head );
    SW_EXPECT_TRUE( breaking._bBroken == SW_TRUE );
    SW_EXPECT_NEAR_EQUAL( 28.0f, breaking._damage, 0.001f );
    SW_EXPECT_TRUE( loadout.getHelmet().isEmpty() );
    SW_EXPECT_NEAR_EQUAL( 40.0f, loadout.absorbDamage( 40.0f, BrHitZone::Head )._damage, 0.001f );
    // 3 단계 헬멧은 더 줄인다.
    SW_EXPECT_TRUE( loadout.tryEquipArmor( "helmet3" ) );
    SW_EXPECT_NEAR_EQUAL( 18.0f, loadout.absorbDamage( 40.0f, BrHitZone::Head )._damage, 0.001f );

    // 가방 — 무게 한도 = 20 + 용량. 짐이 새 한도를 넘으면 작은 가방으로 못 바꾼다.
    Inventory inventory;
    inventory.initialize( &itemCatalog, 20, loadout.computeCarryLimit() );
    SW_EXPECT_NEAR_EQUAL( 20.0f, inventory.getMaxWeight(), 0.001f );
    SW_EXPECT_EQUAL( 40, inventory.addItem( "ammo556", 100 ) ); // 0.5 × 40 = 20
    SW_EXPECT_TRUE( loadout.tryEquipBackpack( "bag3", inventory ) );
    SW_EXPECT_NEAR_EQUAL( 70.0f, inventory.getMaxWeight(), 0.001f );
    SW_EXPECT_EQUAL( 60, inventory.addItem( "ammo556", 60 ) ); // 100 발 = 50
    SW_EXPECT_FALSE( loadout.tryEquipBackpack( "bag1", inventory ) );
    SW_EXPECT_TRUE( loadout.getBackpackId() == hashed_string( "bag3" ) );
    SW_EXPECT_FALSE( loadout.tryEquipBackpack( "helmet1", inventory ) );

    // 재장전 — 탄약 아이템에서 빈 만큼만 옮긴다.
    WeaponDef rifle;
    rifle._id             = hashed_string( "rifle" );
    rifle._ammoId         = hashed_string( "ammo556" );
    rifle._magazineSize   = 30;
    rifle._maxReserveAmmo = 300;
    rifle._fireInterval   = 0.1f;
    rifle._reloadTime     = 2.0f;
    WeaponState weapon;
    weapon.equip( rifle, 0 );
    SW_EXPECT_FALSE( loadout.tryReload( weapon, inventory ) ); // 가득
    WeaponShot shot;
    for ( int32 index = 0; index < 12; ++index )
    {
        (void)weapon.pullTrigger( GameRay{}, true, shot );
        weapon.update( 0.11f );
    }
    SW_EXPECT_EQUAL( 18, weapon.getMagazineAmmo() );
    SW_EXPECT_TRUE( loadout.tryReload( weapon, inventory ) );
    SW_EXPECT_EQUAL( 88, inventory.getItemCount( "ammo556" ) );
    SW_EXPECT_FALSE( loadout.tryReload( weapon, inventory ) ); // 재장전 중
    weapon.update( 2.1f );
    SW_EXPECT_EQUAL( 30, weapon.getMagazineAmmo() );
    SW_EXPECT_EQUAL( 0, weapon.getReserveAmmo() );

    // 탄약이 없으면 재장전하지 못한다.
    SW_EXPECT_TRUE( inventory.removeItem( "ammo556", 88 ) );
    for ( int32 index = 0; index < 5; ++index )
    {
        (void)weapon.pullTrigger( GameRay{}, true, shot );
        weapon.update( 0.11f );
    }
    SW_EXPECT_FALSE( loadout.tryReload( weapon, inventory ) );
}

SW_TEST_CASE( BattleRoyaleTest, DownedReviveTeamWipeAndPlacement )
{
    BrTestWorld world;
    SW_ASSERT_TRUE( world.initialize( 11u ) );
    world.startTwoSquads();

    // 적(2)이 0 을 기절시킨다 — 킬 피드 "knocked", 남은 인원은 그대로 4.
    SW_EXPECT_NEAR_EQUAL( 100.0f, world._match.applyDamage( 0, 2, 150.0f, BrHitZone::Body ), 0.001f );
    SW_EXPECT_TRUE( world._match.findPlayer( 0 )->isDowned() );
    SW_EXPECT_EQUAL( 4, world._match.countRemainingPlayers() );
    // 같은 팀 피해는 없다.
    SW_EXPECT_NEAR_EQUAL( 0.0f, world._match.applyDamage( 1, 0, 50.0f, BrHitZone::Body ), 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, world._match.applyDamage( 3, 2, 50.0f, BrHitZone::Body ), 0.001f );

    // 출혈 2 초 = 20.
    world.run( 2.0f );
    SW_EXPECT_NEAR_EQUAL( 80.0f, world._match.findPlayer( 0 )->_vitality.getDownedHealth(), 0.01f );
    // 적은 살릴 수 없고, 팀원(1)은 살린다 — 살리는 동안 출혈이 멈춘다.
    SW_EXPECT_FALSE( world._match.beginRevive( 2, 0 ) );
    SW_EXPECT_TRUE( world._match.beginRevive( 1, 0 ) );
    SW_EXPECT_FALSE( world._match.beginRevive( 1, 0 ) );
    world.run( 5.0f );
    SW_EXPECT_NEAR_EQUAL( 80.0f, world._match.findPlayer( 0 )->_vitality.getDownedHealth(), 0.01f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, world._match.findPlayer( 0 )->_revive.getProgress(), 0.02f );
    // 적이 기절한 사람을 쏘면 부활이 처음부터.
    (void)world._match.applyDamage( 0, 2, 10.0f, BrHitZone::Body );
    SW_EXPECT_NEAR_EQUAL( 0.0f, world._match.findPlayer( 0 )->_revive.getProgress(), 0.0001f );
    SW_EXPECT_TRUE( containsBrEvent( world._listEvent, BrEvent::Kind::ReviveStarted, 0 ) );
    world._match.drainEvents( world._listEvent );
    SW_EXPECT_TRUE( containsBrEvent( world._listEvent, BrEvent::Kind::ReviveInterrupted, 0 ) );
    // 다시 붙어 10 초 — 살아난다(체력 10 %).
    SW_EXPECT_TRUE( world._match.beginRevive( 1, 0 ) );
    world.run( 10.5f );
    SW_EXPECT_TRUE( world._match.findPlayer( 0 )->isAlive() );
    SW_EXPECT_NEAR_EQUAL( 10.0f, world._match.findPlayer( 0 )->_vitality.getHealth(), 0.01f );
    SW_EXPECT_TRUE( containsBrEvent( world._listEvent, BrEvent::Kind::PlayerRevived, 0 ) );

    // 팀 0 을 둘 다 기절시키면 팀 전멸 — 둘 다 죽고 처치는 기절시킨 사람에게.
    world._listEvent.clear();
    (void)world._match.applyDamage( 0, 3, 50.0f, BrHitZone::Head );
    SW_EXPECT_TRUE( world._match.findPlayer( 0 )->isDowned() );
    (void)world._match.applyDamage( 1, 2, 150.0f, BrHitZone::Body );
    SW_EXPECT_TRUE( world._match.findPlayer( 0 )->isDead() );
    SW_EXPECT_TRUE( world._match.findPlayer( 1 )->isDead() );
    SW_EXPECT_EQUAL( 1, world._match.findPlayer( 3 )->_kills );
    SW_EXPECT_EQUAL( 1, world._match.findPlayer( 2 )->_kills );
    SW_EXPECT_EQUAL( 2, world._match.countRemainingPlayers() );
    world._match.drainEvents( world._listEvent );
    SW_EXPECT_TRUE( containsBrEvent( world._listEvent, BrEvent::Kind::PlayerKilled, 0 ) );
    SW_EXPECT_TRUE( containsBrEvent( world._listEvent, BrEvent::Kind::PlayerKilled, 1 ) );
    // 순위 — 떨어진 팀은 2 위, 남은 팀이 이긴다.
    SW_EXPECT_EQUAL( 2, world._match.getMatchState().findTeam( 0 )->_placement );
    SW_EXPECT_EQUAL( 1, world._match.getMatchState().getWinningTeam() );
    bool bEnded = false;
    for ( const BrEvent& event : world._listEvent )
        bEnded = bEnded || ( event._kind == BrEvent::Kind::MatchEnded && event._team == 1 );
    SW_EXPECT_TRUE( bEnded );
    SW_EXPECT_TRUE( world._match.getMatchState().getPhase() == MatchPhase::Ended );
}

SW_TEST_CASE( BattleRoyaleTest, ZoneDamageBleedoutCreditAndSupplyDrop )
{
    BrTestWorld world;
    SW_ASSERT_TRUE( world.initialize( 5u ) );
    world.startTwoSquads();
    BrPlayer* pOutside = world._match.findPlayerMutable( 3 );
    SW_ASSERT_NOT_NULL( pOutside );
    SW_EXPECT_TRUE( pOutside->_loadout.tryEquipArmor( "vest2" ) );

    // 원 밖에 세운다 — 첫 원은 맵을 덮으므로 맵 귀퉁이 너머.
    world._match.setPlayerPosition( 3, float2{ -400.0f, -400.0f } );
    world.run( 10.0f );
    // 자기장 피해는 방어구를 거치지 않는다: 2/초 × 10 초.
    SW_EXPECT_NEAR_EQUAL( 80.0f, pOutside->_vitality.getHealth(), 0.05f );
    SW_EXPECT_NEAR_EQUAL( 150.0f, pOutside->_loadout.getVest()._durability, 0.001f );

    // 2 가 기절시키고 자기장 · 출혈이 끝내도 처치는 2 의 것.
    world._match.setPlayerPosition( 2, float2{ -400.0f, -400.0f } );
    (void)world._match.applyDamage( 2, 0, 200.0f, BrHitZone::Limb );
    SW_EXPECT_TRUE( world._match.findPlayer( 2 )->isDowned() );
    world.run( 12.0f );
    SW_EXPECT_TRUE( world._match.findPlayer( 2 )->isDead() );
    SW_EXPECT_EQUAL( 1, world._match.findPlayer( 0 )->_kills );

    // 기절 없이 자기장으로 죽은 사람은 환경의 처치(−1). 팀 1 의 마지막 사람이라 판이 끝난다 — 그 전에 보급 상자.
    world.run( 20.0f );
    SW_ASSERT_TRUE( world._match.getSupplyDrops().size() == 1 );
    const BrSupplyDrop& drop = world._match.getSupplyDrops()[0];
    SW_EXPECT_NEAR_EQUAL( 40.0f, drop._time, 0.11f );
    SW_EXPECT_TRUE( drop._listItem.size() == 2 );
    SW_EXPECT_TRUE( drop._listItem[0]._itemId == hashed_string( "awm" ) && drop._listItem[1]._itemId == hashed_string( "ghillie" ) );
    SW_EXPECT_TRUE( containsBrEvent( world._listEvent, BrEvent::Kind::SupplyDropLanded ) );

    world.run( 40.0f );
    SW_EXPECT_TRUE( pOutside->isDead() );
    bool bEnvironmentKill = false;
    for ( const BrEvent& event : world._listEvent )
        bEnvironmentKill = bEnvironmentKill || ( event._kind == BrEvent::Kind::PlayerKilled && event._player == 3 && event._other == -1 );
    SW_EXPECT_TRUE( bEnvironmentKill );
    SW_EXPECT_EQUAL( 0, world._match.getMatchState().getWinningTeam() );

    // 같은 씨앗 · 같은 입력이면 같은 보급 상자 자리.
    BrTestWorld replay;
    SW_ASSERT_TRUE( replay.initialize( 5u ) );
    replay.startTwoSquads();
    replay.run( 41.0f );
    SW_ASSERT_TRUE( replay._match.getSupplyDrops().size() == 1 );
    SW_EXPECT_NEAR_EQUAL( drop._position._x, replay._match.getSupplyDrops()[0]._position._x, 0.0001f );
    SW_EXPECT_TRUE( drop._position._x >= 0.0f && drop._position._x <= 1000.0f && drop._position._y >= 0.0f && drop._position._y <= 1000.0f );
}
