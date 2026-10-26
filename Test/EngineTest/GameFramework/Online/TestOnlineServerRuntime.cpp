#include "pch.h"

#include "Core/Network/Transport/LoopbackStreamTransport.h"

#include "Engine/Observability/ServiceHealthRegistry.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Base/Online/Server/OnlineServerRuntime.h"

#include "TestFramework/TestFramework.h"

// 온라인 서비스 서버 조립 — 키트를 이름 의존 순서로 세우고(설정 순서 · 이름 순서와 상관없이) 반대로 내린다, 앞 키트가 지역 범위에 건 인터페이스를 뒤 키트가 받는다,
// 모르는 키트 · 빠진 필수 키트 · 초기화와 시작 실패는 기동을 멈추고 만든 것을 되돌린다, 온라인 설정 파일 경로, 상태 검사.

using namespace sw;

namespace
{
    struct OnlineServerRuntimeTestInternal
    {
        /** @brief 키트가 남기는 사건 줄입니다. */
        static vector<string>& getLog()
        {
            static vector<string> s_listLine;
            return s_listLine;
        }

        static void add( const utf8* pName, const utf8* pEvent )
        {
            string line{ pName };
            line += ':';
            line += pEvent;
            getLog().push_back( std::move( line ) );
        }

        static void reset() { vector<string>{}.swap( getLog() ); }

        static OnlineServerRuntimeConfig makeSettings( std::initializer_list<const utf8*> listKitName )
        {
            OnlineServerRuntimeConfig settings;
            settings._bTLS                  = SW_FALSE;
            settings._bManualCacheTime      = SW_TRUE;
            settings._config._ioThreadCount = 0;
            for ( const utf8* pName : listKitName )
            {
                OnlineServerKitEntry& entry = settings._config._listKit.emplace_back();
                entry._name                 = pName;
            }
            return settings;
        }
    };

    /** @brief 가짜 계정 디렉터리 — 앞 키트가 문맥에 내놓는 인터페이스 역할입니다. */
    class TestDirectory final : public IAccountDirectory
    {
    public:
        bool findIdentity( AccountID accountID, AccountIdentity& outIdentity ) const override
        {
            outIdentity._accountID = accountID;
            return true;
        }
        bool findIdentityByDisplayName( string_view displayName, AccountIdentity& outIdentity ) const override
        {
            outIdentity._displayName = string( displayName );
            return false;
        }
        bool isAccountOnline( AccountID accountID ) const override { return accountID != kInvalidAccountID; }
    };

    /** @brief 신원을 내놓는 키트입니다. */
    class ProviderTestKit final : public IOnlineServerKit
    {
    public:
        static constexpr const utf8* kName = "test.provider";

        bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
        {
            OnlineServerRuntimeTestInternal::add( kName, context._pHost != nullptr && context._pStore != nullptr ? "init" : "init-without-base" );
            return exposeInterface<IAccountDirectory>( context, entry, _directory, outError );
        }
        bool start( OnlineServerKitContext& context, string& outError ) override
        {
            (void)outError;
            OnlineServerRuntimeTestInternal::add( kName, context._pRouter != nullptr ? "start" : "start-without-router" );
            return true;
        }
        void stop() override { OnlineServerRuntimeTestInternal::add( kName, "stop" ); }
        void shutdown() override { OnlineServerRuntimeTestInternal::add( kName, "shutdown" ); }

    private:
        TestDirectory _directory;
    };

    /** @brief 앞 키트의 신원을 받아 쓰는 키트입니다(필수 의존). 옵션 `fail=init` · `fail=start` 이면 그 단계에 실패합니다. */
    class ConsumerTestKit final : public IOnlineServerKit
    {
    public:
        static constexpr const utf8* kName = "test.consumer";

        bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
        {
            (void)context;
            string failStage;
            if ( entry.readOptions( {
                                        { "fail", failStage }
            },
                                    outError ) == false )
                return false;
            if ( failStage == "init" )
            {
                outError = "test init failure";
                return false;
            }
            _bFailStart = failStage == "start";
            OnlineServerRuntimeTestInternal::add( kName, "init" );
            return true;
        }
        bool start( OnlineServerKitContext& context, string& outError ) override
        {
            if ( _bFailStart )
            {
                outError = "test start failure";
                return false;
            }
            OnlineServerRuntimeTestInternal::add( kName, context._kits.get<IAccountDirectory>().isAccountOnline( 7 ) ? "start" : "start-without-directory" );
            return true;
        }
        void stop() override { OnlineServerRuntimeTestInternal::add( kName, isStarted() ? "stop" : "stop-unstarted" ); }
        void shutdown() override { OnlineServerRuntimeTestInternal::add( kName, "shutdown" ); }

        span<const utf8* const> getRequiredKits() const override
        {
            static constexpr const utf8* kArrKit[] = { ProviderTestKit::kName };
            return kArrKit;
        }

    private:
        bool _bFailStart{ false };
    };
} // namespace

SW_TEST_CASE( OnlineServerRuntimeTest, KitsStartInOrderShareInterfacesAndStopInReverse )
{
    OnlineServerRuntimeTestInternal::reset();
    {
        const OnlineServerKitRegistrar<ConsumerTestKit> consumer{ ConsumerTestKit::kName };
        const OnlineServerKitRegistrar<ProviderTestKit> provider{ ProviderTestKit::kName };
        LoopbackStreamNetwork                           network;
        unique_ptr<IStreamTransport>                    transport = network.createTransport();
        ServiceHealthRegistry                           health;
        // 설정에 거꾸로 적고 이름 순으로도 소비자가 앞이지만, 필수 의존이 내놓는 키트를 먼저 세운다.
        OnlineServerRuntimeConfig settings = OnlineServerRuntimeTestInternal::makeSettings( { ConsumerTestKit::kName, ProviderTestKit::kName } );
        settings._pTransport               = transport.get();
        settings._pHealthRegistry          = &health;
        settings._listenAddress            = NetAddress::makeLoopback( 7451 );
        OnlineServerRuntime runtime;
        string              error;
        SW_ASSERT_TRUE_MSG( runtime.initialize( settings, UnixMilliseconds{ 1000 }, error ), error.c_str() );
        SW_EXPECT_EQUAL( runtime.getListenPort(), uint16( 7451 ) );
        vector<string> listKitName;
        runtime.collectKitNames( listKitName );
        SW_ASSERT_EQUAL( listKitName.size(), size_t( 2 ) );
        SW_EXPECT_TRUE( listKitName[0] == ProviderTestKit::kName );
        SW_EXPECT_TRUE( runtime.getContext()._pBus != nullptr && runtime.getContext()._pRouter != nullptr );
        health.markTick( 10 );
        SW_EXPECT_TRUE( health.isReady( 10 ) ); // 조립이 끝나면 online_services 검사가 Ok
        runtime.update( UnixMilliseconds{ 1100 } );
        runtime.shutdown();
        SW_EXPECT_FALSE( health.isReady( 10 ) ); // 내리면 다시 Failing
    }
    const vector<string>& listLine = OnlineServerRuntimeTestInternal::getLog();
    SW_ASSERT_EQUAL( listLine.size(), size_t( 8 ) );
    SW_EXPECT_TRUE( listLine[0] == "test.provider:init" );
    SW_EXPECT_TRUE( listLine[1] == "test.consumer:init" );
    SW_EXPECT_TRUE( listLine[2] == "test.provider:start" );
    SW_EXPECT_TRUE( listLine[3] == "test.consumer:start" );
    SW_EXPECT_TRUE( listLine[4] == "test.consumer:stop" );
    SW_EXPECT_TRUE( listLine[5] == "test.provider:stop" );
    SW_EXPECT_TRUE( listLine[6] == "test.consumer:shutdown" ); // 저장소를 내리고 남은 완료를 돌린 뒤
    SW_EXPECT_TRUE( listLine[7] == "test.provider:shutdown" );
    OnlineServerRuntimeTestInternal::reset();
}

