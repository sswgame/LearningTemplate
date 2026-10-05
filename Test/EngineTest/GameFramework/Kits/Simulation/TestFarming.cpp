#include "pch.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Inventory/Inventory.h"
#include "GameFramework/Base/Inventory/ItemCatalog.h"
#include "GameFramework/Base/Inventory/Shop.h"
#include "GameFramework/Base/World/LandRegistry.h"
#include "GameFramework/Kits/Simulation/Farming/CropCatalog.h"
#include "GameFramework/Kits/Simulation/Farming/FarmField.h"
#include "GameFramework/Kits/Simulation/Farming/FarmShippingBin.h"

#include "TestFramework/TestFramework.h"

// 농장 키트(하베스트 문 장르) — 물 준 날만 자라는 작물, 다시 자라는 작물, 계절(공유 시계의 이름)이 바뀌면 시듦, 비, 빌린 가방 · 지갑으로 출하 정산.

using namespace sw;

namespace
{
    constexpr const utf8* kFarmingTestCropXml = R"(
<CropCatalog>
  <Crop id="turnip" name="Turnip" seed="turnip_seed" produce="turnip" days="3" seasons="Spring" seedPrice="20" sellPrice="60"/>
  <Crop id="tomato" name="Tomato" seed="tomato_seed" produce="tomato" days="4" regrow="2" seasons="Spring, Summer" seedPrice="40" sellPrice="30" harvest="2"/>
  <Crop id="ghost" seed="ghost_seed" seasons="Monsoon"/>
</CropCatalog>
)";

    /** @brief 칸을 갈고 물 주고 씨를 심습니다. */
    void prepareFarmTile( FarmField& field, int32 x, int32 y, const hashed_string& seed )
    {
        (void)field.till( x, y );
        (void)field.water( x, y );
        (void)field.plant( x, y, seed, "Spring" );
    }
} // namespace

/**
 * @brief [FarmingTest] 작물은 물 받은 날만 자라고 정한 날 수만큼 자라야 거둔다 — 갈지 않은 땅에는 심을 수 없다
 */
SW_TEST_CASE( FarmingTest, CropsGrowOnlyOnWateredDays )
{
    CropCatalog catalog;
    catalog.setKnownSeasons( { "Spring", "Summer", "Fall", "Winter" } );
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kFarmingTestCropXml, "FarmingTest" ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), catalog.getCrops().size() ); // 모르는 계절(Monsoon)만 적힌 ghost 는 빠졌다

    FarmField field;
    field.initialize( 4, 4, &catalog );
    SW_EXPECT_TRUE( field.plant( 0, 0, "turnip_seed", "Spring" ) == FarmActionResult::NotTilled );
    SW_EXPECT_TRUE( field.till( 9, 9 ) == FarmActionResult::OutOfBounds );
    prepareFarmTile( field, 0, 0, "turnip_seed" );
    SW_EXPECT_TRUE( field.plant( 0, 0, "turnip_seed", "Spring" ) == FarmActionResult::Occupied );
    SW_EXPECT_TRUE( field.water( 0, 0 ) == FarmActionResult::AlreadyWatered );

    hashed_string produce;
    int32         count = 0;
    field.advanceDay( "Spring", false ); // 자람 1
    field.advanceDay( "Spring", false ); // 물을 안 줬다 — 그대로
    SW_EXPECT_NEAR_EQUAL( 1.0f / 3.0f, field.computeGrowthRatio( 0, 0 ), 1.0e-4f );
    SW_EXPECT_TRUE( field.harvest( 0, 0, produce, count ) == FarmActionResult::NotReady );
    (void)field.water( 0, 0 );
    field.advanceDay( "Spring", false );
    (void)field.water( 0, 0 );
    field.advanceDay( "Spring", false );
    SW_EXPECT_EQUAL( 1u, field.getReadyCount() );
    SW_EXPECT_TRUE( field.harvest( 0, 0, produce, count ) == FarmActionResult::Done );
    SW_EXPECT_TRUE( produce == hashed_string( "turnip" ) );
    SW_EXPECT_EQUAL( 1, count );
    SW_EXPECT_EQUAL( 0u, field.getCropCount() );
    SW_EXPECT_TRUE( field.harvest( 0, 0, produce, count ) == FarmActionResult::NoCrop );
}

