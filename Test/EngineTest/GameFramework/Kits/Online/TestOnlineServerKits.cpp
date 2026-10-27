#include "pch.h"

#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/Time/Clock.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/Base/Online/Identity/AccountNameIndex.h"
#include "GameFramework/Base/Online/Identity/AccountPresence.h"
#include "GameFramework/Base/Online/Identity/AccountSessionControl.h"
#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Base/Online/Server/OnlineServerRuntime.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardCatalog.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Rule/MatchModeCatalog.h"

#include "TestFramework/TestFramework.h"

// 온라인 키트의 서버 조립 — 실제 키트를 런타임에 붙여 기동 · 종료가 돌고 인터페이스가 이어지는지, 모르는 옵션 · 빠진 의존을 기동에서 막는지,
// 순위표 · 매칭 정의 데이터를 읽는지 본다(요청 흐름 끝단은 OnlineLoadBotTest 가 같은 조립으로 돈다).

using namespace sw;

namespace
{
    struct OnlineServerKitsTestInternal
    {
        static constexpr int64 kStartMs = 1767225600000; ///< 2026-01-01 00:00:00 UTC

        static OnlineServerKitEntry& addKit( OnlineServerRuntimeConfig& inoutSettings, const utf8* pName )
        {
            OnlineServerKitEntry& entry = inoutSettings._config._listKit.emplace_back();
            entry._name                 = pName;
            return entry;
        }

        static void addOption( OnlineServerKitEntry& inoutEntry, const utf8* pKey, const utf8* pValue )
        {
            OnlineServerOption& option = inoutEntry._listOption.emplace_back();
            option._key                = pKey;
            option._value              = pValue;
        }

        /** @brief 런타임이 빌리는 단조 시계입니다. 상태가 없는 운영 시계라 테스트끼리 나눠도 된다. */
        static inline const SystemMonotonicClock kClock{};

        static OnlineServerRuntimeConfig makeSettings( IStreamTransport* pTransport, uint16 port )
        {
            OnlineServerRuntimeConfig settings;
            settings._bTLS                  = SW_FALSE;
            settings._pClock                = &kClock;
            settings._pTransport            = pTransport;
            settings._listenAddress         = NetAddress::makeLoopback( port );
            settings._config._ioThreadCount = 0;
            return settings;
        }
    };
} // namespace

SW_TEST_CASE( OnlineServerKitsTest, AccountMailboxTradeAndAdminAssembleAndShutDown )
{
#if defined( SW_SHIPPING )
    SW_TEST_REQUIRES_FEATURE( DevelopmentBuild ); // the shipping account kit refuses development options and needs a real ticket key secret
#endif
    using Internal = OnlineServerKitsTestInternal;
    LoopbackStreamNetwork        network;
    unique_ptr<IStreamTransport> transport      = network.createTransport();
    OnlineServerRuntimeConfig    settings       = Internal::makeSettings( transport.get(), 7461 );
    unique_ptr<IStreamTransport> adminTransport = network.createTransport(); // GM 키트는 GM 리슨이 있어야 오른다
    settings._bAdminListener                    = SW_TRUE;
    settings._pAdminTransport                   = adminTransport.get();
    settings._adminListenAddress                = NetAddress::makeLoopback( 7561 );
    Internal::addOption( Internal::addKit( settings, "account" ), "lightPasswordHash", "1" );
    (void)Internal::addKit( settings, "mailbox" );
    Internal::addOption( Internal::addKit( settings, "trade" ), "tradable", "cur.gold" );
    Internal::addOption( Internal::addKit( settings, "admin" ), "seedAdmin", "1:super" );
    OnlineServerRuntime runtime;
    string              error;
    {
        SW_TEST_DEFENSIVE_SCOPE( "the account kit without a ticket key variable falls back to the Dev key with a warning" ); // 거절 · 실패 로그가 이 구간이 보려는 동작이다
        SW_ASSERT_TRUE_MSG( runtime.initialize( settings, UnixMilliseconds{ Internal::kStartMs }, error ), error.c_str() );
    }
    const OnlineServerKitContext& context = runtime.getContext();
    SW_EXPECT_TRUE( context._kits.find<IAccountDirectory>() != nullptr ); // 계정 키트가 내놓았다
    const bool bAccountExposed = context._kits.find<IAccountPresence>() != nullptr && context._kits.find<IAccountSessionControl>() != nullptr && context._kits.find<const IAccountNameIndex>() != nullptr;
    SW_EXPECT_TRUE( bAccountExposed );
    SW_EXPECT_TRUE( context._kits.find<const ILedgerPolicy>() == nullptr ); // 경제 키트를 올리지 않았다
    vector<string> listKitName;
    runtime.collectKitNames( listKitName );
    SW_ASSERT_EQUAL( listKitName.size(), size_t( 4 ) );
    SW_EXPECT_TRUE( listKitName[0] == "account" ); // 신원을 내놓는 키트가 먼저
    for ( int32 tickIndex = 0; tickIndex < 5; ++tickIndex )
    {
        runtime.update( UnixMilliseconds{ Internal::kStartMs + tickIndex * 100 } );
    }
    runtime.shutdown();
}

