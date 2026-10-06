/**
 * @file TestBaseValueStateArchive.cpp
 * @brief 키트가 품는 기반 값 타입(타이머 · 게이지 · 체력 · 차례 · 무기 · 기술 · 이동기 …)의 상태 바이트 왕복 시험입니다.
 * @details 타입마다 한 사례 — 움직인 뒤 쓰고, 새 객체에 읽고, 둘을 같은 걸음만큼 더 돌려 바이트가 같은지 봅니다.
 */
#include "pch.h"

#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/AI/SpawnDirector.h"
#include "GameFramework/Base/Combat/FrameData.h"
#include "GameFramework/Base/Combat/LockOnSelector.h"
#include "GameFramework/Base/Combat/ResourceGauge.h"
#include "GameFramework/Base/Combat/TurnOrder.h"
#include "GameFramework/Base/Combat/Vitality.h"
#include "GameFramework/Base/Combat/Weapon.h"
#include "GameFramework/Base/Data/StatBlock.h"
#include "GameFramework/Base/Gimmick/ElementGrid.h"
#include "GameFramework/Base/Gimmick/ElementRuleTable.h"
#include "GameFramework/Base/Input/TimingJudge.h"
#include "GameFramework/Base/Interaction/InteractionProgress.h"
#include "GameFramework/Base/Inventory/Crafting.h"
#include "GameFramework/Base/Inventory/GridInventory.h"
#include "GameFramework/Base/Inventory/Inventory.h"
#include "GameFramework/Base/Inventory/ItemCatalog.h"
#include "GameFramework/Base/Inventory/Shop.h"
#include "GameFramework/Base/Match/MatchState.h"
#include "GameFramework/Base/Movement/ArcadeVehicleMotor.h"
#include "GameFramework/Base/Movement/PlatformerMotor2D.h"
#include "GameFramework/Base/Progression/LevelProgress.h"
#include "GameFramework/Base/Progression/RunMap.h"
#include "GameFramework/Base/Utility/Countdown.h"
#include "GameFramework/Base/Utility/RayMath.h"
#include "GameFramework/Base/Utility/StateArchiveUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr float32 kValueStateStep = 1.0f / 60.0f;

    constexpr const utf8* kValueStateItemXml = R"(
<ItemCatalog>
  <Item id="potion" category="Consumable" maxStack="10" value="12"/>
  <Item id="herb" category="Material" maxStack="99" value="2"/>
</ItemCatalog>
)";

    constexpr const utf8* kValueStateShopXml = R"(
<ShopCatalog>
  <Shop id="general" restockDays="2">
    <Stock item="potion" price="20" count="5" restock="3"/>
  </Shop>
</ShopCatalog>
)";

    constexpr const utf8* kValueStateRecipeXml = R"(
<RecipeCatalog>
  <Recipe id="brew" time="2"><In item="herb" count="1"/><Out item="potion" count="1"/></Recipe>
</RecipeCatalog>
)";

    constexpr const utf8* kValueStateSpawnXml = R"(
<SpawnTable budgetPerMinute="120" maxBudget="6" startBudget="2">
  <Entry id="grunt" cost="1" weight="1" max="50" tags="Common"/>
  <Entry id="boss" cost="4" weight="1" max="1" tags="Special"/>
</SpawnTable>
)";

    constexpr const utf8* kValueStateElementTablePath = "common/data/elements/default.elements.xml";

    constexpr const utf8* kValueStateLevelText = R"(
#            #
#            #
##############
)";

    /** @brief 상태 하나의 바이트입니다. */
    template <typename TState>
    vector<uint8> captureValueBytes( const TState& state )
    {
        Archive archive;
        state.writeState( archive );
        vector<uint8> bytes;
        archive.writeData( bytes );
        return bytes;
    }

    /**
     * @brief @p original 의 바이트를 @p inoutRestored 에 읽고(끝까지 다 읽어야 한다), 둘을 @p step 으로 같은 만큼 더 돌린 뒤 바이트가 같으면 true 입니다.
     */
    template <typename TState, typename TStep>
    bool replaysAfterRestore( TState& original, TState& inoutRestored, TStep step )
    {
        const vector<uint8> bytes = captureValueBytes( original );
        Archive             archive( bytes.data(), bytes.size() );
        if ( inoutRestored.readState( archive ) == false || archive.getRemainingBytes() != 0 )
            return false;
        if ( captureValueBytes( inoutRestored ) != bytes )
            return false;
        step( original );
        step( inoutRestored );
        return captureValueBytes( original ) == captureValueBytes( inoutRestored );
    }

    /** @brief 잘린 바이트는 거절하는가입니다(앞 절반만). */
    template <typename TState>
    bool rejectsTruncated( const TState& original, TState& inoutTarget )
    {
        vector<uint8> bytes = captureValueBytes( original );
        bytes.resize( bytes.size() / 2 );
        Archive archive( bytes.data(), bytes.size() );
        return inoutTarget.readState( archive ) == false;
    }
} // namespace

