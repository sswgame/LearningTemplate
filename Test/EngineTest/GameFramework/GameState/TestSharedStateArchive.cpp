/**
 * @file TestSharedStateArchive.cpp
 * @brief 키트 여럿이 나눠 쓰는 기반 상태(플래그 · 지갑 · 시계 · 평판 · 퀘스트 일지)의 상태 바이트 왕복 시험입니다.
 */
#include "pch.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Inventory/Inventory.h"
#include "GameFramework/Base/Inventory/Shop.h"
#include "GameFramework/Base/Progression/Reputation.h"
#include "GameFramework/Base/Quest/QuestCatalog.h"
#include "GameFramework/Base/Quest/QuestLog.h"
#include "GameFramework/Base/World/GameFlags.h"
#include "GameFramework/Base/World/WorldClock.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr const utf8* kSharedStateQuestXml = R"(
<QuestCatalog>
  <Quest id="hunt">
    <Stage id="kill" next="done"><Objective kind="Kill" target="wolf" count="3"/></Stage>
    <Stage id="done" complete="true"/>
  </Quest>
</QuestCatalog>
)";

    constexpr const utf8* kSharedStateOtherQuestXml = R"(
<QuestCatalog>
  <Quest id="other"><Stage id="done" complete="true"/></Quest>
</QuestCatalog>
)";

    /** @brief 상태 하나의 바이트입니다. */
    template <typename TState>
    vector<uint8> captureStateBytes( const TState& state )
    {
        Archive archive;
        state.writeState( archive );
        vector<uint8> bytes;
        archive.writeData( bytes );
        return bytes;
    }

    /** @brief 바이트를 읽어 넣습니다 — 끝까지 다 읽었을 때만 true 입니다. */
    template <typename TState>
    bool restoreStateBytes( TState& inoutState, const vector<uint8>& bytes )
    {
        Archive archive( bytes.data(), bytes.size() );
        return inoutState.readState( archive ) && archive.getRemainingBytes() == 0;
    }

    WorldClockSettings makeSharedStateClockSettings( float32 secondsPerDay )
    {
        WorldClockSettings settings;
        settings._listSeason    = { "Spring", "Summer" };
        settings._secondsPerDay = secondsPerDay;
        settings._daysPerSeason = 2;
        return settings;
    }
} // namespace

/**
 * @brief [SharedStateArchiveTest] 플래그 — 넣은 순서와 상관없이 같은 바이트이고, 읽으면 값이 그대로다. 잘린 바이트는 거절하고 그대로 둔다
 */
SW_TEST_CASE( SharedStateArchiveTest, FlagsRoundTripInNameOrder )
{
    GameFlags first;
    first.setFlag( "door.b", 2 );
    first.setFlag( "door.a", 1 );
    GameFlags second;
    second.setFlag( "door.a", 1 );
    second.setFlag( "door.b", 2 );
    const vector<uint8> bytes = captureStateBytes( first );
    SW_EXPECT_TRUE( bytes == captureStateBytes( second ) );

    GameFlags restored;
    SW_ASSERT_TRUE( restoreStateBytes( restored, bytes ) );
    SW_EXPECT_EQUAL( 1, restored.getFlag( "door.a" ) );
    SW_EXPECT_EQUAL( 2, restored.getFlag( "door.b" ) );

    GameFlags     untouched;
    vector<uint8> cut = bytes;
    cut.pop_back();
    untouched.setFlag( "keep", 5 );
    SW_EXPECT_FALSE( restoreStateBytes( untouched, cut ) );
    SW_EXPECT_EQUAL( 5, untouched.getFlag( "keep" ) );
}

/**
 * @brief [SharedStateArchiveTest] 지갑 — 통화 여럿의 잔액(빚 포함)이 그대로 오고 알림은 오지 않는다. 빈 통화 이름은 거절한다
 */