SW_TEST_CASE( OnlineServerKitsTest, AssemblyDoesNotLeanOnTheNameOrderOfIndependentKits )
{
#if defined( SW_SHIPPING )
    SW_TEST_REQUIRES_FEATURE( DevelopmentBuild ); // the shipping account kit refuses development options and needs a real ticket key secret
#endif
    // 의존이 없는 키트끼리는 이름 순으로 서는데, 그 순서에 기댄 조립(적지 않은 의존)은 이름 역순으로 세우면 드러난다. 두 순서 모두 서고 내려가야 한다.
    using Internal = OnlineServerKitsTestInternal;
    for ( const uint8 bReverse : { SW_FALSE, SW_TRUE } )
    {
        LoopbackStreamNetwork        network;
        unique_ptr<IStreamTransport> transport      = network.createTransport();
        OnlineServerRuntimeConfig    settings       = Internal::makeSettings( transport.get(), 7466 );
        settings._bReverseIndependentKitOrder       = bReverse;
        unique_ptr<IStreamTransport> adminTransport = network.createTransport(); // GM 키트는 GM 리슨이 있어야 오른다
        settings._bAdminListener                    = SW_TRUE;
        settings._pAdminTransport                   = adminTransport.get();
        settings._adminListenAddress                = NetAddress::makeLoopback( 7566 );
        Internal::addOption( Internal::addKit( settings, "account" ), "lightPasswordHash", "1" );
        (void)Internal::addKit( settings, "social" );
        Internal::addOption( Internal::addKit( settings, "chat" ), "bannedWords", "none" );
        (void)Internal::addKit( settings, "directory" );
        (void)Internal::addKit( settings, "liveops" );
        (void)Internal::addKit( settings, "mailbox" );
        Internal::addOption( Internal::addKit( settings, "trade" ), "tradable", "cur.gold" );
        (void)Internal::addKit( settings, "admin" );
        OnlineServerRuntime runtime;
        string              error;
        {
            SW_TEST_DEFENSIVE_SCOPE( "the account kit without a ticket key variable falls back to the Dev key with a warning" ); // 거절 · 실패 로그가 이 구간이 보려는 동작이다
            SW_ASSERT_TRUE_MSG( runtime.initialize( settings, UnixMilliseconds{ Internal::kStartMs }, error ), error.c_str() );
        }
        vector<string> listKitName;
        runtime.collectKitNames( listKitName );
        SW_ASSERT_EQUAL( listKitName.size(), size_t( 8 ) );
        // 이름 순이면 GM 키트가 앞쪽, 역순이면 뒤쪽이다 — 두 순서가 실제로 달랐는지 본다.
        const bool bAdminBeforeTrade = std::find( listKitName.begin(), listKitName.end(), "admin" ) < std::find( listKitName.begin(), listKitName.end(), "trade" );
        SW_EXPECT_TRUE( bAdminBeforeTrade == ( bReverse == SW_FALSE ) );
        for ( int32 tickIndex = 0; tickIndex < 3; ++tickIndex )
        {
            runtime.update( UnixMilliseconds{ Internal::kStartMs + tickIndex * 100 } );
        }
        runtime.shutdown();
    }
}

