// 유령 사냥 키트(루이지 맨션 장르) — 손전등 원뿔 · 스트로브, 유령 상태 순환 · 기절 시간, 흡입 줄다리기 · 서지 · 강화 단계, 가구 보물의 결정성, 방 불 · 열쇠 문, 부의 탈출.
#include "pch.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Framework/GameStateRefs.h"
#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemStackList.h"
#include "GameFramework/Base/Gameplay/Inventory/LootTable.h"
#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/Base/World/Land/AreaGraph.h"
#include "GameFramework/Base/World/Query/GameFlags.h"
#include "GameFramework/Kits/Horror/GhostHunt/GhostCatalog.h"
#include "GameFramework/Kits/Horror/GhostHunt/GhostEncounter.h"
#include "GameFramework/Kits/Horror/GhostHunt/GhostMansion.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr const utf8* kGhostHuntXml = R"(
<GhostHunt>
  <Flashlight range="8" angle="25" strobeRange="10" strobeAngle="40" strobeCharge="1"/>
  <Vacuum range="4" stages="10 20 30" alignThreshold="0.7" alignBonus="2" surgeFill="1" surgeDamage="40" dragSpeed="2" dragReduction="0.75"/>
  <Ghost id="goob" hp="40" hideTime="2" appearTime="1.5" attackTime="1" attackDamage="8" stunTime="3" pull="1" fleeInterval="1" coins="10"/>
  <Ghost id="shy" hp="40" hideTime="2" appearTime="1.5" attackTime="1" stunTime="3" strobeOnly="true"/>
  <Room id="foyer" ghosts="goob,goob" key="parlorKey"/>
  <Room id="parlor" ghosts="goob"/>
  <Room id="hall"/>
  <Door id="parlorDoor" key="parlorKey" flag="door.parlor"/>
  <Furniture id="dresser" room="foyer" loot="dresserLoot" search="Shake"/>
  <Furniture id="curtain" room="foyer" loot="curtainLoot" search="Vacuum"/>
  <Furniture id="chest" room="parlor" loot="dresserLoot"/>
  <Furniture id="vase" room="parlor" loot="curtainLoot"/>
  <Boo id="booA" room="foyer" furniture="dresser" hp="30" escapeTime="5"/>
</GhostHunt>
)";

    constexpr const utf8* kGhostAreaXml = R"(
<AreaGraph>
  <Area id="foyer"/>
  <Area id="parlor"/>
  <Area id="hall"/>
  <Link from="foyer" to="parlor" requires="door.parlor"/>
  <Link from="foyer" to="hall"/>
</AreaGraph>
)";

    constexpr const utf8* kGhostLootXml = R"(
<LootCatalog>
  <Table id="dresserLoot" rolls="2"><Entry item="coin" weight="5" min="5" max="10"/><Entry item="pearl" weight="1"/></Table>
  <Table id="curtainLoot"><Entry item="bill" weight="1" min="1" max="3"/></Table>