/**
 * @brief [BaseValueStateArchiveTest] 카운트다운은 음수 늦음까지, 몫 누적은 남은 몫을 그대로 싣는다
 */
SW_TEST_CASE( BaseValueStateArchiveTest, CountdownKeepsLatenessAndAccumulatorKeepsFraction )
{
    Countdown countdown( 1.5f );
    (void)countdown.tick( 2.0f ); // −0.5 — 이번 걸음에 끝나고 0.5 초 늦었다
    RateAccumulator accumulator;
    accumulator.add( 2.75f );
    (void)accumulator.takeOne();

    Archive archive;
    StateArchiveUtil::writeCountdown( archive, countdown );
    StateArchiveUtil::writeRateAccumulator( archive, accumulator );
    vector<uint8> bytes;
    archive.writeData( bytes );

    Archive         reader( bytes.data(), bytes.size() );
    Countdown       restoredCountdown;
    RateAccumulator restoredAccumulator;
    SW_ASSERT_TRUE( StateArchiveUtil::readCountdown( reader, restoredCountdown ) );
    SW_ASSERT_TRUE( StateArchiveUtil::readRateAccumulator( reader, restoredAccumulator ) );
    SW_EXPECT_NEAR_EQUAL( -0.5f, restoredCountdown._remaining, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.75f, restoredAccumulator.getFraction(), 1e-6f );
}

/**
 * @brief [BaseValueStateArchiveTest] 자원 게이지 — 쓴 뒤 되살려 같은 회복을 낸다
 */
SW_TEST_CASE( BaseValueStateArchiveTest, ResourceGaugeReplaysAfterRestore )
{
    ResourceGaugeSettings settings;
    settings._exhaustThreshold = 30.0f;
    ResourceGauge gauge( settings );
    (void)gauge.drain( 400.0f, 0.5f ); // 탈진
    gauge.update( 0.4f );
    ResourceGauge restored( settings );
    SW_EXPECT_TRUE( replaysAfterRestore( gauge, restored, []( ResourceGauge& state )
    {
        for ( int32 stepIndex = 0; stepIndex < 120; ++stepIndex )
            state.update( kValueStateStep );
    } ) );
    SW_EXPECT_TRUE( restored.isExhausted() == gauge.isExhausted() );
    ResourceGauge untouched( settings );
    SW_EXPECT_TRUE( rejectsTruncated( gauge, untouched ) );
}

/**
 * @brief [BaseValueStateArchiveTest] 체력 — 피해 · 강인도 · 무적이 이어진다
 */
SW_TEST_CASE( BaseValueStateArchiveTest, VitalityReplaysAfterRestore )
{
    VitalitySettings settings;
    settings._maxShield = 50.0f;
    Vitality vitality( settings );
    (void)vitality.applyDamage( 30.0f, 5.0f, 7 );
    (void)vitality.addShield( 12.0f );
    vitality.setInvulnerable( 0.25f );
    vitality.update( 0.1f );
    Vitality restored( settings );
    SW_EXPECT_TRUE( replaysAfterRestore( vitality, restored, []( Vitality& state )
    {
        for ( int32 stepIndex = 0; stepIndex < 30; ++stepIndex )
            state.update( kValueStateStep );
        (void)state.applyDamage( 20.0f, 3.0f, 9 );
    } ) );
    SW_EXPECT_NEAR_EQUAL( vitality.getHealth(), restored.getHealth(), 1e-6f );
}

