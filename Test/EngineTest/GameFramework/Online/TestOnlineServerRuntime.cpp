#include "pch.h"

#include "Core/Network/Transport/LoopbackStreamTransport.h"

#include "Engine/Observability/ServiceHealthRegistry.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Base/Online/Server/OnlineServerRuntime.h"

#include "TestFramework/TestFramework.h"

// 온라인 서비스 서버 조립 — 키트를 시작 순서대로 붙이고(설정 순서와 상관없이) 반대로 내린다, 앞 키트가 내놓은 인터페이스를 뒤 키트가 받는다,
// 모르는 키트와 시작 실패는 기동을 멈추고 만든 것을 되돌린다, 온라인 설정 파일 경로, 상태 검사.

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

        static OnlineServerRuntimeSettings makeSettings( std::initializer_list<const utf8*> listKitName )
        {
            OnlineServerRuntimeSettings settings;
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
            (void)entry;
            (void)outError;
            OnlineServerRuntimeTestInternal::add( kName, context._pHost != nullptr && context._pStore != nullptr ? "init" : "init-without-base" );
            context._pAccountDirectory = &_directory;
            return true;
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

    /** @brief 앞 키트의 신원을 받아 쓰는 키트입니다. 옵션 `fail=start` 이면 시작에 실패합니다. */
    class ConsumerTestKit final : public IOnlineServerKit
    {
    public:
        static constexpr const utf8* kName = "test.consumer";

        bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
        {
            (void)context;
            (void)outError;
            const string* pFail = entry.findOption( "fail" );
            _bFailStart         = pFail != nullptr && *pFail == "start";
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
            OnlineServerRuntimeTestInternal::add( kName, context._pAccountDirectory != nullptr && context._pAccountDirectory->isAccountOnline( 7 ) ? "start" : "start-without-directory" );
            return true;
        }
        void stop() override { OnlineServerRuntimeTestInternal::add( kName, "stop" ); }

    private:
        bool _bFailStart{ false };
    };
} // namespace

SW_TEST_CASE( OnlineServerRuntimeTest, KitsStartInOrderShareInterfacesAndStopInReverse )
{
    OnlineServerRuntimeTestInternal::reset();
    {
        const OnlineServerKitRegistrar<ConsumerTestKit> consumer{ ConsumerTestKit::kName, 9100 };
        const OnlineServerKitRegistrar<ProviderTestKit> provider{ ProviderTestKit::kName, 9000 };
        LoopbackStreamNetwork                           network;
        unique_ptr<IStreamTransport>                    transport = network.createTransport();
        ServiceHealthRegistry                           health;
        // 설정에는 거꾸로 적어도 시작 순서(인터페이스를 내놓는 키트가 먼저)로 붙는다.
        OnlineServerRuntimeSettings settings = OnlineServerRuntimeTestInternal::makeSettings( { ConsumerTestKit::kName, ProviderTestKit::kName } );
        settings._pTransport                 = transport.get();
        settings._pHealthRegistry            = &health;
        settings._listenAddress              = NetAddress::makeLoopback( 7451 );
        OnlineServerRuntime runtime;
        string              error;
        SW_ASSERT_TRUE_MSG( runtime.initialize( settings, 1000, error ), error.c_str() );
        SW_EXPECT_EQUAL( runtime.getListenPort(), uint16( 7451 ) );
        vector<string> listKitName;
        runtime.collectKitNames( listKitName );
        SW_ASSERT_EQUAL( listKitName.size(), size_t( 2 ) );
        SW_EXPECT_TRUE( listKitName[0] == ProviderTestKit::kName );
        SW_EXPECT_TRUE( runtime.getContext()._pBus != nullptr && runtime.getContext()._pRouter != nullptr );
        health.markTick( 10 );
        SW_EXPECT_TRUE( health.isReady( 10 ) ); // 조립이 끝나면 online_services 검사가 Ok
        runtime.update( 1100 );
        runtime.shutdown();
        SW_EXPECT_FALSE( health.isReady( 10 ) ); // 내리면 다시 Failing
    }
    const vector<string>& listLine = OnlineServerRuntimeTestInternal::getLog();
    SW_ASSERT_EQUAL( listLine.size(), size_t( 7 ) );
    SW_EXPECT_TRUE( listLine[0] == "test.provider:init" );
    SW_EXPECT_TRUE( listLine[1] == "test.consumer:init" );
    SW_EXPECT_TRUE( listLine[2] == "test.provider:start" );
    SW_EXPECT_TRUE( listLine[3] == "test.consumer:start" );
    SW_EXPECT_TRUE( listLine[4] == "test.consumer:stop" );
    SW_EXPECT_TRUE( listLine[5] == "test.provider:stop" );
    SW_EXPECT_TRUE( listLine[6] == "test.provider:shutdown" ); // 저장소를 내리고 남은 완료를 돌린 뒤
    OnlineServerRuntimeTestInternal::reset();
}