</LootCatalog>
)";

    constexpr float3 kPlayer{ 0.0f, 0.0f, -2.0f };
    constexpr float3 kNorth{ 0.0f, 0.0f, 1.0f };

    /** @brief 유령이 공격할 때까지 시간을 흘립니다(0.1 초씩). 공격 중이 되었으면 true 입니다. */
    bool waitForAttack( GhostEncounter& encounter, uint32 ghostId )
    {
        for ( int32 tick = 0; tick < 200; ++tick )
        {
            const GhostInstance* pGhost = encounter.findGhost( ghostId );
            if ( pGhost != nullptr && pGhost->_state == GhostState::Attacking )
                return true;
            encounter.update( 0.1f );
        }
        return false;
    }

    /** @brief 유령을 기절시켜 빨아들이기 시작합니다. */
    bool stunAndGrab( GhostEncounter& encounter, uint32 ghostId )
    {
        if ( waitForAttack( encounter, ghostId ) == false || encounter.shineBeam( kPlayer, kNorth ) == 0 )
            return false;
        return encounter.startSuction( ghostId, kPlayer );
    }

    /** @brief 흡입 줄다리기를 잡힐 때까지 합니다. @p bOpposite 면 도망 반대로, 아니면 도망 방향의 옆으로 당깁니다. 걸린 틱 수입니다. */
    int32 tugUntilCaught( GhostEncounter& encounter, uint32 ghostId, bool bOpposite, float32& outDragDistance )
    {
        outDragDistance = 0.0f;
        for ( int32 tick = 1; tick <= 400; ++tick )
        {
            const GhostInstance*   pGhost = encounter.findGhost( ghostId );
            const float3           flee   = pGhost->_fleeDirection;
            const float3           pull   = bOpposite ? float3{ -flee._x, 0.0f, -flee._z } : float3{ flee._z, 0.0f, -flee._x };
            const GhostSuctionTick result = encounter.updateSuction( pull, 0.1f );
            outDragDistance += result._drag.getLength();
            if ( result._bCaught == SW_TRUE )
                return tick;
        }
        return -1;
    }

    struct GhostMansionScene
    {
        GhostCatalog _catalog;
        LootCatalog  _loot;
        AreaGraph    _areaGraph;
        GameFlags    _flags;
        Inventory    _bag;    ///< 플레이어 가방(열쇠)
        Wallet       _wallet; ///< 지갑(동전)
        GhostMansion _mansion;

        bool initialize( uint32 seed )
        {
            if ( _catalog.loadFromXmlText( kGhostHuntXml, "GhostHuntTest" ) == false || _loot.loadFromXmlText( kGhostLootXml, "GhostHuntTest" ) == false ||
                 _areaGraph.loadFromXmlText( kGhostAreaXml, "GhostHuntTest" ) == false )
                return false;
            _bag.initialize( nullptr, 8 );
            GameStateRefs refs;
            refs._pFlags     = &_flags;
            refs._pInventory = &_bag;
            refs._pWallet    = &_wallet;
            _mansion.initialize( &_catalog, &_loot, &_areaGraph, refs, seed );
            return true;
        }

        void run( float32 seconds )
        {
            for ( float32 elapsed = 0.0f; elapsed < seconds - 0.001f; elapsed += 0.1f )
            {
                _mansion.update( 0.1f );
            }
        }

        /** @brief 지금 방의 유령 하나를 기절시켜 반대로 당겨 잡습니다. */
        bool catchGhost( uint32 ghostId )
        {
            GhostEncounter& encounter = _mansion.getEncounter();
            for ( int32 tick = 0; tick < 200; ++tick )
            {
                const GhostInstance* pGhost = encounter.findGhost( ghostId );
                if ( pGhost != nullptr && pGhost->_state == GhostState::Attacking )
                    break;
                _mansion.update( 0.1f );
            }
            if ( encounter.shineBeam( kPlayer, kNorth ) == 0 || encounter.startSuction( ghostId, kPlayer ) == false )
                return false;
            for ( int32 tick = 0; tick < 400; ++tick )
            {
                const float3 flee = encounter.findGhost( ghostId )->_fleeDirection;
                if ( encounter.updateSuction( float3{ -flee._x, 0.0f, -flee._z }, 0.1f )._bCaught == SW_TRUE )
                {
                    _mansion.update( 0.1f );
                    return true;
                }
            }
            return false;
        }
    };

    bool hasMansionEvent( const vector<GhostMansionEvent>& listEvent, GhostMansionEventType type )
    {
        for ( const GhostMansionEvent& event : listEvent )
        {
            if ( event._type == type )
                return true;
        }
        return false;
    }
} // namespace

/**
 * @brief [GhostHuntTest] 손전등 — 원뿔(각 · 거리) 경계, 보통 빛은 공격 중(심장이 드러난) 유령만, 스트로브는 다 모아야 넓은 원뿔의 나타난 유령까지 기절시킨다
 */
