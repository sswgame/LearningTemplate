#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/Kits/Genre/Simulation/Farming/CropCatalog.h"
#include "GameFramework/Kits/Genre/Simulation/Farming/FarmField.h"
#include "GameFramework/Kits/Genre/Simulation/Farming/FarmShippingBin.h"

// FarmingTest — 농사 키트: 밭 행동의 거절 이유, 물 준 날만 자람, 비 · 다시 자라는 작물, 계절이 지나 시듦, 상태 왕복,
// 출하함 정산(수확물 판매가 · 씨앗은 절반 · 모르는 아이템 0). 카탈로그는 글로 준다. 키트 GF_Farming(nogpu).

namespace
{
    constexpr const utf8* kCropXML = R"(<CropCatalog>
  <Crop id="turnip" name="Turnip" seed="turnip_seed" produce="turnip" days="2" seasons="Spring" seedPrice="20" sellPrice="60"/>
  <Crop id="tomato" name="Tomato" seed="tomato_seed" produce="tomato" days="3" regrow="2" seasons="Spring,Summer" seedPrice="30" sellPrice="40" harvest="2"/>
</CropCatalog>)";

    const sw::hashed_string kSpring{ "Spring" };
    const sw::hashed_string kSummer{ "Summer" };
    const sw::hashed_string kFall{ "Fall" };
    const sw::hashed_string kTurnip{ "turnip" };
    const sw::hashed_string kTurnipSeed{ "turnip_seed" };
    const sw::hashed_string kTomato{ "tomato" };
    const sw::hashed_string kTomatoSeed{ "tomato_seed" };
} // namespace

/**
 * @brief [FarmingTest] 밭 행동은 밭 밖 · 갈지 않음 · 이미 갊 · 모르는 씨앗 · 계절 밖 · 작물 있음 · 이미 물 줌 · 작물 없음 · 덜 자람을 이유로 거절한다
 */
SW_TEST_CASE( FarmingTest, FieldActionsReportWhyTheyFail )
{
    sw::CropCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kCropXML, "FarmingTest" ) );
    sw::FarmField field;
    field.initialize( 3, 2, &catalog );

    SW_EXPECT_TRUE( field.till( 3, 0 ) == sw::FarmActionResult::OutOfBounds );
    SW_EXPECT_TRUE( field.water( 0, 0 ) == sw::FarmActionResult::NotTilled );
    SW_EXPECT_TRUE( field.plant( 0, 0, kTurnipSeed, kSpring ) == sw::FarmActionResult::NotTilled );
    SW_EXPECT_TRUE( field.till( 0, 0 ) == sw::FarmActionResult::Done );
    SW_EXPECT_TRUE( field.till( 0, 0 ) == sw::FarmActionResult::AlreadyTilled );
    SW_EXPECT_TRUE( field.plant( 0, 0, sw::hashed_string( "rock" ), kSpring ) == sw::FarmActionResult::UnknownSeed );
    SW_EXPECT_TRUE( field.plant( 0, 0, kTomatoSeed, kFall ) == sw::FarmActionResult::OutOfSeason );
    SW_EXPECT_TRUE( field.plant( 0, 0, kTurnipSeed, kSpring ) == sw::FarmActionResult::Done );
    SW_EXPECT_TRUE( field.till( 0, 0 ) == sw::FarmActionResult::Occupied );
    SW_EXPECT_TRUE( field.plant( 0, 0, kTurnipSeed, kSpring ) == sw::FarmActionResult::Occupied );
    SW_EXPECT_TRUE( field.water( 0, 0 ) == sw::FarmActionResult::Done );
    SW_EXPECT_TRUE( field.water( 0, 0 ) == sw::FarmActionResult::AlreadyWatered );

    sw::hashed_string produce;
    int32             count = -1;
    SW_EXPECT_TRUE( field.harvest( 1, 0, produce, count ) == sw::FarmActionResult::NoCrop );
    SW_EXPECT_TRUE( field.harvest( 0, 0, produce, count ) == sw::FarmActionResult::NotReady );
    SW_EXPECT_EQUAL( 0, count );
    SW_EXPECT_EQUAL( 1u, field.getCropCount() );
}

/**
 * @brief [FarmingTest] 어제 물을 받은 작물만 하루 자라고, 자란 날이 `days` 에 닿으면 거둘 수 있다. 한 번 거두는 작물은 거두면 치워진다
 */