SW_TEST_CASE( OnlineServerKitsTest, StartupStopsOnUnknownOptionsAndMissingDependencies )
{
    using Internal = OnlineServerKitsTestInternal;
    {
        LoopbackStreamNetwork        network;
        unique_ptr<IStreamTransport> transport = network.createTransport();
        OnlineServerRuntimeConfig    settings  = Internal::makeSettings( transport.get(), 7462 );
        Internal::addOption( Internal::addKit( settings, "account" ), "ticketKey", "abc" ); // 철자가 틀린 옵션
        OnlineServerRuntime runtime;
        string              error;
        SW_EXPECT_FALSE( runtime.initialize( settings, UnixMilliseconds{ Internal::kStartMs }, error ) );
        SW_EXPECT_TRUE( error.find( "ticketKey" ) != string::npos );
    }
    {
        LoopbackStreamNetwork        network;
        unique_ptr<IStreamTransport> transport = network.createTransport();
        OnlineServerRuntimeConfig    settings  = Internal::makeSettings( transport.get(), 7463 );
        Internal::addOption( Internal::addKit( settings, "chat" ), "bannedWords", "none" ); // 계정 키트 없이 채팅
        OnlineServerRuntime runtime;
        string              error;
        SW_EXPECT_FALSE( runtime.initialize( settings, UnixMilliseconds{ Internal::kStartMs }, error ) );
        SW_EXPECT_TRUE( error.find( "needs kit 'account'" ) != string::npos );
    }
    {
        LoopbackStreamNetwork        network;
        unique_ptr<IStreamTransport> transport = network.createTransport();
        OnlineServerRuntimeConfig    settings  = Internal::makeSettings( transport.get(), 7464 );
        (void)Internal::addKit( settings, "economy" ); // 카탈로그 없이 경제
        OnlineServerRuntime runtime;
        string              error;
        SW_EXPECT_FALSE( runtime.initialize( settings, UnixMilliseconds{ Internal::kStartMs }, error ) );
        SW_EXPECT_TRUE( error.find( "currencyCatalog" ) != string::npos );
    }
    {
        LoopbackStreamNetwork        network;
        unique_ptr<IStreamTransport> transport = network.createTransport();
        OnlineServerRuntimeConfig    settings  = Internal::makeSettings( transport.get(), 7465 );
        OnlineServerRuntime          runtime;
        string                       error;
        SW_EXPECT_FALSE( runtime.initialize( settings, UnixMilliseconds{ Internal::kStartMs }, error ) ); // 키트를 하나도 적지 않았다
        SW_EXPECT_TRUE( error.find( "_listKit" ) != string::npos );
    }
}