SW_TEST_CASE( GhostHuntTest, FlashlightConeAndStrobe )
{
    GhostCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kGhostHuntXml, "GhostHuntTest" ) );
    GhostEncounter encounter;
    encounter.initialize( &catalog, 1 );
    const float3 eye{};
    SW_EXPECT_TRUE( encounter.isInCone( eye, kNorth, float3{ 0.0f, 0.0f, 7.9f }, false ) );
    SW_EXPECT_FALSE( encounter.isInCone( eye, kNorth, float3{ 0.0f, 0.0f, 8.1f }, false ) );
    SW_EXPECT_TRUE( encounter.isInCone( eye, kNorth, float3{ 2.0f, 0.0f, 5.0f }, false ) );  // 21.8°
    SW_EXPECT_FALSE( encounter.isInCone( eye, kNorth, float3{ 3.0f, 0.0f, 5.0f }, false ) ); // 31° — 보통 빛 밖
    SW_EXPECT_TRUE( encounter.isInCone( eye, kNorth, float3{ 3.0f, 0.0f, 5.0f }, true ) );   // 스트로브 안
    SW_EXPECT_FALSE( encounter.isInCone( eye, kNorth, float3{ 0.0f, 0.0f, -1.0f }, true ) ); // 등 뒤

    const uint32 nearGoob = encounter.spawnGhost( "goob", float3{ 0.0f, 0.0f, 5.0f } );
    const uint32 shy      = encounter.spawnGhost( "shy", float3{ 0.0f, 0.0f, 5.0f } );
    const uint32 farGoob  = encounter.spawnGhost( "goob", float3{ 0.0f, 0.0f, 9.0f } );
    const uint32 sideGoob = encounter.spawnGhost( "goob", float3{ 4.0f, 0.0f, 4.0f } ); // 45°
    SW_EXPECT_EQUAL( static_cast<uint32>( 0 ), encounter.spawnGhost( "nobody", float3{} ) );
    SW_EXPECT_EQUAL( 0, encounter.shineBeam( eye, kNorth ) ); // 숨은 유령에는 빛이 닿지 않는다
    encounter.update( 2.0f );
    SW_EXPECT_TRUE( encounter.findGhost( nearGoob )->_state == GhostState::Visible );
    SW_EXPECT_EQUAL( 0, encounter.shineBeam( eye, kNorth ) ); // 나타났지만 공격 전 — 보통 빛으로는 안 된다
    encounter.update( 1.5f );
    SW_EXPECT_TRUE( encounter.findGhost( nearGoob )->_state == GhostState::Attacking );
    SW_EXPECT_EQUAL( 1, encounter.shineBeam( eye, kNorth ) );
    SW_EXPECT_TRUE( encounter.findGhost( nearGoob )->_state == GhostState::Stunned );
    SW_EXPECT_TRUE( encounter.findGhost( shy )->_state == GhostState::Attacking ); // 스트로브만 듣는다

    encounter.chargeStrobe( 0.5f );
    SW_EXPECT_EQUAL( 0, encounter.releaseStrobe( eye, kNorth ) ); // 덜 모았다 — 헛번쩍
    SW_EXPECT_NEAR_EQUAL( 0.0f, encounter.getStrobeCharge(), 0.0001f );
    encounter.chargeStrobe( 0.6f );
    encounter.chargeStrobe( 0.6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, encounter.getStrobeCharge(), 0.0001f );
    SW_EXPECT_EQUAL( 2, encounter.releaseStrobe( eye, kNorth ) );
    SW_EXPECT_TRUE( encounter.findGhost( shy )->_state == GhostState::Stunned );
    SW_EXPECT_TRUE( encounter.findGhost( farGoob )->_state == GhostState::Stunned );
    SW_EXPECT_TRUE( encounter.findGhost( sideGoob )->_state == GhostState::Attacking );
}

/**
 * @brief [GhostHuntTest] 유령 순환 — 숨음 → 나타남 → 공격(끝에 맞는다) → 숨음, 기절은 정한 시간 뒤 풀리고, 기절하지 않은 유령은 빨아들일 수 없다
 */
SW_TEST_CASE( GhostHuntTest, GhostCycleAndStunTimeout )
{
    GhostCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kGhostHuntXml, "GhostHuntTest" ) );
    GhostEncounter encounter;
    encounter.initialize( &catalog, 1 );
    const uint32 ghost = encounter.spawnGhost( "goob", float3{} );
    SW_EXPECT_FALSE( encounter.startSuction( ghost, kPlayer ) );
    encounter.update( 1.9f );
    SW_EXPECT_TRUE( encounter.findGhost( ghost )->_state == GhostState::Hidden );
    encounter.update( 0.2f );
    SW_EXPECT_TRUE( encounter.findGhost( ghost )->_state == GhostState::Visible );
    encounter.update( 1.5f );
    SW_EXPECT_TRUE( encounter.findGhost( ghost )->_state == GhostState::Attacking );
    encounter.update( 1.0f );
    SW_EXPECT_TRUE( encounter.findGhost( ghost )->_state == GhostState::Hidden );
    vector<GhostEvent> listEvent;
    encounter.drainEvents( listEvent );
    SW_ASSERT_TRUE( listEvent.size() == 2 );
    SW_EXPECT_TRUE( listEvent[0]._type == GhostEventType::Appeared );
    SW_EXPECT_TRUE( listEvent[1]._type == GhostEventType::AttackLanded );
    SW_EXPECT_NEAR_EQUAL( 8.0f, listEvent[1]._amount, 0.001f );

    SW_ASSERT_TRUE( waitForAttack( encounter, ghost ) );
    SW_EXPECT_EQUAL( 1, encounter.shineBeam( kPlayer, kNorth ) );
    encounter.update( 2.9f );
    SW_EXPECT_TRUE( encounter.findGhost( ghost )->_state == GhostState::Stunned );
    encounter.update( 0.2f );
    SW_EXPECT_TRUE( encounter.findGhost( ghost )->_state == GhostState::Visible ); // 깨어났다
    SW_EXPECT_FALSE( encounter.startSuction( ghost, kPlayer ) );
}