SW_TEST_CASE( FarmingTest, CropsGrowOnlyOnWateredDays )
{
    sw::CropCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kCropXML, "FarmingTest" ) );
    sw::FarmField field;
    field.initialize( 2, 2, &catalog );
    SW_REQUIRE_TRUE( field.till( 0, 0 ) == sw::FarmActionResult::Done );
    SW_REQUIRE_TRUE( field.plant( 0, 0, kTurnipSeed, kSpring ) == sw::FarmActionResult::Done );

    SW_REQUIRE_TRUE( field.water( 0, 0 ) == sw::FarmActionResult::Done );
    field.advanceDay( kSpring, false );
    SW_EXPECT_EQUAL( 1, field.findTile( 0, 0 )->_growth );
    SW_EXPECT_NEAR( 0.5f, field.computeGrowthRatio( 0, 0 ), 1.0e-6f );
    SW_EXPECT_TRUE( field.findTile( 0, 0 )->_bWatered == SW_FALSE ); // 물은 마른다

    field.advanceDay( kSpring, false ); // 물을 주지 않은 날은 자라지 않는다
    SW_EXPECT_EQUAL( 1, field.findTile( 0, 0 )->_growth );

    SW_REQUIRE_TRUE( field.water( 0, 0 ) == sw::FarmActionResult::Done );
    field.advanceDay( kSpring, false );
    SW_EXPECT_EQUAL( 1u, field.getReadyCount() );
    SW_EXPECT_NEAR( 1.0f, field.computeGrowthRatio( 0, 0 ), 1.0e-6f );

    sw::hashed_string produce;
    int32             count = 0;
    SW_REQUIRE_TRUE( field.harvest( 0, 0, produce, count ) == sw::FarmActionResult::Done );
    SW_EXPECT_EQUAL( kTurnip, produce );
    SW_EXPECT_EQUAL( 1, count );
    SW_EXPECT_EQUAL( 0u, field.getCropCount() );
    SW_EXPECT_TRUE( field.findTile( 0, 0 )->_bTilled == SW_TRUE ); // 거둬도 간 땅은 남는다
}

/**
 * @brief [FarmingTest] 비 오는 날은 갈아 둔 타일이 젖은 채 시작하고, 다시 자라는 작물은 거둔 뒤 남아 `regrow` 일 뒤 또 거둔다
 */
SW_TEST_CASE( FarmingTest, RainWatersAndRegrowingCropsStay )
{
    sw::CropCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kCropXML, "FarmingTest" ) );
    sw::FarmField field;
    field.initialize( 2, 1, &catalog );
    SW_REQUIRE_TRUE( field.till( 1, 0 ) == sw::FarmActionResult::Done );
    SW_REQUIRE_TRUE( field.plant( 1, 0, kTomatoSeed, kSpring ) == sw::FarmActionResult::Done );

    field.advanceDay( kSpring, true ); // 어제 물이 없어 자라지 않고, 비로 젖는다
    SW_EXPECT_EQUAL( 0, field.findTile( 1, 0 )->_growth );
    SW_EXPECT_TRUE( field.findTile( 1, 0 )->_bWatered == SW_TRUE );
    SW_EXPECT_TRUE( field.findTile( 0, 0 )->_bWatered == SW_FALSE ); // 갈지 않은 땅은 젖지 않는다
    for ( int32 day = 0; day < 3; ++day )
    {
        field.advanceDay( kSummer, true );
    }
    SW_REQUIRE_EQUAL( 1u, field.getReadyCount() );

    sw::hashed_string produce;
    int32             count = 0;
    SW_REQUIRE_TRUE( field.harvest( 1, 0, produce, count ) == sw::FarmActionResult::Done );
    SW_EXPECT_EQUAL( kTomato, produce );
    SW_EXPECT_EQUAL( 2, count );
    SW_EXPECT_EQUAL( 1u, field.getCropCount() );
    SW_EXPECT_EQUAL( 1, field.findTile( 1, 0 )->_growth ); // days 3 - regrow 2

    field.advanceDay( kSummer, true );
    SW_EXPECT_EQUAL( 0u, field.getReadyCount() );
    field.advanceDay( kSummer, true );
    SW_EXPECT_EQUAL( 1u, field.getReadyCount() );
}

/**
 * @brief [FarmingTest] 새 계절에 자라지 않는 작물은 다 자랐어도 시들고, 거두면 아무것도 없이 치워진다
 */
