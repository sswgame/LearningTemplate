#include "pch.h"

#include "GameFramework/Kits/Simulation/Farming/CropCatalog.h"
#include "GameFramework/Kits/Simulation/Farming/FarmCalendar.h"
#include "GameFramework/Kits/Simulation/Farming/FarmField.h"
#include "GameFramework/Kits/Simulation/Farming/FarmInventory.h"

#include "TestFramework/TestFramework.h"

// 농장 키트(하베스트 문 장르) — 달력의 하루 · 계절 · 해, 물 준 날만 자라는 작물, 다시 자라는 작물, 계절이 바뀌면 시듦, 비, 출하 정산.

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
        (void)field.plant( x, y, seed, FarmSeason::Spring );
    }
} // namespace

/**
 * @brief [FarmingTest] 하루는 6:00 에 시작해 26:00 에 끝나고, 28 일이 지나면 계절이, 네 계절이 지나면 해가 넘어간다
 */
SW_TEST_CASE( FarmingTest, CalendarRollsDaysSeasonsAndYears )
{
    FarmCalendar calendar;
    SW_EXPECT_EQUAL( 6, calendar.getHour() );
    SW_EXPECT_FALSE( calendar.advanceMinutes( 60.0f * 19.0f ) ); // 25:00
    SW_EXPECT_TRUE( calendar.advanceMinutes( 120.0f ) );         // 26:00 에서 멈춘다
    SW_EXPECT_EQUAL( 26, calendar.getHour() );

    for ( int32 dayIndex = 1; dayIndex < FarmCalendar::kDaysPerSeason; ++dayIndex )
        SW_EXPECT_FALSE( calendar.startNextDay() );
    SW_EXPECT_EQUAL( 28, calendar.getDay() );
    SW_EXPECT_TRUE( calendar.startNextDay() );
    SW_EXPECT_TRUE( calendar.getSeason() == FarmSeason::Summer );
    SW_EXPECT_EQUAL( 1, calendar.getDay() );
    SW_EXPECT_EQUAL( 6, calendar.getHour() );
    SW_EXPECT_EQUAL( 28, calendar.getElapsedDays() );

    calendar.setDate( 1, FarmSeason::Winter, 28 );
    SW_EXPECT_TRUE( calendar.startNextDay() );
    SW_EXPECT_EQUAL( 2, calendar.getYear() );
    SW_EXPECT_TRUE( calendar.getSeason() == FarmSeason::Spring );
}

/**
 * @brief [FarmingTest] 작물은 물 받은 날만 자라고 정한 날 수만큼 자라야 거둔다 — 갈지 않은 땅에는 심을 수 없다
 */
SW_TEST_CASE( FarmingTest, CropsGrowOnlyOnWateredDays )
{
    CropCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kFarmingTestCropXml, "FarmingTest" ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), catalog.getCrops().size() ); // 계절이 없는 ghost 는 빠졌다

    FarmField field;
    field.initialize( 4, 4, &catalog );
    SW_EXPECT_TRUE( field.plant( 0, 0, "turnip_seed", FarmSeason::Spring ) == FarmActionResult::NotTilled );
    SW_EXPECT_TRUE( field.till( 9, 9 ) == FarmActionResult::OutOfBounds );
    prepareFarmTile( field, 0, 0, "turnip_seed" );
    SW_EXPECT_TRUE( field.plant( 0, 0, "turnip_seed", FarmSeason::Spring ) == FarmActionResult::Occupied );
    SW_EXPECT_TRUE( field.water( 0, 0 ) == FarmActionResult::AlreadyWatered );

    hashed_string produce;
    int32         count = 0;
    field.advanceDay( FarmSeason::Spring, false ); // 자람 1
    field.advanceDay( FarmSeason::Spring, false ); // 물을 안 줬다 — 그대로
    SW_EXPECT_NEAR_EQUAL( 1.0f / 3.0f, field.computeGrowthRatio( 0, 0 ), 1.0e-4f );
    SW_EXPECT_TRUE( field.harvest( 0, 0, produce, count ) == FarmActionResult::NotReady );
    (void)field.water( 0, 0 );
    field.advanceDay( FarmSeason::Spring, false );
    (void)field.water( 0, 0 );
    field.advanceDay( FarmSeason::Spring, false );
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
    field.advanceDay( FarmSeason::Spring, true );
    SW_EXPECT_TRUE( field.findTile( 1, 0 )->_bWatered == SW_TRUE );
    for ( int32 dayIndex = 0; dayIndex < 3; ++dayIndex )
        field.advanceDay( FarmSeason::Spring, true );
    hashed_string produce;
    int32         count = 0;
    SW_ASSERT_TRUE( field.harvest( 0, 0, produce, count ) == FarmActionResult::Done );
    SW_EXPECT_EQUAL( 2, count );
    SW_EXPECT_EQUAL( 1u, field.getCropCount() ); // 남았다

    field.advanceDay( FarmSeason::Spring, true );
    SW_EXPECT_TRUE( field.harvest( 0, 0, produce, count ) == FarmActionResult::NotReady );
    field.advanceDay( FarmSeason::Spring, true );
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

    field.advanceDay( FarmSeason::Summer, false );
    SW_EXPECT_TRUE( field.findTile( 0, 0 )->_bWithered == SW_TRUE );
    SW_EXPECT_TRUE( field.findTile( 1, 0 )->_bWithered == SW_FALSE );
    hashed_string produce;
    int32         count = 7;
    SW_EXPECT_TRUE( field.harvest( 0, 0, produce, count ) == FarmActionResult::Done );
    SW_EXPECT_EQUAL( 0, count );
    SW_EXPECT_TRUE( produce.empty() );

    (void)field.water( 0, 0 );
    SW_EXPECT_TRUE( field.plant( 0, 0, "turnip_seed", FarmSeason::Summer ) == FarmActionResult::OutOfSeason );
    SW_EXPECT_TRUE( field.plant( 0, 0, "rose_seed", FarmSeason::Summer ) == FarmActionResult::UnknownSeed );
}

/**
 * @brief [FarmingTest] 가게에서 씨를 사면 돈이 줄고, 출하함에 넣은 것은 하루 끝 정산에서 카탈로그 값으로 팔린다
 */
SW_TEST_CASE( FarmingTest, ShippingSellsAtTheEndOfTheDay )
{
    CropCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kFarmingTestCropXml, "FarmingTest" ) );
    FarmInventory inventory;
    inventory.addGold( 100 );
    SW_EXPECT_TRUE( inventory.buyItem( "turnip_seed", 3, 20 ) );
    SW_EXPECT_FALSE( inventory.buyItem( "tomato_seed", 2, 40 ) ); // 40 남았는데 80
    SW_EXPECT_EQUAL( 40, inventory.getGold() );
    SW_EXPECT_EQUAL( 3, inventory.getItemCount( "turnip_seed" ) );

    inventory.addItem( "turnip", 5 );
    SW_EXPECT_TRUE( inventory.shipItem( "turnip", 4 ) );
    SW_EXPECT_FALSE( inventory.shipItem( "turnip", 2 ) );
    SW_EXPECT_TRUE( inventory.shipItem( "turnip_seed", 1 ) );
    SW_EXPECT_EQUAL( 5, inventory.getShippedItemCount() );
    SW_EXPECT_EQUAL( 4 * 60 + 10, inventory.settleShipping( catalog ) );
    SW_EXPECT_EQUAL( 40 + 250, inventory.getGold() );
    SW_EXPECT_EQUAL( 0, inventory.getShippedItemCount() );
    SW_EXPECT_EQUAL( 1, inventory.getItemCount( "turnip" ) );
}
