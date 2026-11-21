#include "pch.h"

#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/Clock.h"

#include "Engine/Config/ServerConfig.h"
#include "Engine/Server/ServiceHealthRegistry.h"

#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStoreRouter.h"
#include "GameFramework/Runtime/Online/Directory/ServerRecord.h"
#include "GameFramework/Runtime/Online/Identity/Server/AccountDirectory.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Runtime/Online/Server/IOnlineStoreDriver.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerRuntime.h"
#include "GameFramework/Runtime/Online/Server/Tests/OnlineConfigTestTypes.h"

// OnlineServerRuntimeTest — 온라인 서비스 서버 조립: 키트를 이름 의존 순서로 세우고(설정 순서 · 이름 순서와 상관없이) 반대로 내린다, 앞 키트가 지역 범위에 건 인터페이스를
// 뒤 키트가 받는다, 모르는 키트 · 빠진 필수 키트 · 초기화와 시작 실패는 기동을 멈추고 만든 것을 되돌린다, 상태 검사(online_services)(nogpu).

using namespace sw;

namespace
{
    /** @brief 키트가 남기는 사건 줄입니다. 픽스처가 케이스 앞뒤로 비운다(케이스 순서는 섞인다). */
    vector<string>& getKitEventLog()
    {
        static vector<string> s_listLine;
        return s_listLine;
    }

    void addKitEvent( const utf8* pName, const utf8* pEvent )
    {
        StringBuilder<constant::buffer::kMax128> line;
        line.appendFormat( "%#:%#", pName, pEvent );
        getKitEventLog().push_back( string( line.view() ) );
    }

    /** @brief 테스트 제공 키트가 내놓는 디렉터리입니다. 제공 키트 상수가 테스트 키트 이름이라 받는 쪽의 의존 이름이 그것이 된다. */
    class ITestDirectory : public IAccountDirectory
    {
    public:
        static constexpr const utf8* kProviderKit = "test.provider";
    };

    /** @brief 이름은 제공 키트와 같지만 아무것도 걸지 않는 키트가 낼 것으로 적힌 인터페이스입니다. */
    class ITestSilent
    {
    public:
        static constexpr const utf8* kProviderKit = "test.silent";

        ITestSilent()          = default;
        virtual ~ITestSilent() = default;

        ITestSilent( const ITestSilent& )            = delete;
        ITestSilent& operator=( const ITestSilent& ) = delete;
    };

    /** @brief 가짜 계정 디렉터리 — 앞 키트가 문맥에 내놓는 인터페이스 역할입니다. */
    class TestDirectory final : public ITestDirectory
    {
    public:
        [[nodiscard]] bool findIdentity( AccountID accountID, AccountIdentity& outIdentity ) const override
        {
            outIdentity._accountID = accountID;
            return true;
        }
        [[nodiscard]] bool findIdentityByDisplayName( string_view displayName, AccountIdentity& outIdentity ) const override
        {
            outIdentity._displayName = string( displayName );
            return false;
        }
        bool isAccountOnline( AccountID accountID ) const override { return accountID != kInvalidAccountID; }
    };

    /** @brief 신원을 내놓는 키트입니다. */
    class ProviderTestKit final : public OnlineServerKit<>
    {
    public:
        static constexpr const utf8* kName = ITestDirectory::kProviderKit;

        [[nodiscard]] expected<void> initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, const Dependencies& ) override
        {
            addKitEvent( kName, context._pHost != nullptr && context._pStore != nullptr ? "init" : "init-without-base" );
            return exposeInterface<ITestDirectory>( context, entry, _directory );
        }
        expected<void> start( OnlineServerKitContext& context, const Dependencies&, const UnorderedDependencies& ) override
        {
            addKitEvent( kName, context._pRouter != nullptr ? "start" : "start-without-router" );
            return {};
        }
        const sw::IAfterDeleteAccount* getAfterDeleteAccount() const override { return nullptr; } // 테스트 키트는 표가 없다
        void                           stop() override { addKitEvent( kName, "stop" ); }
        void                           shutdown() override { addKitEvent( kName, "shutdown" ); }

