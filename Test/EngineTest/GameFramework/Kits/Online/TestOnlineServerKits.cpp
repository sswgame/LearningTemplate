#include "pch.h"

#include "Core/Network/Transport/LoopbackStreamTransport.h"

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

        static OnlineServerRuntimeConfig makeSettings( IStreamTransport* pTransport, uint16 port )
        {
            OnlineServerRuntimeConfig settings;
            settings._bTLS                  = SW_FALSE;
            settings._bManualCacheTime      = SW_TRUE;
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
    SW_TEST_SKIP( "the shipping account kit refuses development options and needs a real ticket key secret" );
#endif
    using Internal = OnlineServerKitsTestInternal;
    LoopbackStreamNetwork        network;
    unique_ptr<IStreamTransport> transport = network.createTransport();
    OnlineServerRuntimeConfig    settings  = Internal::makeSettings( transport.get(), 7461 );
    Internal::addOption( Internal::addKit( settings, "account" ), "lightPasswordHash", "1" );
    (void)Internal::addKit( settings, "mailbox" );
    Internal::addOption( Internal::addKit( settings, "trade" ), "tradable", "cur.gold" );
    Internal::addOption( Internal::addKit( settings, "admin" ), "seedAdmin", "1:super" );
    OnlineServerRuntime runtime;
    string              error;
    SW_ASSERT_TRUE_MSG( runtime.initialize( settings, Internal::kStartMs, error ), error.c_str() );
    const OnlineServerKitContext& context = runtime.getContext();
    SW_EXPECT_TRUE( context._pAccountDirectory != nullptr ); // 계정 키트가 내놓았다
    SW_EXPECT_TRUE( context._pPresence != nullptr && context._pSessionControl != nullptr && context._pNameIndex != nullptr );
    SW_EXPECT_TRUE( context._pLedgerPolicy == nullptr ); // 경제 키트를 올리지 않았다
    vector<string> listKitName;
    runtime.collectKitNames( listKitName );
    SW_ASSERT_EQUAL( listKitName.size(), size_t( 4 ) );
    SW_EXPECT_TRUE( listKitName[0] == "account" ); // 신원을 내놓는 키트가 먼저
    for ( int32 tickIndex = 0; tickIndex < 5; ++tickIndex )
    {
        runtime.update( Internal::kStartMs + tickIndex * 100 );
    }
    runtime.shutdown();
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
        SW_EXPECT_FALSE( runtime.initialize( settings, Internal::kStartMs, error ) );
        SW_EXPECT_TRUE( error.find( "ticketKey" ) != string::npos );
    }
    {
        LoopbackStreamNetwork        network;
        unique_ptr<IStreamTransport> transport = network.createTransport();
        OnlineServerRuntimeConfig    settings  = Internal::makeSettings( transport.get(), 7463 );
        Internal::addOption( Internal::addKit( settings, "chat" ), "bannedWords", "none" ); // 계정 키트 없이 채팅
        OnlineServerRuntime runtime;
        string              error;
        SW_EXPECT_FALSE( runtime.initialize( settings, Internal::kStartMs, error ) );
        SW_EXPECT_TRUE( error.find( "account kit" ) != string::npos );
    }
    {
        LoopbackStreamNetwork        network;
        unique_ptr<IStreamTransport> transport = network.createTransport();
        OnlineServerRuntimeConfig    settings  = Internal::makeSettings( transport.get(), 7464 );
        (void)Internal::addKit( settings, "economy" ); // 카탈로그 없이 경제
        OnlineServerRuntime runtime;
        string              error;
        SW_EXPECT_FALSE( runtime.initialize( settings, Internal::kStartMs, error ) );
        SW_EXPECT_TRUE( error.find( "currencyCatalog" ) != string::npos );
    }
    {
        LoopbackStreamNetwork        network;
        unique_ptr<IStreamTransport> transport = network.createTransport();
        OnlineServerRuntimeConfig    settings  = Internal::makeSettings( transport.get(), 7465 );
        OnlineServerRuntime          runtime;
        string                       error;
        SW_EXPECT_FALSE( runtime.initialize( settings, Internal::kStartMs, error ) ); // 키트를 하나도 적지 않았다
        SW_EXPECT_TRUE( error.find( "_listKit" ) != string::npos );
    }
}

SW_TEST_CASE( OnlineServerKitsTest, LeaderboardAndMatchModeDataAreRead )
{
    LeaderboardCatalog boards;
    SW_ASSERT_TRUE( boards.loadFromXMLText( R"(<LeaderboardCatalog>
            <Board id="kills" order="descending" update="sum" reset="weekly" resetMinute="60" resetDay="2" sourceStat="kills" clientSubmit="true"/>
            <Board id="race" order="ascending">
                <Season id="1" startMs="1767225600000" endMs="1769904000000">
                    <Reward from="1" to="10" asset="cur.gem" amount="100" mailTitle="mail.season_reward"/>
                </Season>
            </Board>
            <Board id="broken" order="sideways"/>
            <Achievement id="first_blood" stat="kills" threshold="1" rewardAsset="cur.gold" rewardAmount="100" mailTitle="mail.achievement"/>
        </LeaderboardCatalog>)" ) );
    SW_ASSERT_EQUAL( boards.getBoards().size(), size_t( 2 ) ); // 모르는 정렬의 보드는 건너뛴다(경고)
    const LeaderboardDefinition& kills = boards.getBoards()[0];
    SW_EXPECT_TRUE( kills._update == LeaderboardUpdate::Sum && kills._reset == LeaderboardReset::Weekly );
    SW_EXPECT_EQUAL( kills._resetMinuteOfDay, 60 );
    SW_EXPECT_TRUE( kills._bClientSubmit == SW_TRUE );
    const LeaderboardDefinition& race = boards.getBoards()[1];
    SW_EXPECT_TRUE( race._order == LeaderboardOrder::Ascending );
    SW_ASSERT_EQUAL( race._listSeason.size(), size_t( 1 ) );
    SW_ASSERT_EQUAL( race._listSeason[0]._listRewardTier.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( race._listSeason[0]._listRewardTier[0]._amount, int64( 100 ) );
    SW_ASSERT_EQUAL( boards.getAchievements().size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( boards.getAchievements()[0]._rewardAmount, int64( 100 ) );

    MatchModeCatalog modes;
    SW_ASSERT_TRUE( modes.loadFromXMLText( R"(<MatchModeCatalog><Mode id="duo" teamCount="2" teamSize="2" maxWaitMs="60000"/><Mode id="bad" teamSize="0"/></MatchModeCatalog>)" ) );
    SW_ASSERT_EQUAL( modes.getModes().size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( modes.getModes()[0]._maxWaitMs, int64( 60000 ) );
    MatchModeDefinition compact;
    SW_EXPECT_TRUE( MatchModeCatalog::parseCompactMode( "squad:2x4", compact ) );
    SW_EXPECT_TRUE( compact._modeID == "squad" && compact._teamCount == 2 && compact._teamSize == 4 );
    SW_EXPECT_FALSE( MatchModeCatalog::parseCompactMode( "squad:2", compact ) );
    SW_EXPECT_FALSE( MatchModeCatalog::parseCompactMode( ":2x2", compact ) );
}