SW_TEST_CASE( SharedStateArchiveTest, WalletRoundTripKeepsDebtAndRejectsEmptyCurrency )
{
    Wallet wallet;
    wallet.add( "Gold", 120 );
    wallet.add( "Credits", 5 );
    wallet.charge( "Upkeep", 30 );
    const vector<uint8> bytes = captureStateBytes( wallet );

    Wallet restored;
    SW_ASSERT_TRUE( restoreStateBytes( restored, bytes ) );
    SW_EXPECT_EQUAL( int64{ 120 }, restored.getBalance( "Gold" ) );
    SW_EXPECT_EQUAL( int64{ 5 }, restored.getBalance( "Credits" ) );
    SW_EXPECT_EQUAL( int64{ -30 }, restored.getBalance( "Upkeep" ) );
    vector<WalletEvent> listEvent;
    restored.drainEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.empty() );

    Archive unnamed;
    unnamed << uint32{ 1 };
    unnamed << string_view( "" );
    unnamed << int64{ 7 };
    vector<uint8> unnamedBytes;
    unnamed.writeData( unnamedBytes );
    SW_EXPECT_FALSE( restoreStateBytes( restored, unnamedBytes ) );
    SW_EXPECT_EQUAL( int64{ 120 }, restored.getBalance( "Gold" ) );
}

/**
 * @brief [SharedStateArchiveTest] 거절할 수 없는 지출 — 잔액이 빚이 되고, 빚이 있는 동안 쓰기는 거절된다
 */
SW_TEST_CASE( SharedStateArchiveTest, ChargeMakesDebtThatBlocksSpending )
{
    Wallet wallet;
    wallet.add( "Gold", 10 );
    wallet.charge( "Gold", 25 );
    SW_EXPECT_EQUAL( int64{ -15 }, wallet.getBalance( "Gold" ) );
    SW_EXPECT_FALSE( wallet.trySpend( "Gold", 1 ) );
    SW_EXPECT_FALSE( wallet.canAfford( "Gold", 0 ) );
    wallet.add( "Gold", 20 );
    SW_EXPECT_TRUE( wallet.trySpend( "Gold", 5 ) );
    SW_EXPECT_EQUAL( int64{ 0 }, wallet.getBalance( "Gold" ) );
}

/**
 * @brief [SharedStateArchiveTest] 시계 — 날 · 시각이 그대로 오고, 하루가 더 짧은 설정의 시계는 그 시각을 거절한다
 */
SW_TEST_CASE( SharedStateArchiveTest, ClockRoundTripKeepsTimeAndRejectsOutOfDay )
{
    WorldClock clock;
    clock.initialize( makeSharedStateClockSettings( 24.0f ) );
    clock.update( 30.0f ); // 6 시에서 30 시간 — 다음 날 12 시
    SW_EXPECT_EQUAL( 1, clock.getDay() );
    const vector<uint8> bytes = captureStateBytes( clock );

    WorldClock restored;
    restored.initialize( makeSharedStateClockSettings( 24.0f ) );
    SW_ASSERT_TRUE( restoreStateBytes( restored, bytes ) );
    SW_EXPECT_EQUAL( clock.getDay(), restored.getDay() );
    SW_EXPECT_EQUAL( clock.getHour(), restored.getHour() );
    vector<WorldClockEvent> listEvent;
    restored.drainEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.empty() );

    WorldClock shortDay;
    shortDay.initialize( makeSharedStateClockSettings( 10.0f ) ); // 하루 안의 초 12 가 하루(10 초) 밖이다
    SW_EXPECT_FALSE( restoreStateBytes( shortDay, bytes ) );
    SW_EXPECT_EQUAL( 0, shortDay.getDay() );
}

/**
 * @brief [SharedStateArchiveTest] 평판 — 세력마다 값이 그대로 온다
 */
SW_TEST_CASE( SharedStateArchiveTest, ReputationRoundTrip )
{
    ReputationState reputation;
    reputation.initialize( nullptr );
    (void)reputation.changeValue( "sprout", 30 );
    (void)reputation.changeValue( "restaurant", 7 );
    const vector<uint8> bytes = captureStateBytes( reputation );

    ReputationState restored;
    restored.initialize( nullptr );
    SW_ASSERT_TRUE( restoreStateBytes( restored, bytes ) );
    SW_EXPECT_EQUAL( 30, restored.getValue( "sprout" ) );
    SW_EXPECT_EQUAL( 7, restored.getValue( "restaurant" ) );
}