    private:
        TestDirectory _directory;
    };

    /** @brief 앞 키트의 신원을 받아 쓰는 키트입니다(필수 의존). 설정 `_failStage` 가 `init` · `start` 이면 그 단계에 실패합니다. */
    class ConsumerTestKit final : public OnlineServerKit<OnlineKitRequired<ITestDirectory>>
    {
    public:
        static constexpr const utf8* kName = "test.consumer";

        ConsumerTestKit()
            : _bFailStart{ SW_FALSE }
        {
        }
        [[nodiscard]] expected<void> initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, const Dependencies& ) override
        {
            (void)context;
            OnlineRuntimeTestKitConfig kitConfig;
            if ( expected<void> settings = entry.readSettings( kitConfig ); settings.has_value() == false )
                return settings;
            const string& failStage = kitConfig._failStage;
            if ( failStage == "init" )
                return sw::unexpected( Error{ ErrorKind::InvalidArgument, "test init failure" } );
            _bFailStart = failStage == "start";
            addKitEvent( kName, "init" );
            return {};
        }
        expected<void> start( OnlineServerKitContext& context, const Dependencies& interfaces, const UnorderedDependencies& ) override
        {
            (void)context;
            if ( _bFailStart )
                return sw::unexpected( Error{ ErrorKind::InvalidArgument, "test start failure" } );
            addKitEvent( kName, interfaces.get<ITestDirectory>().isAccountOnline( 7 ) ? "start" : "start-without-directory" );
            return {};
        }
        const sw::IAfterDeleteAccount* getAfterDeleteAccount() const override { return nullptr; } // 테스트 키트는 표가 없다
        void                           stop() override { addKitEvent( kName, isStarted() ? "stop" : "stop-unstarted" ); }
        void                           shutdown() override { addKitEvent( kName, "shutdown" ); }

    private:
        uint8 _bFailStart : 1;
    };

    /** @brief 제공 키트 이름(`test.silent`)으로 서지만 그 인터페이스를 걸지 않는 키트입니다. */
    class SilentTestKit final : public OnlineServerKit<>
    {
    public:
        static constexpr const utf8* kName = ITestSilent::kProviderKit;

        [[nodiscard]] expected<void> initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, const Dependencies& ) override
        {
            (void)context;
            (void)entry;
            addKitEvent( kName, "init" );
            return {};
        }
        expected<void> start( OnlineServerKitContext& context, const Dependencies&, const UnorderedDependencies& ) override
        {
            (void)context;
            return {};
        }
        const sw::IAfterDeleteAccount* getAfterDeleteAccount() const override { return nullptr; } // 테스트 키트는 표가 없다
        void                           stop() override { addKitEvent( kName, "stop" ); }
        void                           shutdown() override { addKitEvent( kName, "shutdown" ); }
    };

    /** @brief 걸리지 않은 인터페이스를 필수로 받는 키트입니다. 런타임이 이 키트의 `initialize` 를 부르면 안 된다. */
    class NeedsSilentTestKit final : public OnlineServerKit<OnlineKitRequired<ITestSilent>>
    {
    public:
        static constexpr const utf8* kName = "test.needsSilent";

        [[nodiscard]] expected<void> initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, const Dependencies& ) override
        {
            (void)context;
            (void)entry;
            addKitEvent( kName, "init" );
            return {};
        }
        expected<void> start( OnlineServerKitContext& context, const Dependencies&, const UnorderedDependencies& ) override
        {
            (void)context;
            return {};
        }
        const sw::IAfterDeleteAccount* getAfterDeleteAccount() const override { return nullptr; } // 테스트 키트는 표가 없다
        void                           stop() override { addKitEvent( kName, "stop" ); }
    };

    /** @brief 디렉터리를 순서 없이 받는 키트입니다. 의존이 아니어도 `start` 는 모든 키트의 `initialize` 뒤라 찾는다. */
    class UnorderedTestKit final : public OnlineServerKit<OnlineKitRequired<>, OnlineKitOptional<>, OnlineKitUnordered<ITestDirectory>>
    {
    public:
        static constexpr const utf8* kName = "test.unordered";

        [[nodiscard]] expected<void> initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, const Dependencies& ) override
        {
            (void)context;
            (void)entry;
            addKitEvent( kName, "init" );
            return {};
        }
        expected<void> start( OnlineServerKitContext& context, const Dependencies&, const UnorderedDependencies& unordered ) override
        {
            (void)context;
            addKitEvent( kName, unordered.find<ITestDirectory>() != nullptr ? "start" : "start-without-directory" );
            return {};
        }
        const sw::IAfterDeleteAccount* getAfterDeleteAccount() const override { return nullptr; } // 테스트 키트는 표가 없다
        void                           stop() override {}
    };

    /**
     * @brief 의존 이름이 형식 목록에서만 나오는지 보는 키트입니다(조립에 올리지 않는다).
     * @details 선언하지 않은 형식을 꺼내면(`interfaces.find<AccountPresence>()`) `static_assert` 로 컴파일이 멈추므로 실행 테스트로는 재지 않는다.
     */
    class TypeListTestKit final
        : public OnlineServerKit<OnlineKitRequired<ITestDirectory, IAccountDirectory>, OnlineKitOptional<const IAccountBlockList, IOfflinePushSender>, OnlineKitUnordered<const ILedgerPolicy>>
    {
    public:
        [[nodiscard]] expected<void> initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, const Dependencies& ) override
        {
            (void)context;
            (void)entry;
            return {};
        }
        expected<void> start( OnlineServerKitContext& context, const Dependencies&, const UnorderedDependencies& ) override
        {
            (void)context;
            return {};
        }
        const sw::IAfterDeleteAccount* getAfterDeleteAccount() const override { return nullptr; } // 테스트 키트는 표가 없다
        void                           stop() override {}
    };

    /** @brief 서버 색인 읽기의 답을 받아 둡니다(라우터 델리게이트는 멤버 함수다). */
    struct DirectoryIndexProbe
    {
        int32 _replyCount{ 0 };
        int32 _memberCount{ 0 };
        void  onReply( const EphemeralReply& reply )
        {
            ++_replyCount;
            _memberCount = static_cast<int32>( reply._listMember.size() );
        }
    };

    /** @brief 케이스마다 두 테스트 키트를 레지스트리에 올리고 사건 줄을 비웁니다. 소멸자가 둘 다 되돌립니다. */
    struct OnlineServerRuntimeTest
    {

        const OnlineServerKitRegistrar<ConsumerTestKit> _consumer{ ConsumerTestKit::kName };
        const OnlineServerKitRegistrar<ProviderTestKit> _provider{ ProviderTestKit::kName };
        SystemMonotonicClock                            _clock{};
        LoopbackStreamNetwork                           _network{};
        unique_ptr<IStreamTransport>                    _transport{ _network.createTransport() };
        OnlineServerRuntimeTest() { getKitEventLog().clear(); }
        ~OnlineServerRuntimeTest() { getKitEventLog().clear(); }

        /** @brief 런타임 설정입니다. 전송은 루프백, TLS 는 끈다. */
        OnlineServerRuntimeConfig makeSettings( std::initializer_list<const utf8*> listKitName )
        {
            OnlineServerRuntimeConfig settings;
            settings._bTLS                              = SW_FALSE;
            settings._pClock                            = &_clock;
            settings._pTransport                        = _transport.get();
            settings._config._ioThreadCount             = 0;
            settings._config._maxPendingCallsPerService = 100000; // 온라인 서버 설정 파일의 값 — 필수 칸이다
            settings._store._driver                     = kMemoryStoreDriverName;
            settings._cache._driver                     = kMemoryStoreDriverName;
            for ( const utf8* pName : listKitName )
            {
                OnlineServerKitEntry& entry = settings._config._listKit.emplace_back();
                entry._name                 = pName;
            }
            return settings;
        }
    };
} // namespace