/**
 * @brief [GhostHuntTest] 줄다리기 — 도망 반대로 당기면 보너스 피해로 절반 시간에 잡고 덜 끌려가며(규칙을 끄면 옆으로 당긴 것과 같아진다), 놓으면 체력을 남긴 채 달아난다
 */
SW_TEST_CASE( GhostHuntTest, SuctionTugOfWarRewardsOppositePull )
{
    GhostCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kGhostHuntXml, "GhostHuntTest" ) );

    GhostEncounter opposite;
    opposite.initialize( &catalog, 5 );
    const uint32 oppositeGhost = opposite.spawnGhost( "goob", float3{} );
    SW_ASSERT_TRUE( stunAndGrab( opposite, oppositeGhost ) );
    SW_EXPECT_FALSE( opposite.startSuction( oppositeGhost, kPlayer ) ); // 이미 흡입 중
    float32     oppositeDrag  = 0.0f;
    const int32 oppositeTicks = tugUntilCaught( opposite, oppositeGhost, true, oppositeDrag );

    GhostEncounter sideways;
    sideways.initialize( &catalog, 5 );
    const uint32 sidewaysGhost = sideways.spawnGhost( "goob", float3{} );
    SW_ASSERT_TRUE( stunAndGrab( sideways, sidewaysGhost ) );
    float32     sidewaysDrag  = 0.0f;
    const int32 sidewaysTicks = tugUntilCaught( sideways, sidewaysGhost, false, sidewaysDrag );

    SW_EXPECT_TRUE( 19 <= oppositeTicks && oppositeTicks <= 21 ); // 10 × 2 배 → 40 체력에 2 초
    SW_EXPECT_TRUE( 39 <= sidewaysTicks && sidewaysTicks <= 41 ); // 10 → 4 초
    SW_EXPECT_TRUE( oppositeDrag * 4.0f < sidewaysDrag );
    SW_EXPECT_TRUE( opposite.findGhost( oppositeGhost )->_state == GhostState::Caught );
    SW_EXPECT_EQUAL( 0, opposite.countRemaining() );
    vector<GhostEvent> listEvent;
    opposite.drainEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.back()._type == GhostEventType::Caught );
    SW_EXPECT_EQUAL( 10, listEvent.back()._coins );

    // 놓으면 달아난다 — 깎인 체력은 그대로 남아 다음에 이어서 잡는다.
    GhostEncounter escape;
    escape.initialize( &catalog, 5 );
    const uint32 escapeGhost = escape.spawnGhost( "goob", float3{} );
    SW_ASSERT_TRUE( stunAndGrab( escape, escapeGhost ) );
    for ( int32 tick = 0; tick < 10; ++tick )
    {
        (void)escape.updateSuction( float3{}, 0.1f ); // 당기지 않아도 기본 흡입은 든다
    }
    escape.stopSuction();
    SW_EXPECT_TRUE( escape.findGhost( escapeGhost )->_state == GhostState::Hidden );
    SW_EXPECT_NEAR_EQUAL( 30.0f, escape.findGhost( escapeGhost )->_hp, 0.01f );
    SW_EXPECT_EQUAL( static_cast<uint32>( 0 ), escape.getSuctionTarget() );
    SW_ASSERT_TRUE( stunAndGrab( escape, escapeGhost ) );
    float32 escapeDrag = 0.0f;
    SW_EXPECT_TRUE( tugUntilCaught( escape, escapeGhost, true, escapeDrag ) <= 16 );
}

/**
 * @brief [GhostHuntTest] 강화 · 서지 · 결정성 — 강화 단계가 흡입력을 올리고, 반대로 당겨 채운 서지가 한 번에 큰 피해를 주며, 같은 씨앗 · 입력이면 같은 싸움이다
 */