SW_TEST_CASE( OnlineServerRuntimeTest, UnknownKitStopsStartup )
{
    OnlineServerRuntimeTestInternal::reset();
    const OnlineServerKitRegistrar<ProviderTestKit> provider{ ProviderTestKit::kName, 9000 };
    LoopbackStreamNetwork                           network;
    unique_ptr<IStreamTransport>                    transport = network.createTransport();
    OnlineServerRuntimeSettings                     settings  = OnlineServerRuntimeTestInternal::makeSettings( { ProviderTestKit::kName, "test.missing" } );
    settings._pTransport                                      = transport.get();
    OnlineServerRuntime runtime;
    string              error;
    SW_EXPECT_FALSE( runtime.initialize( settings, 1000, error ) );
    SW_EXPECT_TRUE( error.find( "test.missing" ) != string::npos && error.find( "not in this build" ) != string::npos );
    runtime.shutdown(); // 반쯤 만든 것도 내린다(두 번 불러도 된다)
    runtime.shutdown();
    OnlineServerRuntimeTestInternal::reset();
}

SW_TEST_CASE( OnlineServerRuntimeTest, KitStartFailureUnwindsEveryKit )
{
    OnlineServerRuntimeTestInternal::reset();
    {
        const OnlineServerKitRegistrar<ConsumerTestKit> consumer{ ConsumerTestKit::kName, 9100 };
        const OnlineServerKitRegistrar<ProviderTestKit> provider{ ProviderTestKit::kName, 9000 };
        LoopbackStreamNetwork                           network;
        unique_ptr<IStreamTransport>                    transport = network.createTransport();
        OnlineServerRuntimeSettings                     settings  = OnlineServerRuntimeTestInternal::makeSettings( { ProviderTestKit::kName, ConsumerTestKit::kName } );
        OnlineServerOption&                             option    = settings._config._listKit[1]._listOption.emplace_back();
        option._key                                               = "fail";
        option._value                                             = "start";
        settings._pTransport                                      = transport.get();
        settings._listenAddress                                   = NetAddress::makeLoopback( 7452 );
        OnlineServerRuntime runtime;
        string              error;
        SW_EXPECT_FALSE( runtime.initialize( settings, 1000, error ) );
        SW_EXPECT_TRUE( error.find( "test.consumer" ) != string::npos && error.find( "test start failure" ) != string::npos );
        runtime.shutdown();
    }
    const vector<string>& listLine = OnlineServerRuntimeTestInternal::getLog();
    // 실패한 키트도 stop 을 받고, 먼저 붙은 키트는 반대 순서로 내린다.
    SW_ASSERT_EQUAL( listLine.size(), size_t( 6 ) );
    SW_EXPECT_TRUE( listLine[2] == "test.provider:start" );
    SW_EXPECT_TRUE( listLine[3] == "test.consumer:stop" );
    SW_EXPECT_TRUE( listLine[4] == "test.provider:stop" );
    SW_EXPECT_TRUE( listLine[5] == "test.provider:shutdown" );
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
    entry.collectOptions( "mode", listMode );
    SW_ASSERT_EQUAL( listMode.size(), size_t( 2 ) );
    SW_EXPECT_TRUE( listMode[1] == "duo" );
    SW_EXPECT_TRUE( entry.findOption( "missing" ) == nullptr );
}