/** @brief [OnlineServerRuntimeTest] 설정에 거꾸로 적고 이름 순으로도 소비자가 앞이지만 필수 의존이 내놓는 키트를 먼저 세우고, 내릴 때는 반대 순서다 */
SW_TEST_CASE_F( OnlineServerRuntimeTest, KitsStartInOrderShareInterfacesAndStopInReverse )
{
    ServiceHealthRegistry     health;
    OnlineServerRuntimeConfig settings = makeSettings( { ConsumerTestKit::kName, ProviderTestKit::kName } );
    settings._pHealthRegistry          = &health;
    settings._listenAddress            = NetAddress::makeLoopback( 7451 );
    {
        OnlineServerRuntime runtime;
        {
            const auto result = runtime.initialize( settings, UnixMilliseconds{ 1000 } );
            SW_REQUIRE_TRUE_MSG( result.has_value(), result.has_value() ? std::string_view{} : result.error().message() );
        }
        SW_EXPECT_EQUAL( runtime.getListenPort(), uint16( 7451 ) );
        vector<string> listKitName;
        runtime.collectKitNames( listKitName );
        SW_REQUIRE_EQUAL( listKitName.size(), size_t( 2 ) );
        SW_EXPECT_EQUAL( listKitName[0], string( ProviderTestKit::kName ) );
        SW_EXPECT_TRUE( runtime.getContext()._pBus != nullptr && runtime.getContext()._pRouter != nullptr );
        health.markTick( 10 );
        SW_EXPECT_TRUE( health.isReady( 10 ) ); // 조립이 끝나면 online_services 검사가 Ok
        runtime.update( UnixMilliseconds{ 1100 } );
        runtime.shutdown();
        SW_EXPECT_FALSE( health.isReady( 10 ) ); // 내리면 다시 Failing
    }
    const vector<string>& listLine = getKitEventLog();
    SW_REQUIRE_EQUAL( listLine.size(), size_t( 8 ) );
    SW_EXPECT_EQUAL( listLine[0], string( "test.provider:init" ) );
    SW_EXPECT_EQUAL( listLine[1], string( "test.consumer:init" ) );
    SW_EXPECT_EQUAL( listLine[2], string( "test.provider:start" ) );
    SW_EXPECT_EQUAL( listLine[3], string( "test.consumer:start" ) );
    SW_EXPECT_EQUAL( listLine[4], string( "test.consumer:stop" ) );
    SW_EXPECT_EQUAL( listLine[5], string( "test.provider:stop" ) );
    SW_EXPECT_EQUAL( listLine[6], string( "test.consumer:shutdown" ) ); // 저장소를 내리고 남은 완료를 돌린 뒤
    SW_EXPECT_EQUAL( listLine[7], string( "test.provider:shutdown" ) );
}