/**
 * @brief [FarmingTest] 다시 자라는 작물은 거둔 뒤에도 남아 `regrow` 날 만에 다시 열린다 · 비는 갈아 둔 칸에 물을 준다
 */
SW_TEST_CASE( FarmingTest, RegrowingCropsStayAndRainWatersTilledSoil )
{
    CropCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kFarmingTestCropXml, "FarmingTest" ) );
    FarmField field;
    field.initialize( 2, 1, &catalog );
    prepareFarmTile( field, 0, 0, "tomato_seed" );
    (void)field.till( 1, 0 );

    // 비가 오면 다음 날 아침 물 준 상태로 시작한다 — 계속 비면 물을 안 줘도 자란다.
    field.advanceDay( "Spring", true );
    SW_EXPECT_TRUE( field.findTile( 1, 0 )->_bWatered == SW_TRUE );
    for ( int32 dayIndex = 0; dayIndex < 3; ++dayIndex )
        field.advanceDay( "Spring", true );
    hashed_string produce;
    int32         count = 0;
    SW_ASSERT_TRUE( field.harvest( 0, 0, produce, count ) == FarmActionResult::Done );
    SW_EXPECT_EQUAL( 2, count );
    SW_EXPECT_EQUAL( 1u, field.getCropCount() ); // 남았다

    field.advanceDay( "Spring", true );
    SW_EXPECT_TRUE( field.harvest( 0, 0, produce, count ) == FarmActionResult::NotReady );
    field.advanceDay( "Spring", true );
    SW_EXPECT_TRUE( field.harvest( 0, 0, produce, count ) == FarmActionResult::Done );
}

/**
 * @brief [FarmingTest] 계절이 바뀌면 그 계절에 안 자라는 작물은 시들고(치우면 아무것도 없다), 철 지난 씨는 심을 수 없다
 */
SW_TEST_CASE( FarmingTest, SeasonChangeWithersOutOfSeasonCrops )
{
    CropCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kFarmingTestCropXml, "FarmingTest" ) );
    FarmField field;
    field.initialize( 2, 1, &catalog );
    prepareFarmTile( field, 0, 0, "turnip_seed" ); // 봄만
    prepareFarmTile( field, 1, 0, "tomato_seed" ); // 봄 · 여름

    field.advanceDay( "Summer", false );
    SW_EXPECT_TRUE( field.findTile( 0, 0 )->_bWithered == SW_TRUE );
    SW_EXPECT_TRUE( field.findTile( 1, 0 )->_bWithered == SW_FALSE );
    hashed_string produce;
    int32         count = 7;
    SW_EXPECT_TRUE( field.harvest( 0, 0, produce, count ) == FarmActionResult::Done );
    SW_EXPECT_EQUAL( 0, count );
    SW_EXPECT_TRUE( produce.empty() );

    (void)field.water( 0, 0 );
    SW_EXPECT_TRUE( field.plant( 0, 0, "turnip_seed", "Summer" ) == FarmActionResult::OutOfSeason );
    SW_EXPECT_TRUE( field.plant( 0, 0, "rose_seed", "Summer" ) == FarmActionResult::UnknownSeed );
}

/**
 * @brief [FarmingTest] 출하함은 빌린 가방에서 꺼내 담고, 하루 끝 정산에서 카탈로그 값으로 팔아 빌린 지갑에 더한다
 */
