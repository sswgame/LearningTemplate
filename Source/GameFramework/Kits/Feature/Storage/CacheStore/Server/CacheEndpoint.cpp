#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/CacheStore/Server/CacheEndpoint.h"

#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Network/Transport/PlatformSocketUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Time/Clock.h"

#include "Engine/Module/ModuleSubsystemRegistrar.h"

#include "GameFramework/Kits/Feature/Storage/CacheStore/Server/Driver/RESP/RESPEphemeralStore.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Online/Cache/Server/MemoryEphemeralStore.h"
#include "GameFramework/Runtime/Online/Security/NetSecurity.h"
#include "GameFramework/Runtime/Online/Server/IOnlineStoreDriver.h"

namespace sw
{
    namespace
    {
        struct CacheStoreFactoryInternal
        {
            /** @brief RESP 저장소에 주는 운영 시계입니다. 상태가 없어 저장소들이 나눠 쓴다. */
            static const SystemMonotonicClock& getRESPClock()
            {
                static const SystemMonotonicClock s_clock{};
                return s_clock;
            }

            [[nodiscard]] static expected<void> applyQueryArgument( string_view name, string_view value, CacheEndpoint& outEndpoint )
            {
                if ( name == "prefix" )
                {
                    outEndpoint._keyPrefix = string( value );
                    return {};
                }
                if ( name == "ca" )
                {
                    outEndpoint._trustFile = string( value );
                    return {};
                }
                if ( name == "tls" )
                {
                    bool bOn = false;
                    if ( StringUtil::tryParseBool( value, bOn ) == false )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "cache endpoint: tls must be a boolean (0, 1, true or false)" } );
                    outEndpoint._bTLS = bOn ? SW_TRUE : SW_FALSE;
                    return {};
                }
                if ( name == "timeoutMs" )
                {
                    int64 timeoutMs = 0;
                    if ( StringUtil::parseInt64( value, timeoutMs ) == false || timeoutMs <= 0 )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "cache endpoint: timeoutMs must be a positive integer" } );
                    outEndpoint._timeoutMs = timeoutMs;
                    return {};
                }
                return unexpected( Error{ ErrorKind::InvalidArgument, "cache endpoint: unknown argument", name } );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    /**
     * @class RESPEphemeralStoreDriver
     * @brief 캐시 키트의 캐시 드라이버입니다 — 이 키트의 모듈 서브시스템(`SW_MODULE_SUBSYSTEM( Engine, … )`)이고 `IEphemeralStoreDriver` 로 공개된다.
     * @details "memory" 캐시의 데이터(같은 프로세스의 저장소끼리 나눠 쓴다)와 그 시계를 값으로 든다 — 예전에는 소유자 없는 엔진 수명 값(`ScopeOwned`)이었다. 저장소는 이 데이터를
     *          주소로 들므로 키트가 내려가기(자식 범위가 걷히기) 전에 소멸해야 한다(서버 조립이 그렇게 한다 — 저장소 코드도 이 키트에 있다).
     *          `IEphemeralStoreDriver` 는 상속하지 않는다 — 서브시스템이 가진 어댑터 멤버가 구현하고, 등록이 그 멤버를 공개한다(`getPublishedPart`).
     */
    class RESPEphemeralStoreDriver final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "RespEphemeralStoreDriver";

        RESPEphemeralStoreDriver()
            : _memoryClock{}
            , _memoryDatabase{ _memoryClock }
            , _driver{ *this }
        {
        }

        // ISubsystem — 세울 것이 없다.
        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /**
         * @brief 저장소를 만들고 초기화합니다. 실패하면 그 이유입니다(대상에 비밀을 담지 않는다).
         * @param secret AUTH 비밀(`ServerSecret::read` 로 읽은 것)입니다. 쓰고 나면 호출한 쪽이 비웁니다.
         */
        expected<unique_ptr<IEphemeralStore>> createEphemeralStore( string_view driverName, string_view endpoint, string_view secret );

        /** @brief 공개할 캐시 드라이버입니다(`ModuleSubsystemRegistration` 이 `IEphemeralStoreDriver` 로 공개한다). 이 서브시스템과 수명이 같습니다. */
        IEphemeralStoreDriver& getPublishedPart() { return _driver; }

    private:
        /** @brief 캐시 드라이버 능력입니다. 서브시스템의 같은 이름 함수로 넘깁니다(수명은 서브시스템과 같다). */
        class Driver final : public IEphemeralStoreDriver
        {
        public:
            explicit Driver( RESPEphemeralStoreDriver& owner )
                : _owner{ owner }
            {
            }

            expected<unique_ptr<IEphemeralStore>> createEphemeralStore( string_view driverName, string_view endpoint, string_view secret ) override
            {
                return _owner.createEphemeralStore( driverName, endpoint, secret );
            }

        private:
            RESPEphemeralStoreDriver& _owner;
        };

        SystemMonotonicClock    _memoryClock;    ///< "memory" 캐시의 시계 — 데이터보다 먼저 만들어지고 나중에 사라진다(선언 순서)
        MemoryEphemeralDatabase _memoryDatabase; ///< "memory" 캐시의 데이터 — 같은 프로세스의 저장소끼리 나눠 쓴다
        Driver                  _driver;         ///< `IEphemeralStoreDriver` 로 공개되는 부분
    };

    SW_MODULE_SUBSYSTEM( Engine, RESPEphemeralStoreDriver, IEphemeralStoreDriver );

    expected<unique_ptr<IEphemeralStore>> RESPEphemeralStoreDriver::createEphemeralStore( string_view driverName, string_view endpoint, string_view secret )
    {
        if ( driverName == kMemoryStoreDriverName )
            return unique_ptr<IEphemeralStore>( make_unique<MemoryEphemeralStore>( &_memoryDatabase ) );
        if ( driverName != CacheEndpoint::kRESPDriverName )
            return unexpected( Error{ ErrorKind::Unsupported, "the cache driver is not in this build (memory, resp)", driverName } );
        CacheEndpoint parsed;
        if ( expected<void> endpointRead = CacheEndpoint::parseEndpoint( endpoint, parsed ); endpointRead.has_value() == false )
            return unexpected( endpointRead.error() );
        unique_ptr<ITLSContext> tlsContext;
        if ( parsed._bTLS == SW_TRUE )
        {
            expected<unique_ptr<ITLSContext>> created = NetSecurity::createClientTLSContext( game::getSubsystem<INetSecurityProvider>(), parsed._trustFile, parsed._host );
            if ( created.has_value() == false )
                return unexpected( created.error() );
            tlsContext = std::move( *created );
        }
        RESPStoreConfig settings;
        settings._address   = parsed._address;
        settings._keyPrefix = parsed._keyPrefix;
        settings._timeoutMs = parsed._timeoutMs;
        const size_t colon  = secret.find( ':' );
        if ( colon == string_view::npos )
        {
            settings._password = string( secret );
        }
        else
        {
            settings._userName = string( secret.substr( 0, colon ) );
            settings._password = string( secret.substr( colon + 1 ) );
        }
        unique_ptr<IStreamTransport> transport = StreamTransportFactory::createPlatformTransport();
        if ( transport == nullptr )
            return unexpected( Error{ ErrorKind::Unsupported, "cache driver resp: this platform has no stream transport" } );
        StreamTransportConfig transportSettings;
        transportSettings._ioThreadCount      = 1;
        transportSettings._maxConnections     = 4;
        transportSettings._idleTimeoutSeconds = 0.0; // 캐시 연결은 조용해도 유지한다. 구독 연결이 그렇다
        unique_ptr<RESPEphemeralStore> store  = make_unique<RESPEphemeralStore>( CacheStoreFactoryInternal::getRESPClock() );
        if ( expected<void> initialized = store->initialize( std::move( transport ), transportSettings, settings, std::move( tlsContext ) ); initialized.has_value() == false )
            return unexpected( initialized.error() );
        return unique_ptr<IEphemeralStore>( std::move( store ) );
    }

    expected<void> CacheEndpoint::parseEndpoint( string_view endpoint, CacheEndpoint& outEndpoint )
    {
        outEndpoint               = CacheEndpoint{};
        const size_t      query   = endpoint.find( '?' );
        const string_view address = endpoint.substr( 0, query );
        const size_t      colon   = address.rfind( ':' );
        outEndpoint._host         = string( address.substr( 0, colon ) );
        if ( outEndpoint._host.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "cache endpoint needs host:port" } );
        if ( PlatformSocketUtil::resolveAddress( address, kDefaultRESPPort, outEndpoint._address ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the cache endpoint is not a host:port that resolves to IPv4", address } );
        if ( query == string_view::npos )
            return {};
        string_view rest = endpoint.substr( query + 1 );
        while ( rest.empty() == false )
        {
            const size_t      ampersand = rest.find( '&' );
            const string_view argument  = rest.substr( 0, ampersand );
            rest                        = ampersand == string_view::npos ? string_view{} : rest.substr( ampersand + 1 );
            string_view key;
            string_view value;
            if ( StringUtil::trySplitKeyValue( argument, key, value ) == false )
                return unexpected( Error{ ErrorKind::InvalidArgument, "cache endpoint: an argument is not key=value", argument } );
            if ( expected<void> result = CacheStoreFactoryInternal::applyQueryArgument( key, value, outEndpoint ); result.has_value() == false )
                return result;
        }
        return {};
    }
} // namespace sw
