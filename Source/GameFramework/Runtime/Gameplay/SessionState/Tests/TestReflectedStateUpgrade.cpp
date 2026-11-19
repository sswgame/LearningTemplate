// ReflectedStateUpgradeTest — 손 본문 상태를 리플렉션 상태 칸으로 옮긴 형식의 옛 판 바이트가 판 올림 사슬로 올라 읽힌다(세이브 구간 · 어빌리티 본문, nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Gameplay/Ability/AbilitySaveState.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"
#include "GameFramework/Runtime/Gameplay/Progression/Reputation.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLog.h"
#include "GameFramework/Runtime/World/Environment/WeatherSystem.h"

using namespace sw;

namespace
{
    struct ReflectedStateUpgradeTestUtil
    {
        /** @brief 옛 판 구간 본문 @p oldBody 를 @p TState 의 지금 판으로 올려 @p outState 에 읽습니다. */
        template <typename TState>
        [[nodiscard]] static bool upgradeAndRead( const Archive& oldBody, uint32 fromVersion, TState& outState )
        {
            Archive body( oldBody.getData(), oldBody.getSize() );
            if ( StateIO::upgradeSectionBody( TState::kStateTag, fromVersion, TState::kStateVersion, body ) == false )
                return false;
            return outState.readState( body ) && body.getRemainingBytes() == 0;
        }
    };
} // namespace

/** @brief [ReflectedStateUpgradeTest] 지갑 · 평판 판 1(손 목록 본문)은 머리만 붙여 올린다 */
SW_TEST_CASE( ReflectedStateUpgradeTest, WalletAndReputationVersionOneRead )
{
    Archive walletBody;
    {
        StateIO       io( walletBody );
        uint32        count    = 1;
        hashed_string currency = hashed_string( "Gold" );
        int64         amount   = 42;
        io.count( 12, count );
        io.value( currency );
        io.value( amount );
    }
    Wallet wallet;
    SW_REQUIRE_TRUE( ReflectedStateUpgradeTestUtil::upgradeAndRead( walletBody, 1, wallet ) );
    SW_EXPECT_EQUAL( int64{ 42 }, wallet.getBalance( hashed_string( "Gold" ) ) );

    Archive reputationBody;
    {
        StateIO       io( reputationBody );
        uint32        count   = 1;
        hashed_string faction = hashed_string( "Guild" );
        int32         value   = -7;
        io.count( 8, count );
        io.value( faction );
        io.value( value );
    }
    ReputationState reputation;
    reputation.initialize( nullptr );
    SW_REQUIRE_TRUE( ReflectedStateUpgradeTestUtil::upgradeAndRead( reputationBody, 1, reputation ) );
    SW_EXPECT_EQUAL( -7, reputation.getValue( hashed_string( "Guild" ) ) );
}

/** @brief [ReflectedStateUpgradeTest] 퀘스트 일지 판 1 은 상태를 uint8 에서 열거 폭으로 다시 써 올린다 */
SW_TEST_CASE( ReflectedStateUpgradeTest, QuestLogVersionOneRead )
{
    Archive oldBody;
    {
        StateIO       io( oldBody );
        uint32        count     = 1;
        hashed_string questID   = hashed_string( "wolves" );
        hashed_string stageID   = hashed_string( "hunt" );
        vector<int32> listCount = { 3 };
        float32       stageTime = 1.5f;
        int32         completed = 2;
        uint8         status    = static_cast<uint8>( QuestStatus::Active );
        uint8         bAwaiting = SW_FALSE;
        io.count( 22, count );
        io.value( questID );
        io.value( stageID );
        io.list( listCount );
        io.value( stageTime );
        io.value( completed );
        io.value( status );
        io.value( bAwaiting );
    }
    QuestLog log;
    log.initialize( nullptr ); // 카탈로그 없이 — 읽은 진행을 그대로 둔다
    SW_REQUIRE_TRUE( ReflectedStateUpgradeTestUtil::upgradeAndRead( oldBody, 1, log ) );
    const QuestProgress* pProgress = log.findProgress( hashed_string( "wolves" ) );
    SW_REQUIRE_NOT_NULL( pProgress );
    SW_EXPECT_TRUE( pProgress->_status == QuestStatus::Active );
    SW_EXPECT_TRUE( pProgress->_stageID == hashed_string( "hunt" ) );
    SW_EXPECT_EQUAL( 2, pProgress->_completedCount );
    SW_EXPECT_EQUAL( size_t{ 1 }, pProgress->_listCount.size() );
}

/** @brief [ReflectedStateUpgradeTest] 날씨 판 1 은 시간 칸을 앞으로 옮겨 올린다 */
SW_TEST_CASE( ReflectedStateUpgradeTest, WeatherVersionOneRead )
{
    Archive oldBody;
    {
        StateIO       io( oldBody );
        hashed_string none;
        hashed_string previous;
        GameRandom    random;
        random.setSeed( 5 );
        float32 remaining  = 30.0f;
        float32 transition = 0.25f;
        io.value( none );
        io.value( previous );
        io.value( random );
        io.value( remaining );
        io.value( transition );
    }
    WeatherSystem weather;
    SW_REQUIRE_TRUE( ReflectedStateUpgradeTestUtil::upgradeAndRead( oldBody, 1, weather ) );
    SW_EXPECT_EQUAL( 30.0f, weather.getRemaining() );
}

/** @brief [ReflectedStateUpgradeTest] 어빌리티 본문 판 1 은 옛 칸으로 읽어 리플렉션 본문으로 다시 써 올린다(머리가 올린다) */
SW_TEST_CASE( ReflectedStateUpgradeTest, AbilitySaveStateVersionOneRead )
{
    Archive oldBytes;
    {
        StateIO::writeHeader( oldBytes, AbilitySaveState::kStateTag, 1 );
        StateIO       io( oldBytes );
        float32       time      = 4.0f;
        uint32        baseCount = 1;
        hashed_string attribute = hashed_string( "Health" );
        float32       baseValue = 80.0f;
        uint32        noEffect  = 0;
        uint32        tagCount  = 1;
        string        tag       = "State.Stunned";
        int32         tagStack  = 2;
        io.value( time );
        io.count( 8, baseCount );
        io.value( attribute );
        io.value( baseValue );
        io.count( 16, noEffect );
        io.count( 8, tagCount );
        io.value( tag );
        io.value( tagStack );
    }
    AbilitySaveState state;
    Archive          reading( oldBytes.getData(), oldBytes.getSize() );
    {
        StateIO io( reading );
        io.value( state );
        SW_REQUIRE_TRUE( io.isOk() );
    }
    SW_EXPECT_EQUAL( 4.0f, state._time );
    SW_REQUIRE_EQUAL( size_t{ 1 }, state._listBaseValue.size() );
    SW_EXPECT_TRUE( state._listBaseValue[0]._attribute == hashed_string( "Health" ) );
    SW_EXPECT_EQUAL( 80.0f, state._listBaseValue[0]._value );
    SW_REQUIRE_EQUAL( size_t{ 1 }, state._listLooseTag.size() );
    SW_EXPECT_EQUAL( 2, state._listLooseTag[0]._count );
}
