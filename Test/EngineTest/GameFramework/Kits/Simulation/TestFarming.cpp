#include "pch.h"

#include "Engine/Serialization/Format/Archive.h"

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

/**
 * @brief [FarmingTest] 달력 · 밭 · 인벤토리의 상태를 쓰고 새 객체에 읽으면 같은 농장이 이어진다 — 깨진 바이트는 거절하고 그대로 둔다
 * @details 핫 리로드 · 세이브가 디렉터의 시뮬레이션을 이 바이트로 옮긴다(`ComponentStateStore`). 읽은 쪽과 원본을 같은 하루만큼 더 돌려도 같아야 한다.
 */
SW_TEST_CASE( FarmingTest, StateRoundTripContinuesTheSameFarm )
{
    CropCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kFarmingTestCropXml, "FarmingTest" ) );

    FarmCalendar calendar;
    calendar.setDate( 2, FarmSeason::Summer, 5 );
    (void)calendar.advanceMinutes( 125.0f );
    FarmField field;
    field.initialize( 4, 3, &catalog );
    prepareFarmTile( field, 1, 1, "tomato_seed" );
    (void)field.till( 2, 2 );
    field.advanceDay( FarmSeason::Spring, true );
    FarmInventory inventory;
    inventory.addGold( 321 );
    inventory.addItem( "turnip_seed", 7 );
    inventory.addItem( "tomato", 2 );
    SW_EXPECT_TRUE( inventory.shipItem( "tomato", 1 ) );

    Archive written;
    calendar.writeState( written );
    field.writeState( written );
    inventory.writeState( written );

    FarmCalendar  readCalendar;
    FarmField     readField;
    FarmInventory readInventory;
    readField.initialize( 4, 3, &catalog );
    Archive reader( written.getData(), written.getSize() );
    SW_ASSERT_TRUE( readCalendar.readState( reader ) );
    SW_ASSERT_TRUE( readField.readState( reader ) );
    SW_ASSERT_TRUE( readInventory.readState( reader ) );
    SW_EXPECT_EQUAL( uint64( 0 ), reader.getRemainingBytes() );

    SW_EXPECT_EQUAL( 2, readCalendar.getYear() );
    SW_EXPECT_TRUE( readCalendar.getSeason() == FarmSeason::Summer );
    SW_EXPECT_EQUAL( 5, readCalendar.getDay() );
    SW_EXPECT_EQUAL( calendar.getHour(), readCalendar.getHour() );
    SW_EXPECT_EQUAL( calendar.getMinute(), readCalendar.getMinute() );
    SW_EXPECT_EQUAL( field.getCropCount(), readField.getCropCount() );
    SW_EXPECT_NEAR_EQUAL( field.computeGrowthRatio( 1, 1 ), readField.computeGrowthRatio( 1, 1 ), 1.0e-6f );
    SW_ASSERT_NOT_NULL( readField.findTile( 2, 2 ) );
    SW_EXPECT_TRUE( readField.findTile( 2, 2 )->_bTilled == SW_TRUE );
    SW_EXPECT_EQUAL( 321, readInventory.getGold() );
    SW_EXPECT_EQUAL( 7, readInventory.getItemCount( "turnip_seed" ) );
    SW_EXPECT_EQUAL( 1, readInventory.getShippedItemCount() );

    // 같은 하루를 더 돌리면 같은 결과다 — 상태가 다 옮겨졌다
    (void)field.water( 1, 1 );
    (void)readField.water( 1, 1 );
    field.advanceDay( FarmSeason::Spring, false );
    readField.advanceDay( FarmSeason::Spring, false );
    SW_EXPECT_NEAR_EQUAL( field.computeGrowthRatio( 1, 1 ), readField.computeGrowthRatio( 1, 1 ), 1.0e-6f );
    SW_EXPECT_EQUAL( inventory.settleShipping( catalog ), readInventory.settleShipping( catalog ) );

    BLOCK( "크기가 다른 밭 · 잘린 바이트는 거절하고 그대로 둔다" )
    {
        FarmField smallField;
        smallField.initialize( 2, 2, &catalog );
        Archive fieldOnly;
        field.writeState( fieldOnly );
        Archive smallReader( fieldOnly.getData(), fieldOnly.getSize() );
        SW_EXPECT_FALSE( smallField.readState( smallReader ) );
        SW_EXPECT_EQUAL( uint32( 0 ), smallField.getCropCount() );

        FarmInventory truncated;
        truncated.addGold( 5 );
        Archive      cut( written.getData(), written.getSize() - 2 );
        FarmCalendar skipCalendar;
        FarmField    skipField;
        skipField.initialize( 4, 3, &catalog );
        SW_EXPECT_TRUE( skipCalendar.readState( cut ) );
        SW_EXPECT_TRUE( skipField.readState( cut ) );
        SW_EXPECT_FALSE( truncated.readState( cut ) );
        SW_EXPECT_EQUAL( 5, truncated.getGold() );
    }
}