SW_TEST_CASE( OnlineServerKitsTest, LeaderboardAndMatchModeDataAreRead )
{
    LeaderboardCatalog boards;
    SW_ASSERT_TRUE( boards.loadFromXMLText( R"(<LeaderboardCatalog>
            <Board id="kills" order="Descending" update="Sum" reset="Weekly" resetMinute="60" resetDay="2" sourceStat="kills" clientSubmit="true"/>
            <Board id="race" order="Ascending">
                <Season id="1" startMs="1767225600000" endMs="1769904000000">
                    <Reward from="1" to="10" asset="cur.gem" amount="100" mailTitle="mail.season_reward"/>
                </Season>
            </Board>
            <Achievement id="first_blood" stat="kills" threshold="1" rewardAsset="cur.gold" rewardAmount="100" mailTitle="mail.achievement"/>
        </LeaderboardCatalog>)" ) );
    SW_ASSERT_EQUAL( boards.getBoards().size(), size_t( 2 ) );
    const LeaderboardDefinition& kills = boards.getBoards()[0];
    SW_EXPECT_TRUE( kills._update == LeaderboardUpdate::Sum && kills._reset == LeaderboardReset::Weekly );
    SW_EXPECT_EQUAL( kills._resetMinuteOfDay, 60 );
    SW_EXPECT_TRUE( kills._bClientSubmit );
    const LeaderboardDefinition& race = boards.getBoards()[1];
    SW_EXPECT_TRUE( race._order == LeaderboardOrder::Ascending );
    SW_ASSERT_EQUAL( race._listSeason.size(), size_t( 1 ) );
    SW_ASSERT_EQUAL( race._listSeason[0]._listRewardTier.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( race._listSeason[0]._listRewardTier[0]._amount, int64( 100 ) );
    SW_ASSERT_EQUAL( boards.getAchievements().size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( boards.getAchievements()[0]._rewardAmount, int64( 100 ) );

    MatchModeCatalog modes;
    SW_ASSERT_TRUE( modes.loadFromXMLText( R"(<MatchModeCatalog><Mode id="duo" teamCount="2" teamSize="2" maxWaitMs="60000"/></MatchModeCatalog>)" ) );
    SW_ASSERT_EQUAL( modes.getModes().size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( modes.getModes()[0]._maxWaitMs, int64( 60000 ) );
    MatchModeDefinition compact;
    SW_EXPECT_TRUE( MatchModeCatalog::parseCompactMode( "squad:2x4", compact ) );
    SW_EXPECT_TRUE( compact._modeID == "squad" && compact._teamCount == 2 && compact._teamSize == 4 );
    SW_EXPECT_FALSE( MatchModeCatalog::parseCompactMode( "squad:2", compact ) );
    SW_EXPECT_FALSE( MatchModeCatalog::parseCompactMode( ":2x2", compact ) );
    // 팀 수 · 인원의 범위는 `MatchModeDefinition` 의 `PROPERTY` 가 기준이다 — 0 과 상한 + 1 은 읽을 때 거절한다.
    SW_EXPECT_FALSE( MatchModeCatalog::parseCompactMode( "squad:0x4", compact ) );
    SW_EXPECT_FALSE( MatchModeCatalog::parseCompactMode( "squad:17x4", compact ) );
    SW_EXPECT_FALSE( MatchModeCatalog::parseCompactMode( "squad:2x17", compact ) );
    SW_EXPECT_TRUE( MatchModeCatalog::parseCompactMode( "squad:16x16", compact ) );
}

/**
 * @brief [OnlineServerKitsTest] 모르는 정렬 · 순위 구간이 뒤집힌 보상 · 범위 밖 팀 인원 · 모르는 속성은 기본값으로 물러나지 않고 그 원소를 넣지 않는 오류다
 */
SW_TEST_CASE( OnlineServerKitsTest, LeaderboardAndMatchModeRejectBrokenElements )
{
    LeaderboardCatalog boards;
    {
        SW_TEST_DEFENSIVE_SCOPE( "broken boards and achievements are rejected" );
        SW_ASSERT_TRUE( boards.loadFromXMLText( R"(<LeaderboardCatalog>
            <Board id="kills"/>
            <Board id="broken" order="sideways"/>
            <Board id="flipped" reset="Season">
                <Season id="1"><Reward from="10" to="1" asset="cur.gem" amount="100" mailTitle="mail.season_reward"/></Season>
            </Board>
            <Board id="late" resetMinute="1440"/>
            <Achievement id="nostat" threshold="1"/>
        </LeaderboardCatalog>)" ) );
    }
    SW_ASSERT_EQUAL( boards.getBoards().size(), size_t( 1 ) );
    SW_EXPECT_TRUE( boards.getBoards()[0]._boardID == "kills" );
    SW_EXPECT_TRUE( boards.getAchievements().empty() );

    MatchModeCatalog modes;
    {
        SW_TEST_DEFENSIVE_SCOPE( "broken match modes are rejected" );
        SW_ASSERT_TRUE( modes.loadFromXMLText( R"(<MatchModeCatalog><Mode id="duo" teamCount="2" teamSize="2"/><Mode id="bad" teamSize="0"/>)"
                                               R"(<Mode id="typo" teamSzie="2"/><Mode teamSize="2"/></MatchModeCatalog>)" ) );
    }
    SW_ASSERT_EQUAL( modes.getModes().size(), size_t( 1 ) );
    SW_EXPECT_TRUE( modes.getModes()[0]._modeID == "duo" );
}