/** @brief [OnlineServerRuntimeTest] 필수 인터페이스를 내놓을 키트가 설정에 없으면 기동 오류다(빈 칸으로 돌지 않는다) */
SW_TEST_CASE_F( OnlineServerRuntimeTest, MissingRequiredKitStopsStartup )
{
    {
        OnlineServerRuntime runtime;
        const auto          failed1 = runtime.initialize( makeSettings( { ConsumerTestKit::kName } ), UnixMilliseconds{ 1000 } );
        SW_REQUIRE_FALSE( failed1.has_value() );
        StringBuilder<constant::buffer::kMax512> error1Builder;
        error1Builder.appendFormat( "%# %#", failed1.error().message(), failed1.error().detail() );
        const sw::string error1( error1Builder.view() );
        SW_EXPECT_TRUE( error1.find( "needs test.provider" ) != string::npos );
        runtime.shutdown();
    }
    SW_EXPECT_TRUE( getKitEventLog().empty() );
}

/** @brief [OnlineServerRuntimeTest] 의존 키트 이름은 받는 인터페이스 형식의 제공 키트 상수에서만 나온다 — 필수 · 선택은 형식 순서대로, 순서 없는 목록은 이름에 들지 않는다 */
SW_TEST_CASE_F( OnlineServerRuntimeTest, ProviderKitNamesComeFromTheInterfaceTypeList )
{
    TypeListTestKit               kit; // 조립에 올리지 않고 이름 목록만 본다
    const span<const utf8* const> listRequired = kit.getRequiredKits();
    SW_REQUIRE_EQUAL( listRequired.size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( string_view( listRequired[0] ), string_view( "test.provider" ) );
    SW_EXPECT_EQUAL( string_view( listRequired[1] ), string_view( OnlineServerKitName::kAccount ) );
    const span<const utf8* const> listOptional = kit.getOptionalKits();
    SW_REQUIRE_EQUAL( listOptional.size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( string_view( listOptional[0] ), string_view( OnlineServerKitName::kSocial ) );
    SW_EXPECT_EQUAL( string_view( listOptional[1] ), string_view( OnlineServerKitName::kLiveOps ) );

    ProviderTestKit noDependency;
    SW_EXPECT_EQUAL( noDependency.getRequiredKits().size(), size_t( 0 ) );
    SW_EXPECT_EQUAL( noDependency.getOptionalKits().size(), size_t( 0 ) );
}

/** @brief [OnlineServerRuntimeTest] 제공 키트가 설정에 있어도 필수 인터페이스를 걸지 않았으면 받는 키트의 initialize 를 부르지 않고 기동 오류다(null 로 돌지 않는다) */
SW_TEST_CASE_F( OnlineServerRuntimeTest, RequiredInterfaceTheProviderDidNotExposeStopsStartup )
{
    const OnlineServerKitRegistrar<SilentTestKit>      silent{ SilentTestKit::kName };
    const OnlineServerKitRegistrar<NeedsSilentTestKit> needsSilent{ NeedsSilentTestKit::kName };
    {
        OnlineServerRuntime runtime;
        const auto          failed1 = runtime.initialize( makeSettings( { NeedsSilentTestKit::kName, SilentTestKit::kName } ), UnixMilliseconds{ 1000 } );
        SW_REQUIRE_FALSE( failed1.has_value() );
        StringBuilder<constant::buffer::kMax512> error1Builder;
        error1Builder.appendFormat( "%# %#", failed1.error().message(), failed1.error().detail() );
        const sw::string error1( error1Builder.view() );
        SW_EXPECT_TRUE( error1.find( "did not expose" ) != string::npos && error1.find( "test.silent" ) != string::npos );
        runtime.shutdown();
    }
    const vector<string>& listLine = getKitEventLog();
    SW_REQUIRE_EQUAL( listLine.size(), size_t( 3 ) ); // 받는 키트는 init 을 받지 않았으므로 stop 도 받지 않는다
    SW_EXPECT_EQUAL( listLine[0], string( "test.silent:init" ) );
    SW_EXPECT_EQUAL( listLine[1], string( "test.silent:stop" ) );
    SW_EXPECT_EQUAL( listLine[2], string( "test.silent:shutdown" ) );
}

/** @brief [OnlineServerRuntimeTest] 순서 없는 목록은 의존이 아니어서 이름 역순으로 제공 키트보다 먼저 서도 start 에서는 그 인터페이스를 받는다 */
SW_TEST_CASE_F( OnlineServerRuntimeTest, UnorderedInterfaceIsFoundAtStartWithoutOrdering )
{
    const OnlineServerKitRegistrar<UnorderedTestKit> unordered{ UnorderedTestKit::kName };
    OnlineServerRuntimeConfig                        settings = makeSettings( { ProviderTestKit::kName, UnorderedTestKit::kName } );
    settings._bReverseIndependentKitOrder                     = SW_TRUE; // test.unordered 가 test.provider 보다 먼저 선다
    {
        OnlineServerRuntime runtime;
        {
            const auto result = runtime.initialize( settings, UnixMilliseconds{ 1000 } );
            SW_REQUIRE_TRUE_MSG( result.has_value(), result.has_value() ? std::string_view{} : result.error().message() );
        }
        runtime.shutdown();
    }
    const vector<string>& listLine = getKitEventLog();
    SW_REQUIRE_TRUE( listLine.size() >= size_t( 4 ) );
    SW_EXPECT_EQUAL( listLine[0], string( "test.unordered:init" ) );
    SW_EXPECT_EQUAL( listLine[1], string( "test.provider:init" ) );
    SW_EXPECT_EQUAL( listLine[2], string( "test.unordered:start" ) );
    SW_EXPECT_EQUAL( listLine[3], string( "test.provider:start" ) );
}

/** @brief [OnlineServerRuntimeTest] 초기화에 실패한 키트는 스스로 되돌리므로 종료를 받지 않고, 먼저 선 키트만 역순으로 내린다 */
SW_TEST_CASE_F( OnlineServerRuntimeTest, KitInitializeFailureSkipsItsOwnShutdown )
{
    {
        OnlineServerRuntimeConfig settings     = makeSettings( { ProviderTestKit::kName, ConsumerTestKit::kName } );
        settings._config._listKit[1]._settings = R"({ "_failStage": "init" })";
        OnlineServerRuntime runtime;
        const auto          failed1 = runtime.initialize( settings, UnixMilliseconds{ 1000 } );
        SW_REQUIRE_FALSE( failed1.has_value() );
        StringBuilder<constant::buffer::kMax512> error1Builder;
        error1Builder.appendFormat( "%# %#", failed1.error().message(), failed1.error().detail() );
        const sw::string error1( error1Builder.view() );
        SW_EXPECT_TRUE( error1.find( "test.consumer" ) != string::npos && error1.find( "test init failure" ) != string::npos );
        runtime.shutdown();
    }
    const vector<string>& listLine = getKitEventLog();
    SW_REQUIRE_EQUAL( listLine.size(), size_t( 3 ) );
    SW_EXPECT_EQUAL( listLine[0], string( "test.provider:init" ) );
    SW_EXPECT_EQUAL( listLine[1], string( "test.provider:stop" ) );
    SW_EXPECT_EQUAL( listLine[2], string( "test.provider:shutdown" ) );
}

/** @brief [OnlineServerRuntimeTest] 빌드에 없는 키트 이름은 기동 오류이고 반쯤 만든 것도 내린다(두 번 불러도 된다) */
SW_TEST_CASE_F( OnlineServerRuntimeTest, UnknownKitStopsStartup )
{
    OnlineServerRuntime runtime;
    const auto          failed1 = runtime.initialize( makeSettings( { ProviderTestKit::kName, "test.missing" } ), UnixMilliseconds{ 1000 } );
    SW_REQUIRE_FALSE( failed1.has_value() );
    StringBuilder<constant::buffer::kMax512> error1Builder;
    error1Builder.appendFormat( "%# %#", failed1.error().message(), failed1.error().detail() );
    const sw::string error1( error1Builder.view() );
    SW_EXPECT_TRUE( error1.find( "test.missing" ) != string::npos && error1.find( "not in this build" ) != string::npos );
    runtime.shutdown();
    runtime.shutdown();
    SW_EXPECT_FALSE( runtime.isInitialized() );
}

/** @brief [OnlineServerRuntimeTest] 시작에 실패한 키트도 (초기화에 성공했으므로) stop · shutdown 을 받되 isStarted 는 false 이고, 먼저 선 키트는 반대 순서로 내린다 */
SW_TEST_CASE_F( OnlineServerRuntimeTest, KitStartFailureUnwindsEveryKit )
{
    {
        OnlineServerRuntimeConfig settings     = makeSettings( { ProviderTestKit::kName, ConsumerTestKit::kName } );
        settings._config._listKit[1]._settings = R"({ "_failStage": "start" })";
        settings._listenAddress                = NetAddress::makeLoopback( 7452 );
        OnlineServerRuntime runtime;
        const auto          failed1 = runtime.initialize( settings, UnixMilliseconds{ 1000 } );
        SW_REQUIRE_FALSE( failed1.has_value() );
        StringBuilder<constant::buffer::kMax512> error1Builder;
        error1Builder.appendFormat( "%# %#", failed1.error().message(), failed1.error().detail() );
        const sw::string error1( error1Builder.view() );
        SW_EXPECT_TRUE( error1.find( "test.consumer" ) != string::npos && error1.find( "test start failure" ) != string::npos );
        runtime.shutdown();
    }
    const vector<string>& listLine = getKitEventLog();
    SW_REQUIRE_EQUAL( listLine.size(), size_t( 7 ) );
    SW_EXPECT_EQUAL( listLine[2], string( "test.provider:start" ) );
    SW_EXPECT_EQUAL( listLine[3], string( "test.consumer:stop-unstarted" ) );
    SW_EXPECT_EQUAL( listLine[4], string( "test.provider:stop" ) );
    SW_EXPECT_EQUAL( listLine[5], string( "test.consumer:shutdown" ) );
    SW_EXPECT_EQUAL( listLine[6], string( "test.provider:shutdown" ) );
}

/** @brief [OnlineServerRuntimeTest] 단조 시계가 없으면 아무것도 만들지 않고 기동 오류다(폴백 시계는 없다) */
SW_TEST_CASE_F( OnlineServerRuntimeTest, MissingClockStopsStartup )
{
    OnlineServerRuntimeConfig settings = makeSettings( { ProviderTestKit::kName } );
    settings._pClock                   = nullptr;
    OnlineServerRuntime runtime;
    const auto          failed1 = runtime.initialize( settings, UnixMilliseconds{ 1000 } );
    SW_REQUIRE_FALSE( failed1.has_value() );
    StringBuilder<constant::buffer::kMax512> error1Builder;
    error1Builder.appendFormat( "%# %#", failed1.error().message(), failed1.error().detail() );
    const sw::string error1( error1Builder.view() );
    SW_EXPECT_TRUE( error1.find( "_pClock" ) != string::npos );
    SW_EXPECT_FALSE( runtime.isInitialized() );
    SW_EXPECT_TRUE( getKitEventLog().empty() );
}

/** @brief [OnlineServerRuntimeTest] 키트 목록이 비면 빌드의 키트를 모두 띄우지 않고 기동 오류다 */
SW_TEST_CASE_F( OnlineServerRuntimeTest, EmptyKitListStopsStartup )
{
    OnlineServerRuntime runtime;
    const auto          failed1 = runtime.initialize( makeSettings( {} ), UnixMilliseconds{ 1000 } );
    SW_REQUIRE_FALSE( failed1.has_value() );
    StringBuilder<constant::buffer::kMax512> error1Builder;
    error1Builder.appendFormat( "%# %#", failed1.error().message(), failed1.error().detail() );
    const sw::string error1( error1Builder.view() );
    SW_EXPECT_TRUE( error1.find( "lists no kits" ) != string::npos );
    runtime.shutdown();
    SW_EXPECT_TRUE( getKitEventLog().empty() );
}

/** @brief [OnlineServerRuntimeTest] 저장소 · 캐시 드라이버가 비면 메모리로 대신하지 않고 기동을 멈춘다(재시작에 데이터를 잃는 서버가 정상으로 뜨지 않게) */
SW_TEST_CASE_F( OnlineServerRuntimeTest, EmptyDriverStopsStartup )
{
    OnlineServerRuntimeConfig noStore = makeSettings( { ProviderTestKit::kName } );
    noStore._store._driver.clear();
    OnlineServerRuntime runtime;
    const auto          failed1 = runtime.initialize( std::move( noStore ), UnixMilliseconds{ 1000 } );
    SW_REQUIRE_FALSE( failed1.has_value() );
    StringBuilder<constant::buffer::kMax512> error1Builder;
    error1Builder.appendFormat( "%# %#", failed1.error().message(), failed1.error().detail() );
    const sw::string error1( error1Builder.view() );
    SW_EXPECT_TRUE( error1.find( "store and a cache driver" ) != string::npos );
    runtime.shutdown();

    OnlineServerRuntimeConfig noCache = makeSettings( { ProviderTestKit::kName } );
    noCache._cache._driver.clear();
    OnlineServerRuntime runtimeNoCache;
    SW_EXPECT_FALSE( runtimeNoCache.initialize( std::move( noCache ), UnixMilliseconds{ 1000 } ) );
}

/** @brief [OnlineServerRuntimeTest] 온라인 설정이 이름으로 고른 항목만 쓴다 - 이름이 비었거나 목록에 없으면 첫 항목으로 대신하지 않고 오류이다 */
SW_TEST_CASE_F( OnlineServerRuntimeTest, ResolveStoresNeedsNamedEntries )
{
    ServerConfig serverConfig;
    serverConfig._listStore.push_back( ServerStoreEntry{} );
    serverConfig._listStore[0]._name   = "first";
    serverConfig._listStore[0]._driver = "sqlite";
    serverConfig._listStore.push_back( ServerStoreEntry{} );
    serverConfig._listStore[1]._name   = "local";
    serverConfig._listStore[1]._driver = kMemoryStoreDriverName;
    serverConfig._listCache.push_back( ServerCacheEntry{} );
    serverConfig._listCache[0]._name   = "local";
    serverConfig._listCache[0]._driver = kMemoryStoreDriverName;

    OnlineServerConfig        onlineConfig;
    OnlineServerRuntimeConfig settings;
    const auto                failed1 = OnlineServerRuntime::resolveStores( serverConfig, onlineConfig, settings ); // 이름을 적지 않았다
    SW_REQUIRE_FALSE( failed1.has_value() );
    StringBuilder<constant::buffer::kMax512> error1Builder;
    error1Builder.appendFormat( "%# %#", failed1.error().message(), failed1.error().detail() );
    const sw::string error1( error1Builder.view() );
    SW_EXPECT_TRUE( error1.find( "_storeName" ) != string::npos );

    onlineConfig._storeName = "missing";
    onlineConfig._cacheName = "local";
    const auto failed2      = OnlineServerRuntime::resolveStores( serverConfig, onlineConfig, settings );
    SW_REQUIRE_FALSE( failed2.has_value() );
    StringBuilder<constant::buffer::kMax512> error2Builder;
    error2Builder.appendFormat( "%# %#", failed2.error().message(), failed2.error().detail() );
    const sw::string error2( error2Builder.view() );
    SW_EXPECT_TRUE( error2.find( "missing" ) != string::npos );

    onlineConfig._storeName = "local";
    onlineConfig._cacheName = "";
    const auto failed3      = OnlineServerRuntime::resolveStores( serverConfig, onlineConfig, settings );
    SW_REQUIRE_FALSE( failed3.has_value() );
    StringBuilder<constant::buffer::kMax512> error3Builder;
    error3Builder.appendFormat( "%# %#", failed3.error().message(), failed3.error().detail() );
    const sw::string error3( error3Builder.view() );
    SW_EXPECT_TRUE( error3.find( "_cacheName" ) != string::npos );

    onlineConfig._cacheName = "local";
    SW_REQUIRE_TRUE( OnlineServerRuntime::resolveStores( serverConfig, onlineConfig, settings ) );
    SW_EXPECT_EQUAL( string( kMemoryStoreDriverName ), settings._store._driver );
    SW_EXPECT_EQUAL( string( kMemoryStoreDriverName ), settings._cache._driver );
}

/** @brief [OnlineServerRuntimeTest] 게임 서버(공개 주소가 있다)는 기동 안에서 디렉터리에 자기 기록을 쓴다 — 첫 틱에 서버 목록을 읽는 키트(매칭)가 이 서버를 본다.
 *         첫 틱의 키트 뒤에서 썼을 때는 첫 읽기가 빈 목록을 받아 읽기 주기(벽시계 2 초)만큼 배정이 늦었다(shooter3d.onlinematch 가 빠른 무인 실행에서 졌다). */
SW_TEST_CASE_F( OnlineServerRuntimeTest, GameServerIsInTheDirectoryBeforeTheFirstTick )
{
    OnlineServerRuntimeConfig settings = makeSettings( { ConsumerTestKit::kName, ProviderTestKit::kName } );
    settings._config._publicAddress    = "127.0.0.1";
    OnlineServerRuntime runtime;
    SW_REQUIRE_TRUE( runtime.initialize( settings, UnixMilliseconds{ 1000 } ).has_value() );
    EphemeralStoreRouter& router = *runtime.getContext()._pRouter;
    const Subscription    owner  = router.openReplyOwner();
    DirectoryIndexProbe   probe;
    (void)router.submit( EphemeralRequest::makeScoreRange( ServerRecord::makeIndexKey( settings._config._serverKind ), 0, 16 ),
                         SW_DELEGATE_METHOD( EphemeralStoreRouter::ReplyDelegate, &DirectoryIndexProbe::onReply, &probe ), owner );
    for ( int64 tick = 0; tick < 10 && probe._replyCount == 0; ++tick )
    {
        runtime.update( UnixMilliseconds{ 1000 + tick } );
    }
    SW_REQUIRE_EQUAL( probe._replyCount, int32( 1 ) );
    SW_EXPECT_EQUAL( probe._memberCount, int32( 1 ) );
    runtime.shutdown();
}