SW_TEST_CASE( FarmingTest, ShippingBinSellsFromTheBorrowedBagAtTheEndOfTheDay )
{
    CropCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kFarmingTestCropXml, "FarmingTest" ) );
    ItemCatalog items;
    catalog.fillItemCatalog( items, 99 );
    SW_ASSERT_NOT_NULL( items.findItem( "turnip_seed" ) );
    Inventory bag;
    bag.initialize( &items, 8 );
    Wallet          wallet;
    FarmShippingBin bin;
    SW_EXPECT_EQUAL( 5, bag.addItem( "turnip", 5 ) );
    (void)bag.addItem( "turnip_seed", 1 );
    SW_EXPECT_TRUE( bin.shipItem( bag, "turnip", 4 ) );
    SW_EXPECT_FALSE( bin.shipItem( bag, "turnip", 2 ) );
    SW_EXPECT_TRUE( bin.shipItem( bag, "turnip_seed", 1 ) );
    SW_EXPECT_EQUAL( 5, bin.getShippedItemCount() );
    SW_EXPECT_EQUAL( 4 * 60 + 10, bin.settleShipping( catalog, wallet ) );
    SW_EXPECT_EQUAL( int64{ 250 }, wallet.getBalance( bin.getCurrency() ) );
    SW_EXPECT_EQUAL( 0, bin.getShippedItemCount() );
    SW_EXPECT_EQUAL( 1, bag.getItemCount( "turnip" ) );
}

/**
 * @brief [FarmingTest] 밭 · 출하함의 상태를 쓰고 새 객체에 읽으면 같은 농장이 이어진다 — 깨진 바이트는 거절하고 그대로 둔다
 * @details 핫 리로드 · 세이브가 디렉터의 시뮬레이션을 이 바이트로 옮긴다(`ComponentStateStore`). 달력 · 가방 · 돈은 공유 상태가 싣는다.
 *          읽은 쪽과 원본을 같은 하루만큼 더 돌려도 같아야 한다.
 */
SW_TEST_CASE( FarmingTest, StateRoundTripContinuesTheSameFarm )
{
    CropCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kFarmingTestCropXml, "FarmingTest" ) );
    ItemCatalog items;
    catalog.fillItemCatalog( items, 99 );
    Inventory bag;
    bag.initialize( &items, 8 );

    FarmField field;
    field.initialize( 4, 3, &catalog );
    prepareFarmTile( field, 1, 1, "tomato_seed" );
    (void)field.till( 2, 2 );
    field.advanceDay( "Spring", true );
    FarmShippingBin bin;
    (void)bag.addItem( "tomato", 2 );
    SW_EXPECT_TRUE( bin.shipItem( bag, "tomato", 1 ) );

    Archive written;
    field.writeState( written );
    bin.writeState( written );

    FarmField       readField;
    FarmShippingBin readBin;
    readField.initialize( 4, 3, &catalog );
    Archive reader( written.getData(), written.getSize() );
    SW_ASSERT_TRUE( readField.readState( reader ) );
    SW_ASSERT_TRUE( readBin.readState( reader ) );
    SW_EXPECT_EQUAL( uint64( 0 ), reader.getRemainingBytes() );

    SW_EXPECT_EQUAL( field.getCropCount(), readField.getCropCount() );
    SW_EXPECT_NEAR_EQUAL( field.computeGrowthRatio( 1, 1 ), readField.computeGrowthRatio( 1, 1 ), 1.0e-6f );
    SW_ASSERT_NOT_NULL( readField.findTile( 2, 2 ) );
    SW_EXPECT_TRUE( readField.findTile( 2, 2 )->_bTilled == SW_TRUE );
    SW_EXPECT_EQUAL( 1, readBin.getShippedItemCount() );

    // 같은 하루를 더 돌리면 같은 결과다 — 상태가 다 옮겨졌다
    (void)field.water( 1, 1 );
    (void)readField.water( 1, 1 );
    field.advanceDay( "Spring", false );
    readField.advanceDay( "Spring", false );
    SW_EXPECT_NEAR_EQUAL( field.computeGrowthRatio( 1, 1 ), readField.computeGrowthRatio( 1, 1 ), 1.0e-6f );
    Wallet settleWallet;
    Wallet readSettleWallet;
    SW_EXPECT_EQUAL( bin.settleShipping( catalog, settleWallet ), readBin.settleShipping( catalog, readSettleWallet ) );

    BLOCK( "크기가 다른 밭 · 잘린 바이트는 거절하고 그대로 둔다" )
    {
        FarmField smallField;
        smallField.initialize( 2, 2, &catalog );
        Archive fieldOnly;
        field.writeState( fieldOnly );
        Archive smallReader( fieldOnly.getData(), fieldOnly.getSize() );
        SW_EXPECT_FALSE( smallField.readState( smallReader ) );
        SW_EXPECT_EQUAL( uint32( 0 ), smallField.getCropCount() );

        FarmShippingBin truncated;
        (void)bag.addItem( "turnip", 1 );
        SW_ASSERT_TRUE( truncated.shipItem( bag, "turnip", 1 ) );
        Archive   cut( written.getData(), written.getSize() - 2 );
        FarmField skipField;
        skipField.initialize( 4, 3, &catalog );
        SW_EXPECT_TRUE( skipField.readState( cut ) );
        SW_EXPECT_FALSE( truncated.readState( cut ) );
        SW_EXPECT_EQUAL( 1, truncated.getShippedItemCount() );
    }
}