SW_TEST_CASE( GhostHuntTest, VacuumStageSurgeAndDeterminism )
{
    GhostCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kGhostHuntXml, "GhostHuntTest" ) );
    GhostEncounter encounter;
    encounter.initialize( &catalog, 9 );
    SW_EXPECT_NEAR_EQUAL( 10.0f, encounter.computeVacuumPower(), 0.001f );
    encounter.setVacuumStage( 2 );
    SW_EXPECT_NEAR_EQUAL( 30.0f, encounter.computeVacuumPower(), 0.001f );
    encounter.setVacuumStage( 9 );
    SW_EXPECT_EQUAL( 2, encounter.getVacuumStage() );
    encounter.setVacuumStage( 0 );

    const uint32 ghost = encounter.spawnGhost( "goob", float3{} );
    SW_ASSERT_TRUE( stunAndGrab( encounter, ghost ) );
    SW_EXPECT_FALSE( encounter.triggerSurge() );
    for ( int32 tick = 0; tick < 5; ++tick )
    {
        const float3 flee = encounter.findGhost( ghost )->_fleeDirection;
        (void)encounter.updateSuction( float3{ flee._z, 0.0f, -flee._x }, 0.1f ); // 옆으로 — 서지가 차지 않는다
    }
    SW_EXPECT_NEAR_EQUAL( 0.0f, encounter.getSurgeGauge(), 0.0001f );
    for ( int32 tick = 0; tick < 10; ++tick )
    {
        const float3 flee = encounter.findGhost( ghost )->_fleeDirection;
        (void)encounter.updateSuction( float3{ -flee._x, 0.0f, -flee._z }, 0.1f );
    }
    SW_EXPECT_NEAR_EQUAL( 1.0f, encounter.getSurgeGauge(), 0.001f );
    SW_EXPECT_NEAR_EQUAL( 15.0f, encounter.findGhost( ghost )->_hp, 0.05f ); // 40 − 5 − 20
    SW_EXPECT_TRUE( encounter.triggerSurge() );
    SW_EXPECT_TRUE( encounter.findGhost( ghost )->_state == GhostState::Caught );

    // 같은 씨앗 · 같은 입력 — 도망 방향 순서와 남은 체력이 같다.
    GhostEncounter replayA;
    GhostEncounter replayB;
    replayA.initialize( &catalog, 77 );
    replayB.initialize( &catalog, 77 );
    const uint32 ghostA = replayA.spawnGhost( "goob", float3{} );
    const uint32 ghostB = replayB.spawnGhost( "goob", float3{} );
    SW_ASSERT_TRUE( stunAndGrab( replayA, ghostA ) );
    SW_ASSERT_TRUE( stunAndGrab( replayB, ghostB ) );
    bool bSameFlee = true;
    for ( int32 tick = 0; tick < 25; ++tick )
    {
        (void)replayA.updateSuction( float3{ 1.0f, 0.0f, 0.0f }, 0.1f );
        (void)replayB.updateSuction( float3{ 1.0f, 0.0f, 0.0f }, 0.1f );
        bSameFlee = bSameFlee && replayA.findGhost( ghostA )->_fleeDirection == replayB.findGhost( ghostB )->_fleeDirection;
    }
    SW_EXPECT_TRUE( bSameFlee );
    SW_EXPECT_NEAR_EQUAL( replayA.findGhost( ghostA )->_hp, replayB.findGhost( ghostB )->_hp, 0.0001f );
}

/**
 * @brief [GhostHuntTest] 가구 — 정한 방법으로만 뒤지고 한 번만 주며, 같은 씨앗이면 뒤지는 순서와 상관없이 같은 보물이 나온다
 */
SW_TEST_CASE( GhostHuntTest, FurnitureLootIsDeterministic )
{
    GhostMansionScene sceneA;
    GhostMansionScene sceneB;
    SW_ASSERT_TRUE( sceneA.initialize( 7 ) );
    SW_ASSERT_TRUE( sceneB.initialize( 7 ) );
    ItemStackList curtainA;
    ItemStackList chestA;
    ItemStackList curtainB;
    ItemStackList chestB;
    SW_EXPECT_TRUE( sceneA._mansion.searchFurniture( "curtain", GhostSearchMode::Shake, curtainA ) == GhostSearchResult::WrongMode );
    SW_EXPECT_TRUE( sceneA._mansion.searchFurniture( "curtain", GhostSearchMode::Vacuum, curtainA ) == GhostSearchResult::Found );
    SW_EXPECT_TRUE( sceneA._mansion.searchFurniture( "chest", GhostSearchMode::Shake, chestA ) == GhostSearchResult::Found );
    // 반대 순서로 뒤진다.
    SW_EXPECT_TRUE( sceneB._mansion.searchFurniture( "chest", GhostSearchMode::Vacuum, chestB ) == GhostSearchResult::Found );
    SW_EXPECT_TRUE( sceneB._mansion.searchFurniture( "curtain", GhostSearchMode::Vacuum, curtainB ) == GhostSearchResult::Found );

    SW_EXPECT_TRUE( curtainA.getItemCount( "bill" ) >= 1 );
    SW_EXPECT_EQUAL( curtainA.getItemCount( "bill" ), curtainB.getItemCount( "bill" ) );
    SW_EXPECT_TRUE( chestA.getTotalCount() >= 2 );
    SW_EXPECT_EQUAL( chestA.getItemCount( "coin" ), chestB.getItemCount( "coin" ) );
    SW_EXPECT_EQUAL( chestA.getItemCount( "pearl" ), chestB.getItemCount( "pearl" ) );

    const int32 totalBefore = chestA.getTotalCount();
    SW_EXPECT_TRUE( sceneA._mansion.searchFurniture( "chest", GhostSearchMode::Shake, chestA ) == GhostSearchResult::AlreadySearched );
    SW_EXPECT_EQUAL( totalBefore, chestA.getTotalCount() );
    SW_EXPECT_TRUE( sceneA._mansion.isSearched( "chest" ) );
    SW_EXPECT_FALSE( sceneA._mansion.isSearched( "vase" ) );
    SW_EXPECT_TRUE( sceneA._mansion.searchFurniture( "piano", GhostSearchMode::Shake, chestA ) == GhostSearchResult::UnknownFurniture );
}