SW_TEST_CASE( OnlineServerRuntimeTest, MissingRequiredKitStopsStartup )
{
    OnlineServerRuntimeTestInternal::reset();
    {
        const OnlineServerKitRegistrar<ConsumerTestKit> consumer{ ConsumerTestKit::kName };
        const OnlineServerKitRegistrar<ProviderTestKit> provider{ ProviderTestKit::kName };
        LoopbackStreamNetwork                           network;
        unique_ptr<IStreamTransport>                    transport = network.createTransport();
        OnlineServerRuntimeConfig                       settings  = OnlineServerRuntimeTestInternal::makeSettings( { ConsumerTestKit::kName } );
        settings._pTransport                                      = transport.get();
        OnlineServerRuntime runtime;
        string              error;
        SW_EXPECT_FALSE( runtime.initialize( settings, UnixMilliseconds{ 1000 }, error ) );
        SW_EXPECT_TRUE( error.find( "needs kit 'test.provider'" ) != string::npos ); // 필수 인터페이스를 내놓을 키트가 없으면 기동 오류다(빈 칸으로 돌지 않는다)
        runtime.shutdown();
    }
    SW_EXPECT_TRUE( OnlineServerRuntimeTestInternal::getLog().empty() );
    OnlineServerRuntimeTestInternal::reset();
}

SW_TEST_CASE( OnlineServerRuntimeTest, KitInitializeFailureSkipsItsOwnShutdown )
{
    OnlineServerRuntimeTestInternal::reset();
    {
        const OnlineServerKitRegistrar<ConsumerTestKit> consumer{ ConsumerTestKit::kName };
        const OnlineServerKitRegistrar<ProviderTestKit> provider{ ProviderTestKit::kName };
        LoopbackStreamNetwork                           network;
        unique_ptr<IStreamTransport>                    transport = network.createTransport();
        OnlineServerRuntimeConfig                       settings  = OnlineServerRuntimeTestInternal::makeSettings( { ProviderTestKit::kName, ConsumerTestKit::kName } );
        OnlineServerOption&                             option    = settings._config._listKit[1]._listOption.emplace_back();
        option._key                                               = "fail";
        option._value                                             = "init";
        settings._pTransport                                      = transport.get();
        OnlineServerRuntime runtime;
        string              error;
        SW_EXPECT_FALSE( runtime.initialize( settings, UnixMilliseconds{ 1000 }, error ) );
        SW_EXPECT_TRUE( error.find( "test.consumer" ) != string::npos && error.find( "test init failure" ) != string::npos );
        runtime.shutdown();
    }
    // 초기화에 실패한 키트는 스스로 되돌리므로 종료를 받지 않고, 먼저 선 키트만 역순으로 내린다.
    const vector<string>& listLine = OnlineServerRuntimeTestInternal::getLog();
    SW_ASSERT_EQUAL( listLine.size(), size_t( 3 ) );
    SW_EXPECT_TRUE( listLine[0] == "test.provider:init" );
    SW_EXPECT_TRUE( listLine[1] == "test.provider:stop" );
    SW_EXPECT_TRUE( listLine[2] == "test.provider:shutdown" );
    OnlineServerRuntimeTestInternal::reset();
}

SW_TEST_CASE( OnlineServerRuntimeTest, UnknownKitStopsStartup )
{
    OnlineServerRuntimeTestInternal::reset();
    const OnlineServerKitRegistrar<ProviderTestKit> provider{ ProviderTestKit::kName };
    LoopbackStreamNetwork                           network;
    unique_ptr<IStreamTransport>                    transport = network.createTransport();
    OnlineServerRuntimeConfig                       settings  = OnlineServerRuntimeTestInternal::makeSettings( { ProviderTestKit::kName, "test.missing" } );
    settings._pTransport                                      = transport.get();
    OnlineServerRuntime runtime;
    string              error;
    SW_EXPECT_FALSE( runtime.initialize( settings, UnixMilliseconds{ 1000 }, error ) );
    SW_EXPECT_TRUE( error.find( "test.missing" ) != string::npos && error.find( "not in this build" ) != string::npos );
    runtime.shutdown(); // 반쯤 만든 것도 내린다(두 번 불러도 된다)
    runtime.shutdown();
    OnlineServerRuntimeTestInternal::reset();
}