/**
 * @brief [BaseValueStateArchiveTest] 차례 — 타임라인 게이지 · 난수가 이어져 같은 다음 차례들을 낸다
 */
SW_TEST_CASE( BaseValueStateArchiveTest, TurnOrderReplaysAfterRestore )
{
    TurnOrder order;
    order.initialize( TurnOrderMode::Timeline, 7 );
    order.addActor( 1, 1.0f );
    order.addActor( 2, 1.5f );
    order.addActor( 3, 0.7f );
    for ( int32 turn = 0; turn < 4; ++turn )
        (void)order.next();
    TurnOrder restored;
    restored.initialize( TurnOrderMode::Rounds, 1 );
    vector<int32> listOriginal;
    vector<int32> listRestored;
    SW_EXPECT_TRUE( replaysAfterRestore( order, restored, []( TurnOrder& state )
    {
        for ( int32 turn = 0; turn < 6; ++turn )
            (void)state.next();
    } ) );
    order.previewOrder( 5, listOriginal );
    restored.previewOrder( 5, listRestored );
    SW_EXPECT_TRUE( listOriginal == listRestored );
}

/**
 * @brief [BaseValueStateArchiveTest] 락온 — 표적 · 숨은 시간이 온다
 */
SW_TEST_CASE( BaseValueStateArchiveTest, LockOnReplaysAfterRestore )
{
    vector<LockOnCandidate> listCandidate( 2 );
    listCandidate[0]._position = float3{ 0.0f, 0.0f, 10.0f };
    listCandidate[0]._id       = 11;
    listCandidate[1]._position = float3{ 2.0f, 0.0f, 8.0f };
    listCandidate[1]._id       = 12;
    LockOnSelector selector;
    (void)selector.pickBest( float3{}, float3{ 0.0f, 0.0f, 1.0f }, listCandidate );
    listCandidate[0]._bVisible = SW_FALSE;
    listCandidate[1]._bVisible = SW_FALSE;
    (void)selector.update( float3{}, listCandidate, 0.2f );
    LockOnSelector restored;
    SW_EXPECT_TRUE( replaysAfterRestore( selector, restored, [&listCandidate]( LockOnSelector& state )
    {
        (void)state.update( float3{}, listCandidate, 0.2f );
    } ) );
    SW_EXPECT_EQUAL( selector.getTarget(), restored.getTarget() );
}

/**
 * @brief [BaseValueStateArchiveTest] 무기 — 탄창 · 쿨다운 · 퍼짐 난수가 이어지고, 다른 무기를 든 상태에는 읽히지 않는다
 */
SW_TEST_CASE( BaseValueStateArchiveTest, WeaponReplaysAfterRestoreAndRejectsOtherWeapon )
{
    WeaponDef smg;
    smg._id           = "smg";
    smg._magazineSize = 10;
    WeaponState weapon;
    weapon.equip( smg, 30, 5u );
    GameRay    aim;
    WeaponShot shot;
    (void)weapon.pullTrigger( aim, true, shot );
    weapon.update( 0.05f );
    (void)weapon.pullTrigger( aim, false, shot );

    WeaponState restored;
    restored.equip( smg, 0, 1u );
    SW_EXPECT_TRUE( replaysAfterRestore( weapon, restored, [&aim]( WeaponState& state )
    {
        WeaponShot stepShot;
        for ( int32 stepIndex = 0; stepIndex < 20; ++stepIndex )
        {
            state.update( kValueStateStep );
            (void)state.pullTrigger( aim, stepIndex == 0, stepShot );
        }
    } ) );

    WeaponDef bow;
    bow._id = "bow";
    WeaponState other;
    other.equip( bow, 3, 1u );
    const vector<uint8> bytes = captureValueBytes( weapon );
    Archive             archive( bytes.data(), bytes.size() );
    SW_EXPECT_FALSE( other.readState( archive ) );
    SW_EXPECT_EQUAL( 3, other.getReserveAmmo() );
}

/**
 * @brief [BaseValueStateArchiveTest] 기술 타임라인 — 기술은 카탈로그에서 id 로 찾고 프레임 · 히트스톱 · 접촉이 이어진다
 */