/**
 * @brief [GhostHuntTest] 저택 — 방의 유령을 모두 잡아야 불이 켜지고 열쇠가 나오며, 그 열쇠로 연 문이 그래프 길을 연다. 밝은 방에는 유령이 다시 나오지 않는다
 */
SW_TEST_CASE( GhostHuntTest, RoomLightsAndKeyDoor )
{
    GhostMansionScene scene;
    SW_ASSERT_TRUE( scene.initialize( 11 ) );
    GhostMansion& mansion = scene._mansion;
    SW_EXPECT_EQUAL( -1, mansion.enterRoom( "attic" ) );
    SW_EXPECT_EQUAL( 2, mansion.enterRoom( "foyer" ) );
    SW_EXPECT_TRUE( scene._areaGraph.isVisited( "foyer" ) );
    SW_EXPECT_FALSE( mansion.isRoomLit( "foyer" ) );
    SW_EXPECT_TRUE( mansion.unlockDoor( "parlorDoor" ) == GhostDoorResult::NeedKey );
    SW_EXPECT_FALSE( scene._areaGraph.canTraverse( "foyer", "parlor", scene._flags ) );

    const uint32 firstGhost  = mansion.getEncounter().getGhosts()[0]._id;
    const uint32 secondGhost = mansion.getEncounter().getGhosts()[1]._id;
    SW_ASSERT_TRUE( scene.catchGhost( firstGhost ) );
    SW_EXPECT_FALSE( mansion.isRoomLit( "foyer" ) ); // 하나 남았다
    SW_ASSERT_TRUE( scene.catchGhost( secondGhost ) );
    SW_EXPECT_TRUE( mansion.isRoomLit( "foyer" ) );
    SW_EXPECT_TRUE( scene._flags.hasFlag( "lit.foyer" ) );
    SW_EXPECT_EQUAL( 1, scene._bag.getItemCount( "parlorKey" ) );
    SW_EXPECT_EQUAL( int64{ 20 }, scene._wallet.getBalance( "Coins" ) );
    vector<GhostMansionEvent> listEvent;
    mansion.drainEvents( listEvent );
    SW_EXPECT_TRUE( hasMansionEvent( listEvent, GhostMansionEventType::RoomLit ) );
    SW_EXPECT_TRUE( hasMansionEvent( listEvent, GhostMansionEventType::KeyAwarded ) );
    vector<GhostEvent> listGhostEvent;
    mansion.drainGhostEvents( listGhostEvent );
    SW_EXPECT_TRUE( listGhostEvent.empty() == false );

    SW_EXPECT_TRUE( mansion.unlockDoor( "parlorDoor" ) == GhostDoorResult::Opened );
    SW_EXPECT_TRUE( scene._areaGraph.canTraverse( "foyer", "parlor", scene._flags ) );
    SW_EXPECT_TRUE( mansion.unlockDoor( "parlorDoor" ) == GhostDoorResult::AlreadyOpen );
    SW_EXPECT_EQUAL( 0, scene._bag.getItemCount( "parlorKey" ) );
    SW_EXPECT_EQUAL( 0, mansion.enterRoom( "foyer" ) ); // 밝은 방
    SW_EXPECT_EQUAL( 0, mansion.enterRoom( "hall" ) );  // 유령이 없는 방은 들어서면 밝다
    SW_EXPECT_TRUE( mansion.isRoomLit( "hall" ) );
    SW_EXPECT_EQUAL( 1, mansion.enterRoom( "parlor" ) );
}