SW_TEST_CASE( OnlineServerRuntimeTest, KitStartFailureUnwindsEveryKit )
{
    OnlineServerRuntimeTestInternal::reset();
    {
        const OnlineServerKitRegistrar<ConsumerTestKit> consumer{ ConsumerTestKit::kName };
        const OnlineServerKitRegistrar<ProviderTestKit> provider{ ProviderTestKit::kName };
        LoopbackStreamNetwork                           network;
        unique_ptr<IStreamTransport>                    transport = network.createTransport();
        OnlineServerRuntimeConfig                       settings  = OnlineServerRuntimeTestInternal::makeSettings( { ProviderTestKit::kName, ConsumerTestKit::kName } );
        OnlineServerOption&                             option    = settings._config._listKit[1]._listOption.emplace_back();
        option._key                                               = "fail";
        option._value                                             = "start";
        settings._pTransport                                      = transport.get();
        settings._listenAddress                                   = NetAddress::makeLoopback( 7452 );
        OnlineServerRuntime runtime;
        string              error;
        SW_EXPECT_FALSE( runtime.initialize( settings, UnixMilliseconds{ 1000 }, error ) );
        SW_EXPECT_TRUE( error.find( "test.consumer" ) != string::npos && error.find( "test start failure" ) != string::npos );
        runtime.shutdown();
    }
    const vector<string>& listLine = OnlineServerRuntimeTestInternal::getLog();
    // 시작에 실패한 키트도 (초기화에 성공했으므로) stop · shutdown 을 받되 isStarted 는 false 이고, 먼저 선 키트는 반대 순서로 내린다.
    SW_ASSERT_EQUAL( listLine.size(), size_t( 7 ) );
    SW_EXPECT_TRUE( listLine[2] == "test.provider:start" );
    SW_EXPECT_TRUE( listLine[3] == "test.consumer:stop-unstarted" );
    SW_EXPECT_TRUE( listLine[4] == "test.provider:stop" );
    SW_EXPECT_TRUE( listLine[5] == "test.consumer:shutdown" );
    SW_EXPECT_TRUE( listLine[6] == "test.provider:shutdown" );
    OnlineServerRuntimeTestInternal::reset();
}

SW_TEST_CASE( OnlineServerRuntimeTest, OnlineConfigSitsNextToTheServerConfig )
{
    SW_EXPECT_TRUE( OnlineServerConfig::makeConfigPath( "Config/Server/Empty.json" ) == "Config/Server/Empty.online.json" );
    SW_EXPECT_TRUE( OnlineServerConfig::makeConfigPath( "ops" ) == "ops.online.json" );
    OnlineServerKitEntry entry;
    OnlineServerOption&  first = entry._listOption.emplace_back();
    first._key                 = "mode";
    first._value               = "solo";
    OnlineServerOption& second = entry._listOption.emplace_back();
    second._key                = "mode";
    second._value              = "duo";
    vector<string> listMode;
    int32          maxPartySize = 4;
    string         error;
    SW_ASSERT_TRUE( entry.readOptions( {
                                           {        "mode",     listMode},
                                           {"maxPartySize", maxPartySize}
    },
                                       error ) );
    SW_ASSERT_EQUAL( listMode.size(), size_t( 2 ) );
    SW_EXPECT_TRUE( listMode[1] == "duo" );
    SW_EXPECT_EQUAL( maxPartySize, 4 ); // 적지 않은 옵션은 칸의 기본값을 남긴다

    // 표에 없는 키는 철자 실수이므로 기동 오류다.
    SW_EXPECT_FALSE( entry.readOptions( {
                                            { "maxPartySize", maxPartySize }
    },
                                        error ) );
    SW_EXPECT_TRUE( error.find( "unknown option 'mode'" ) != string::npos );

    // 칸의 타입이 읽는 법을 정한다 — 정수 칸에 정수가 아닌 값은 오류다.
    OnlineServerKitEntry sized;
    sized._name                    = "matchmaking";
    OnlineServerOption& sizeOption = sized._listOption.emplace_back();
    sizeOption._key                = "maxPartySize";
    sizeOption._value              = "four";
    SW_EXPECT_FALSE( sized.readOptions( {
                                            { "maxPartySize", maxPartySize }
    },
                                        error ) );
    SW_EXPECT_TRUE( error.find( "must be an integer" ) != string::npos );
}