SW_TEST_CASE( BaseValueStateArchiveTest, MoveTimelineReplaysAfterRestore )
{
    MoveFrameData jab;
    jab._id       = "jab";
    jab._startup  = 3;
    jab._active   = 2;
    jab._recovery = 6;
    MoveCatalog catalog;
    catalog.addMove( jab );

    MoveTimeline timeline;
    timeline.start( jab );
    (void)timeline.advanceFrame();
    (void)timeline.advanceFrame();
    timeline.registerContact( true );
    timeline.applyHitstop( 2 );
    const vector<uint8> bytes = captureValueBytes( timeline );

    MoveTimeline restored;
    Archive      archive( bytes.data(), bytes.size() );
    SW_ASSERT_TRUE( restored.readState( archive, catalog ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, archive.getRemainingBytes() );
    for ( int32 frame = 0; frame < 5; ++frame )
    {
        (void)timeline.advanceFrame();
        (void)restored.advanceFrame();
    }
    SW_EXPECT_TRUE( captureValueBytes( timeline ) == captureValueBytes( restored ) );
    SW_EXPECT_TRUE( restored.wasBlocked() );

    MoveCatalog  emptyCatalog;
    Archive      unknown( bytes.data(), bytes.size() );
    MoveTimeline other;
    SW_EXPECT_FALSE( other.readState( unknown, emptyCatalog ) );
}

/**
 * @brief [BaseValueStateArchiveTest] 2D 플랫포머 이동기 — 점프 중간에 되살려 같은 궤적을 낸다
 */
SW_TEST_CASE( BaseValueStateArchiveTest, PlatformerMotorReplaysAfterRestore )
{
    PlatformTileMap map;
    map.loadFromText( kValueStateLevelText, 1.0f, float2{ 0.0f, 0.0f } );
    PlatformerInput input;
    input._move         = float2{ 1.0f, 0.0f };
    input._bJumpPressed = SW_TRUE;
    input._bJumpHeld    = SW_TRUE;

    PlatformerMotor2D motor;
    motor.setSettings( PlatformerSettings{} );
    motor.setPosition( float2{ 3.5f, 1.45f } );
    for ( int32 frame = 0; frame < 8; ++frame )
        motor.update( map, frame < 3 ? PlatformerInput{} : input, kValueStateStep );

    PlatformerMotor2D restored;
    restored.setSettings( PlatformerSettings{} );
    SW_EXPECT_TRUE( replaysAfterRestore( motor, restored, [&map, &input]( PlatformerMotor2D& state )
    {
        for ( int32 frame = 0; frame < 40; ++frame )
            state.update( map, input, kValueStateStep );
    } ) );
    SW_EXPECT_NEAR_EQUAL( motor.getPosition()._y, restored.getPosition()._y, 1e-6f );
}

/**
 * @brief [BaseValueStateArchiveTest] 아케이드 차 — 고정 걸음 남은 시간 · 드리프트 · 부스트가 이어진다
 */
SW_TEST_CASE( BaseValueStateArchiveTest, ArcadeVehicleReplaysAfterRestore )
{
    ArcadeVehicleInput input;
    input._throttle   = 1.0f;
    input._steer      = 0.4f;
    input._bDriftHeld = SW_TRUE;

    ArcadeVehicleMotor motor;
    motor.reset( float3{}, 0.0f );
    motor.startBoost( 0.5f );
    (void)motor.advance( input, 0.37f );
    vector<ArcadeVehicleEvent> listEvent;
    motor.drainEvents( listEvent );

    ArcadeVehicleMotor restored;
    restored.reset( float3{ 5.0f, 0.0f, 5.0f }, 1.0f );
    SW_EXPECT_TRUE( replaysAfterRestore( motor, restored, [&input]( ArcadeVehicleMotor& state )
    {
        (void)state.advance( input, 0.51f );
    } ) );
    SW_EXPECT_NEAR_EQUAL( motor.getYaw(), restored.getYaw(), 1e-6f );
}

/**
 * @brief [BaseValueStateArchiveTest] 가게 — 재고 · 가격 배율 · 재입고 날이 가게 id 로 온다
 */
SW_TEST_CASE( BaseValueStateArchiveTest, ShopStateReplaysAfterRestore )
{
    ItemCatalog items;
    ShopCatalog shops;
    SW_ASSERT_TRUE( items.loadFromXmlText( kValueStateItemXml, "BaseValueStateArchiveTest" ) );
    SW_ASSERT_TRUE( shops.loadFromXmlText( kValueStateShopXml, "BaseValueStateArchiveTest" ) );
    Inventory inventory;
    inventory.initialize( &items, 4 );
    Wallet wallet;
    wallet.add( "Gold", 200 );

    ShopState shop;
    shop.initialize( &shops, &items );
    SW_ASSERT_TRUE( shop.buy( "general", "potion", 3, wallet, inventory ) == ShopResult::Ok );
    shop.setPriceModifier( "general", 1.5f, 0.5f );
    shop.advanceDay();

    ShopState restored;
    restored.initialize( &shops, &items );
    SW_EXPECT_TRUE( replaysAfterRestore( shop, restored, []( ShopState& state )
    { state.advanceDay(); } ) );
    SW_EXPECT_EQUAL( shop.getStockCount( "general", "potion" ), restored.getStockCount( "general", "potion" ) );
    SW_EXPECT_EQUAL( shop.computeBuyPrice( "general", "potion" ), restored.computeBuyPrice( "general", "potion" ) );
}

/**
 * @brief [BaseValueStateArchiveTest] 제작 — 배운 레시피 · 대기열 남은 시간이 이어져 같은 때 끝난다
 */
SW_TEST_CASE( BaseValueStateArchiveTest, CrafterReplaysAfterRestore )
{
    ItemCatalog   items;
    RecipeCatalog recipes;
    SW_ASSERT_TRUE( items.loadFromXmlText( kValueStateItemXml, "BaseValueStateArchiveTest" ) );
    SW_ASSERT_TRUE( recipes.loadFromXmlText( kValueStateRecipeXml, "BaseValueStateArchiveTest" ) );
    Inventory inventory;
    inventory.initialize( &items, 4 );
    (void)inventory.addItem( "herb", 5 );

    Crafter crafter;
    crafter.initialize( &recipes );
    crafter.learnRecipe( "brew" );
    crafter.learnRecipe( "a.secret" );
    SW_ASSERT_TRUE( crafter.enqueue( "brew", inventory, {}, 1, 3 ) == CraftResult::Ok );
    vector<hashed_string> listFinished;
    crafter.update( 2.5f, inventory, listFinished );

    Crafter restored;
    restored.initialize( &recipes );
    SW_EXPECT_TRUE( replaysAfterRestore( crafter, restored, [&items]( Crafter& state )
    {
        Inventory stepInventory;
        stepInventory.initialize( &items, 4 );
        vector<hashed_string> listStepFinished;
        state.update( 1.0f, stepInventory, listStepFinished );
    } ) );
    SW_EXPECT_TRUE( restored.isLearned( "brew" ) );
    SW_EXPECT_EQUAL( crafter.getQueue().size(), restored.getQueue().size() );
}

/**
 * @brief [BaseValueStateArchiveTest] 능력치 묶음 · 레벨 진행 — 값이 그대로 오고 이어서 더한다
 */
SW_TEST_CASE( BaseValueStateArchiveTest, StatBlockAndLevelProgressReplayAfterRestore )
{
    StatBlock stats;
    stats.setValue( "Strength", 7.0f );
    stats.addValue( "Speed", 1.5f );
    StatBlock restoredStats;
    restoredStats.setValue( "Other", 1.0f );
    SW_EXPECT_TRUE( replaysAfterRestore( stats, restoredStats, []( StatBlock& state )
    { state.addValue( "Strength", 2.0f ); } ) );
    SW_EXPECT_FALSE( restoredStats.hasValue( "Other" ) );

    ExperienceCurve curve;
    curve.setFormula( 100.0f, 1.5f, 0.0f, 50 );
    LevelProgress progress;
    (void)progress.addXp( curve, 350 );
    LevelProgress restoredProgress;
    SW_EXPECT_TRUE( replaysAfterRestore( progress, restoredProgress, [&curve]( LevelProgress& state )
    { (void)state.addXp( curve, 420 ); } ) );
    SW_EXPECT_EQUAL( progress.getLevel(), restoredProgress.getLevel() );
}

/**
 * @brief [BaseValueStateArchiveTest] 런 지도 — 만든 지도 · 지금 자리가 와서 같은 갈림길을 낸다
 */
SW_TEST_CASE( BaseValueStateArchiveTest, RunMapReplaysAfterRestore )
{
    RunMapSettings settings;
    RunNodeRule    fight;
    fight._kind = "Fight";
    RunNodeRule rest;
    rest._kind            = "Rest";
    rest._weight          = 0.3f;
    settings._listRule    = { fight, rest };
    settings._floorCount  = 6;
    settings._columnCount = 4;
    settings._pathCount   = 3;
    RunMap map;
    map.generate( settings, 17 );
    vector<int32> listChoice;
    map.collectChoices( listChoice );
    SW_ASSERT_FALSE( listChoice.empty() );
    SW_ASSERT_TRUE( map.moveTo( listChoice[0] ) );

    RunMap restored;
    SW_EXPECT_TRUE( replaysAfterRestore( map, restored, []( RunMap& state )
    {
        vector<int32> listStepChoice;
        state.collectChoices( listStepChoice );
        if ( listStepChoice.empty() == false )
            (void)state.moveTo( listStepChoice.back() );
    } ) );
    SW_EXPECT_EQUAL( map.getCurrent(), restored.getCurrent() );
}

/**
 * @brief [BaseValueStateArchiveTest] 원소 격자 — 번지는 불 한가운데서 되살려 같은 칸을 태운다
 */
SW_TEST_CASE( BaseValueStateArchiveTest, ElementGridReplaysAfterRestore )
{
    ElementRuleTable table;
    SW_ASSERT_TRUE( ResourceUtil::initialize() && table.loadFromResource( kValueStateElementTablePath ) );
    ElementGrid grid;
    grid.initialize( 8, 3, &table );
    for ( int32 y = 0; y < 3; ++y )
    {
        for ( int32 x = 0; x < 8; ++x )
            grid.setMaterial( int2{ x, y }, table.findMaterial( "Grass" ) );
    }
    grid.setWind( int2{ 1, 0 } );
    (void)grid.applyStimulus( int2{ 0, 1 }, table.findStimulus( "Fire" ) );
    (void)grid.update( 0.37f );

    // 크기가 다른 격자는 거절하고 그대로다.
    ElementGrid smaller;
    smaller.initialize( 2, 2, &table );
    const vector<uint8> bytes = captureValueBytes( grid );
    Archive             smallerReader( bytes.data(), bytes.size() );
    SW_EXPECT_FALSE( smaller.readState( smallerReader ) );
    SW_EXPECT_EQUAL( 2, smaller.getWidth() );

    ElementGrid restored;
    restored.initialize( 8, 3, &table );
    SW_EXPECT_TRUE( replaysAfterRestore( grid, restored, []( ElementGrid& state )
    { (void)state.update( 0.9f ); } ) );
    SW_EXPECT_EQUAL( grid.computeStateHash(), restored.computeStateHash() );
}

/**
 * @brief [BaseValueStateArchiveTest] 스폰 디렉터 — 예산 · 난수 · 살아 있는 것이 이어져 같은 다음 스폰을 낸다
 */
SW_TEST_CASE( BaseValueStateArchiveTest, SpawnDirectorReplaysAfterRestore )
{
    SpawnTable table;
    SW_ASSERT_TRUE( table.loadFromXmlText( kValueStateSpawnXml, "BaseValueStateArchiveTest" ) );
    SpawnDirector director;
    director.initialize( &table, 3 );
    (void)director.update( 2.0f );
    (void)director.notifyDespawned( 1 );

    SpawnDirector restored;
    restored.initialize( &table, 99 );
    SW_EXPECT_TRUE( replaysAfterRestore( director, restored, []( SpawnDirector& state )
    {
        for ( int32 stepIndex = 0; stepIndex < 10; ++stepIndex )
            (void)state.update( 0.5f );
    } ) );
    SW_EXPECT_EQUAL( director.getTotalAliveCount(), restored.getTotalAliveCount() );
}

/**
 * @brief [BaseValueStateArchiveTest] 격자 가방 — 칸 · 아이템 번호가 와서 다음 번호가 겹치지 않는다
 */
SW_TEST_CASE( BaseValueStateArchiveTest, GridInventoryReplaysAfterRestore )
{
    const GridInventory::ShapeDelegate shapeLookup = GridInventory::ShapeDelegate::create( []( const hashed_string& itemId, GridItemShape& outShape )
    {
        if ( itemId == hashed_string( "rifle" ) )
        {
            outShape._width  = 3;
            outShape._height = 1;
            return true;
        }
        outShape._maxStack = 30;
        return true;
    } );
    GridInventory                      grid;
    grid.initialize( shapeLookup, 4, 3 );
    SW_ASSERT_TRUE( grid.placeItem( "rifle", 1, 0, 0, false ) > 0 );
    SW_ASSERT_EQUAL( 45, grid.addItem( "ammo", 45 ) );

    GridInventory restored;
    restored.initialize( shapeLookup, 4, 3 );
    SW_EXPECT_TRUE( replaysAfterRestore( grid, restored, []( GridInventory& state )
    { (void)state.addItem( "ammo", 20 ); } ) );
    SW_EXPECT_EQUAL( grid.getItemCount( "ammo" ), restored.getItemCount( "ammo" ) );
}

/**
 * @brief [BaseValueStateArchiveTest] 경기 — 팀 · 참가자 · 단계가 와서 같은 끝을 낸다
 */
SW_TEST_CASE( BaseValueStateArchiveTest, MatchStateReplaysAfterRestore )
{
    MatchSettings settings;
    MatchState    match;
    match.initialize( settings );
    const int32 red   = match.addTeam( "Red" );
    const int32 blue  = match.addTeam( "Blue" );
    const int32 redA  = match.addParticipant( red, "Striker" );
    const int32 blueA = match.addParticipant( blue, "Striker" );
    match.start();
    match.update( 1.0f );
    match.reportDamage( redA, blueA, 30.0f );
    match.reportKill( blueA, redA );
    match.addScore( red, 2 );

    MatchState restored;
    restored.initialize( settings );
    SW_EXPECT_TRUE( replaysAfterRestore( match, restored, [redA, blueA]( MatchState& state )
    {
        state.update( 0.5f );
        state.reportKill( redA, blueA );
    } ) );
    SW_EXPECT_TRUE( match.getPhase() == restored.getPhase() );
}

/**
 * @brief [BaseValueStateArchiveTest] 진행형 상호작용 — 참가자 순서 · 진행량 · 시계 · 스킬 체크(대상 · 목표 시각 · 난수) · 씨앗이 와서, 응답 없는 체크의
 *        실패 · 다음 체크 예약과 떠난 뒤의 퇴행까지 같은 값을 낸다
 */
SW_TEST_CASE( BaseValueStateArchiveTest, InteractionProgressReplaysAfterRestore )
{
    TimingJudge judge;
    SW_ASSERT_TRUE( judge.loadFromXmlText( R"(<TimingWindows><Window grade="Good" early="0.15" late="0.15"/></TimingWindows>)", "ValueState" ) );
    InteractionConfig config;
    config._duration           = 10.0f;
    config._maxParticipants    = 2;
    config._regressionRate     = 0.05f;
    config._skillCheckInterval = 1.5f;
    InteractionProgress progress;
    progress.initialize( config, &judge, 77u );
    SW_ASSERT_TRUE( progress.join( 3u ) );
    SW_ASSERT_TRUE( progress.join( 5u ) );
    progress.update( 2.0f );
    SW_ASSERT_TRUE( progress.leave( 3u ) );

    InteractionProgress restored;
    restored.initialize( config, &judge, 1u );
    SW_EXPECT_TRUE( replaysAfterRestore( progress, restored, []( InteractionProgress& state )
    {
        for ( int32 tick = 0; tick < 24; ++tick )
            state.update( 0.25f ); // 응답 없는 체크가 실패하고 다음 체크가 난수로 예약된다
        (void)state.leave( 5u );
        state.update( 3.0f );
    } ) );
    SW_EXPECT_NEAR_EQUAL( progress.getProgress(), restored.getProgress(), 1.0e-6f );

    InteractionProgress truncated;
    truncated.initialize( config, &judge, 1u );
    SW_EXPECT_TRUE( rejectsTruncated( progress, truncated ) );
    SW_EXPECT_EQUAL( 0, truncated.getParticipantCount() );
}