SW_TEST_CASE( FarmingTest, CropsWitherOutOfSeason )
{
    sw::CropCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kCropXML, "FarmingTest" ) );
    sw::FarmField field;
    field.initialize( 1, 1, &catalog );
    SW_REQUIRE_TRUE( field.till( 0, 0 ) == sw::FarmActionResult::Done );
    SW_REQUIRE_TRUE( field.plant( 0, 0, kTurnipSeed, kSpring ) == sw::FarmActionResult::Done );
    field.advanceDay( kSpring, true );
    field.advanceDay( kSpring, true );
    field.advanceDay( kSpring, true );
    SW_REQUIRE_EQUAL( 1u, field.getReadyCount() );

    field.advanceDay( kSummer, false );
    SW_EXPECT_TRUE( field.findTile( 0, 0 )->_bWithered == SW_TRUE );
    SW_EXPECT_EQUAL( 0u, field.getReadyCount() );

    sw::hashed_string produce;
    int32             count = -1;
    SW_REQUIRE_TRUE( field.harvest( 0, 0, produce, count ) == sw::FarmActionResult::Done );
    SW_EXPECT_TRUE( produce.empty() );
    SW_EXPECT_EQUAL( 0, count );
    SW_EXPECT_EQUAL( 0u, field.getCropCount() );
}

/**
 * @brief [FarmingTest] 밭 상태는 작물 · 자람 · 상태 깃발을 그대로 되살리고, 크기가 다른 밭은 읽기를 거절하고 그대로다
 */
SW_TEST_CASE( FarmingTest, FieldStateRoundTrips )
{
    sw::CropCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kCropXML, "FarmingTest" ) );
    sw::FarmField original;
    original.initialize( 2, 2, &catalog );
    SW_REQUIRE_TRUE( original.till( 1, 1 ) == sw::FarmActionResult::Done );
    SW_REQUIRE_TRUE( original.plant( 1, 1, kTomatoSeed, kSpring ) == sw::FarmActionResult::Done );
    SW_REQUIRE_TRUE( original.water( 1, 1 ) == sw::FarmActionResult::Done );
    original.advanceDay( kSpring, true );

    sw::Archive written;
    original.writeState( written );
    sw::FarmField restored;
    restored.initialize( 2, 2, &catalog );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    const sw::FarmTile* pTile = restored.findTile( 1, 1 );
    SW_REQUIRE_TRUE( pTile != nullptr );
    SW_EXPECT_EQUAL( kTomato, pTile->_cropID );
    SW_EXPECT_EQUAL( 1, pTile->_growth );
    SW_EXPECT_TRUE( pTile->_bTilled == SW_TRUE );
    SW_EXPECT_TRUE( pTile->_bWatered == SW_TRUE );
    SW_EXPECT_TRUE( restored.findTile( 0, 0 )->_bTilled == SW_FALSE );

    sw::FarmField wider;
    wider.initialize( 3, 2, &catalog );
    sw::Archive again( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( wider.readState( again ) );
    SW_EXPECT_EQUAL( 0u, wider.getCropCount() );
}

/**
 * @brief [FarmingTest] 출하함은 가방에 있는 만큼만 받고, 하루 끝에 수확물은 판매가 · 씨앗은 씨앗값의 절반으로 팔아 지갑에 더한 뒤 빈다
 */
SW_TEST_CASE( FarmingTest, ShippingBinSellsAtCatalogPrices )
{
    sw::CropCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kCropXML, "FarmingTest" ) );
    sw::ItemCatalog items;
    catalog.fillItemCatalog( items, 99 );
    sw::Inventory bag;
    SW_REQUIRE_TRUE( bag.initialize( &items, 4 ) );
    SW_REQUIRE_EQUAL( 3, bag.addItem( kTurnip, 3 ) );
    SW_REQUIRE_EQUAL( 2, bag.addItem( kTurnipSeed, 2 ) );

    sw::FarmShippingBin bin;
    SW_EXPECT_TRUE( bin.shipItem( bag, kTurnip, 3 ) );
    SW_EXPECT_FALSE( bin.shipItem( bag, kTurnip, 1 ) ); // 가방에 남은 것이 없다
    SW_EXPECT_FALSE( bin.shipItem( bag, kTurnipSeed, 0 ) );
    SW_EXPECT_TRUE( bin.shipItem( bag, kTurnipSeed, 2 ) );
    SW_EXPECT_EQUAL( 5, bin.getShippedItemCount() );
    SW_EXPECT_EQUAL( 0, bag.getItemCount( kTurnip ) );

    sw::Wallet wallet;
    SW_EXPECT_EQUAL( 3 * 60 + 2 * 10, bin.settleShipping( catalog, wallet ) );
    SW_EXPECT_EQUAL( int64( 200 ), wallet.getBalance( sw::Wallet::getDefaultCurrency() ) );
    SW_EXPECT_EQUAL( 0, bin.getShippedItemCount() );
    SW_EXPECT_EQUAL( 0, bin.settleShipping( catalog, wallet ) );
    SW_EXPECT_EQUAL( 0, catalog.findSellPrice( sw::hashed_string( "rock" ) ) );
}