/**
 * @brief [FarmingTest] 밭 둘이 지갑 하나를 빌려 쓴다 — 두 출하함의 정산이 한 지갑에 더해진다(키트를 섞은 게임의 공유 지갑)
 */
SW_TEST_CASE( FarmingTest, TwoFarmsShareOneBorrowedWallet )
{
    CropCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kFarmingTestCropXml, "FarmingTest" ) );
    Inventory bag;
    bag.initialize( nullptr, 8 );
    Wallet          shared;
    FarmShippingBin north;
    FarmShippingBin south;
    (void)bag.addItem( "turnip", 1 );
    (void)bag.addItem( "turnip", 1 );
    (void)bag.addItem( "turnip", 1 );
    SW_EXPECT_TRUE( north.shipItem( bag, "turnip", 2 ) );
    SW_EXPECT_TRUE( south.shipItem( bag, "turnip", 1 ) );
    SW_EXPECT_EQUAL( 2 * 60, north.settleShipping( catalog, shared ) );
    SW_EXPECT_EQUAL( 60, south.settleShipping( catalog, shared ) );
    SW_EXPECT_EQUAL( int64{ 3 * 60 }, shared.getBalance( "Gold" ) );
}

/**
 * @brief [FarmingTest] 땅을 빌린 밭은 밭 전체를 한 번에 얻는다 — 그 자리에 남의 칸이 하나라도 있으면 아무것도 얻지 않고 거절, 얻은 밭은 남이 쓰지 못한다(막힘은 아님)
 */
SW_TEST_CASE( FarmingTest, FieldClaimsItsWholeLandOrNothing )
{
    LandRegistry land;
    land.initialize( 16, 16, 1.0f, float3{} );
    const uint16 other = land.registerOwner( "Other" );
    SW_ASSERT_TRUE( land.claimRect( other, 5, 5, 5, 5, true ) );

    FarmField field;
    field.initialize( 4, 4, nullptr );
    SW_EXPECT_FALSE( field.bindLand( &land, int2{ 3, 3 } ) ); // (5, 5) 가 남의 땅
    SW_EXPECT_EQUAL( LandRegistry::kNoOwner, land.getOwner( 3, 3 ) );
    SW_ASSERT_TRUE( field.bindLand( &land, int2{ 8, 8 } ) );
    SW_EXPECT_TRUE( land.getOwnerName( 8, 8 ) == hashed_string( "Farming" ) );
    SW_EXPECT_TRUE( land.getOwnerName( 11, 11 ) == hashed_string( "Farming" ) );
    SW_EXPECT_EQUAL( LandRegistry::kNoOwner, land.getOwner( 12, 12 ) );
    SW_EXPECT_FALSE( land.isBlockedFor( other, 9, 9 ) ); // 밭은 지나갈 수 있다
    SW_EXPECT_FALSE( land.claimRect( other, 11, 11, 12, 12, false ) );
}