/**
 * @brief [SharedStateArchiveTest] 퀘스트 일지 — 단계 · 목표 개수가 그대로 오고, 카탈로그에 없는 퀘스트는 버린다(일지 전체를 막지 않는다)
 */
SW_TEST_CASE( SharedStateArchiveTest, QuestLogRoundTripDropsUnknownQuests )
{
    QuestCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kSharedStateQuestXml, "SharedStateArchiveTest" ) );
    QuestCatalog otherCatalog;
    SW_ASSERT_TRUE( otherCatalog.loadFromXmlText( kSharedStateOtherQuestXml, "SharedStateArchiveTest" ) );

    QuestLog log;
    log.initialize( &catalog );
    SW_ASSERT_TRUE( log.start( "hunt", 0 ) == QuestStartResult::Ok );
    SW_EXPECT_EQUAL( 1, log.notify( "Kill", "wolf", 2 ) );
    const vector<uint8> bytes = captureStateBytes( log );

    QuestLog restored;
    restored.initialize( &catalog );
    SW_ASSERT_TRUE( restoreStateBytes( restored, bytes ) );
    SW_EXPECT_TRUE( restored.getStatus( "hunt" ) == QuestStatus::Active );
    const QuestProgress* pProgress = restored.findProgress( "hunt" );
    SW_ASSERT_NOT_NULL( pProgress );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), pProgress->_listCount.size() );
    SW_EXPECT_EQUAL( 2, pProgress->_listCount[0] );
    SW_EXPECT_EQUAL( 1, restored.notify( "Kill", "wolf", 1 ) ); // 이어서 센다 — 셋째 늑대로 끝난다
    SW_EXPECT_TRUE( restored.getStatus( "hunt" ) == QuestStatus::Completed );

    QuestLog other;
    other.initialize( &otherCatalog );
    SW_ASSERT_TRUE( restoreStateBytes( other, bytes ) );
    SW_EXPECT_TRUE( other.getStatus( "hunt" ) == QuestStatus::NotStarted );
}

/**
 * @brief [SharedStateArchiveTest] 플레이어 가방 — 칸마다 아이템 · 개수 · 내구도 · 꾸미기가 그대로 오고, 칸 수가 다른 가방은 거절한다
 */
SW_TEST_CASE( SharedStateArchiveTest, InventoryRoundTripKeepsSlotsAndRejectsOtherSlotCount )
{
    Inventory bag;
    bag.initialize( nullptr, 4, 30.0f );
    SW_ASSERT_EQUAL( 2, bag.addItem( "turnip", 2 ) );
    ItemStack dyed;
    dyed._itemId     = "hat";
    dyed._count      = 1;
    dyed._durability = 0.5f;
    dyed._customization.setColor( "Dye", float4{ 1.0f, 0.0f, 0.0f, 1.0f } );
    dyed._listDetachedPart.push_back( "Feather" );
    SW_ASSERT_TRUE( bag.addStack( dyed ) );
    const vector<uint8> bytes = captureStateBytes( bag );

    Inventory restored;
    restored.initialize( nullptr, 4 );
    SW_ASSERT_TRUE( restoreStateBytes( restored, bytes ) );
    SW_EXPECT_EQUAL( 2, restored.getItemCount( "turnip" ) );
    SW_EXPECT_NEAR_EQUAL( 30.0f, restored.getMaxWeight(), 1e-6f );
    const int32 hatSlot = restored.findFirstSlot( "hat" );
    SW_ASSERT_TRUE( hatSlot >= 0 );
    SW_EXPECT_NEAR_EQUAL( 0.5f, restored.getSlot( hatSlot )._durability, 1e-6f );
    SW_EXPECT_TRUE( restored.getSlot( hatSlot )._customization.isEquivalent( dyed._customization ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), restored.getSlot( hatSlot )._listDetachedPart.size() );
    SW_EXPECT_TRUE( captureStateBytes( restored ) == bytes );

    Inventory smaller;
    smaller.initialize( nullptr, 3 );
    SW_EXPECT_FALSE( restoreStateBytes( smaller, bytes ) );
}