/**
 * @brief [GhostHuntTest] 열쇠는 플레이어 가방에 산다 — 다른 길(다른 키트 · 상점)로 가방에 들어온 열쇠로도 문이 열리고, 열면 가방에서 빠진다
 */
SW_TEST_CASE( GhostHuntTest, KeysLiveInThePlayerBag )
{
    GhostMansionScene scene;
    SW_ASSERT_TRUE( scene.initialize( 11 ) );
    GhostMansion& mansion = scene._mansion;
    SW_EXPECT_EQUAL( 2, mansion.enterRoom( "foyer" ) );
    SW_EXPECT_TRUE( mansion.unlockDoor( "parlorDoor" ) == GhostDoorResult::NeedKey );
    SW_ASSERT_EQUAL( 1, scene._bag.addItem( "parlorKey", 1 ) ); // 방을 밝히지 않고 가방에 넣었다
    SW_EXPECT_TRUE( mansion.unlockDoor( "parlorDoor" ) == GhostDoorResult::Opened );
    SW_EXPECT_EQUAL( 0, scene._bag.getItemCount( "parlorKey" ) );
    SW_EXPECT_TRUE( scene._areaGraph.canTraverse( "foyer", "parlor", scene._flags ) );

    // 가방을 빌려 주지 않은 저택 — 열쇠가 드는 문은 열리지 않는다
    GhostMansionScene bagless;
    SW_ASSERT_TRUE( bagless.initialize( 11 ) );
    GameStateRefs refs;
    refs._pFlags = &bagless._flags;
    bagless._mansion.initialize( &bagless._catalog, &bagless._loot, &bagless._areaGraph, refs, 11 );
    SW_EXPECT_TRUE( bagless._mansion.unlockDoor( "parlorDoor" ) == GhostDoorResult::NeedKey );
}

/**
 * @brief [GhostHuntTest] 부 — 가구에서 들키고, 시간 안에 못 잡으면 체력을 남긴 채 옆 방 가구로 달아나며(같은 씨앗이면 같은 곳), 거기서 다시 찾아 잡는다
 */
SW_TEST_CASE( GhostHuntTest, BooHidesAndEscapes )
{
    GhostMansionScene sceneA;
    GhostMansionScene sceneB;
    SW_ASSERT_TRUE( sceneA.initialize( 3 ) );
    SW_ASSERT_TRUE( sceneB.initialize( 3 ) );
    ItemStackList loot;
    for ( GhostMansionScene* pScene : { &sceneA, &sceneB } )
    {
        (void)pScene->_mansion.enterRoom( "hall" );
        SW_EXPECT_TRUE( pScene->_mansion.searchFurniture( "dresser", GhostSearchMode::Shake, loot ) == GhostSearchResult::BooFound );
    }
    GhostMansion& mansion = sceneA._mansion;
    SW_EXPECT_TRUE( mansion.findBoo( "booA" )->_state == GhostBooState::Revealed );
    SW_EXPECT_FALSE( mansion.damageBoo( "booA", 10.0f ) );
    sceneA.run( 4.8f );
    SW_EXPECT_TRUE( mansion.findBoo( "booA" )->_state == GhostBooState::Revealed );
    sceneA.run( 0.3f );
    sceneB.run( 5.1f );
    const GhostBooRuntime* pBoo = mansion.findBoo( "booA" );
    SW_ASSERT_NOT_NULL( pBoo );
    SW_EXPECT_TRUE( pBoo->_state == GhostBooState::Hiding );
    SW_EXPECT_FALSE( pBoo->_room == hashed_string( "foyer" ) );
    SW_EXPECT_TRUE( pBoo->_room == hashed_string( "parlor" ) || pBoo->_room == hashed_string( "hall" ) );
    SW_EXPECT_NEAR_EQUAL( 20.0f, pBoo->_hp, 0.001f );
    SW_EXPECT_TRUE( pBoo->_room == sceneB._mansion.findBoo( "booA" )->_room );
    SW_EXPECT_TRUE( pBoo->_furniture == sceneB._mansion.findBoo( "booA" )->_furniture );
    vector<GhostMansionEvent> listEvent;
    mansion.drainEvents( listEvent );
    SW_EXPECT_TRUE( hasMansionEvent( listEvent, GhostMansionEventType::BooEscaped ) );

    // 달아난 곳에서 다시 찾는다 — 가구가 있으면 뒤지고, 없으면 방에 들어서면 나온다.
    if ( pBoo->_furniture.empty() )
        (void)mansion.enterRoom( pBoo->_room );
    else
        SW_EXPECT_TRUE( mansion.searchFurniture( pBoo->_furniture, GhostSearchMode::Shake, loot ) == GhostSearchResult::BooFound );
    SW_EXPECT_TRUE( mansion.findBoo( "booA" )->_state == GhostBooState::Revealed );
    SW_EXPECT_TRUE( mansion.damageBoo( "booA", 20.0f ) );
    SW_EXPECT_EQUAL( 1, mansion.countCaughtBoos() );
    SW_EXPECT_FALSE( mansion.damageBoo( "booA", 5.0f ) );
    // 부가 떠난 서랍은 이제 보물을 준다.
    SW_EXPECT_TRUE( mansion.searchFurniture( "dresser", GhostSearchMode::Shake, loot ) == GhostSearchResult::Found );
}

/**
 * @brief [GhostHuntTest] 상태 바이트로 되살린 저택이 같은 판을 잇는다 — 싸움 중인 유령 · 들킨 부 · 뒤진 가구 · 지금 방이 같은 바이트이고,
 *        같은 걸음을 둘 다 더 돌려도(유령 순환 · 부의 탈출 난수) 같은 바이트다. 잘린 바이트는 거절하고 그대로 둔다
 */
SW_TEST_CASE( GhostHuntTest, StateRoundTripContinuesTheSameMansion )
{
    GhostMansionScene scene;
    SW_ASSERT_TRUE( scene.initialize( 3 ) );
    GhostMansion& mansion = scene._mansion;
    ItemStackList loot;
    (void)mansion.enterRoom( "hall" );
    SW_EXPECT_TRUE( mansion.searchFurniture( "dresser", GhostSearchMode::Shake, loot ) == GhostSearchResult::BooFound );
    SW_EXPECT_EQUAL( 2, mansion.enterRoom( "foyer" ) );
    SW_EXPECT_TRUE( mansion.searchFurniture( "curtain", GhostSearchMode::Vacuum, loot ) == GhostSearchResult::Found ); // 부를 찾은 가구는 뒤진 것으로 치지 않는다
    scene.run( 1.0f );

    GameStateRefs refs;
    refs._pFlags     = &scene._flags;
    refs._pInventory = &scene._bag;
    refs._pWallet    = &scene._wallet;
    Archive written;
    mansion.writeState( written );
    GhostMansion restored;
    restored.initialize( &scene._catalog, &scene._loot, &scene._areaGraph, refs, 99 );
    Archive reader( written.getData(), written.getSize() );
    SW_ASSERT_TRUE( restored.readState( reader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, reader.getRemainingBytes() );
    Archive rewritten;
    restored.writeState( rewritten );
    vector<uint8> originalBytes;
    vector<uint8> restoredBytes;
    written.writeData( originalBytes );
    rewritten.writeData( restoredBytes );
    SW_EXPECT_TRUE( originalBytes == restoredBytes );
    SW_EXPECT_TRUE( restored.getCurrentRoom() == hashed_string( "foyer" ) );
    SW_EXPECT_TRUE( restored.isSearched( "curtain" ) );
    SW_EXPECT_TRUE( restored.findBoo( "booA" )->_state == GhostBooState::Revealed );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( restored.getEncounter().getGhosts().size() ) );

    // 같은 걸음을 둘 다 — 유령이 순환하고 부가 시간이 다 되어 같은 곳으로 달아난다.
    for ( int32 tick = 0; tick < 45; ++tick )
    {
        mansion.update( 0.1f );
        restored.update( 0.1f );
    }
    SW_EXPECT_TRUE( restored.findBoo( "booA" )->_state == GhostBooState::Hiding );
    SW_EXPECT_TRUE( mansion.findBoo( "booA" )->_room == restored.findBoo( "booA" )->_room );
    Archive afterOriginal;
    Archive afterRestored;
    mansion.writeState( afterOriginal );
    restored.writeState( afterRestored );
    afterOriginal.writeData( originalBytes );
    afterRestored.writeData( restoredBytes );
    SW_EXPECT_TRUE( originalBytes == restoredBytes );

    GhostMansion truncated;
    truncated.initialize( &scene._catalog, &scene._loot, &scene._areaGraph, refs, 99 );
    Archive cut( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( truncated.readState( cut ) );
    SW_EXPECT_TRUE( truncated.getCurrentRoom().empty() );
    SW_EXPECT_FALSE( truncated.isSearched( "curtain" ) );
}
