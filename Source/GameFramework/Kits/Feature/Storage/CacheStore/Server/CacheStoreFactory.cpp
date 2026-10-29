#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/CacheStore/Server/CacheStoreFactory.h"

#include "Core/Container/StringUtil.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Network/Transport/PlatformSocketUtil.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Time/Clock.h"

#include "Engine/Module/ModuleSubsystemRegistrar.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"
#include "GameFramework/Base/Online/Server/IOnlineStoreDriver.h"
#include "GameFramework/Kits/Feature/Storage/CacheStore/Server/Driver/Resp/RespEphemeralStore.h"

namespace sw
{
    namespace
    {
        struct CacheStoreFactoryInternal
        {
            /** @brief RESP 저장소에 주는 운영 시계입니다. 상태가 없어 저장소들이 나눠 쓴다. */
            static const SystemMonotonicClock& getRespClock()
            {
                static const SystemMonotonicClock s_clock{};
                return s_clock;
            }

            [[nodiscard]] static bool applyQueryArgument( string_view name, string_view value, CacheEndpoint& outEndpoint, string& outError )
            {
                if ( name == "prefix" )
                {
                    outEndpoint._keyPrefix = string( value );
                    return true;
                }
                if ( name == "ca" )
                {
                    outEndpoint._trustFile = string( value );
                    return true;
                }
                if ( name == "tls" )
                {
                    bool bOn = false;
                    if ( StringUtil::tryParseBool( value, bOn ) == false )
                    {
                        outError = "cache endpoint: tls must be a boolean (0, 1, true or false)";
                        return false;
                    }
                    outEndpoint._bTLS = bOn ? SW_TRUE : SW_FALSE;
                    return true;
                }
                if ( name == "timeoutMs" )
                {
                    int64 timeoutMs = 0;
                    if ( StringUtil::parseInt64( value, timeoutMs ) == false || timeoutMs <= 0 )
                    {
                        outError = "cache endpoint: timeoutMs must be a positive integer";
                        return false;
                    }
                    outEndpoint._timeoutMs = timeoutMs;
                    return true;
                }
                outError = "cache endpoint: unknown argument '" + string( name ) + "'";
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    /**
     * @class RespEphemeralStoreDriver
     * @brief 캐시 키트의 캐시 드라이버입니다 — 이 키트의 모듈 서브시스템(`SW_MODULE_SUBSYSTEM( Engine, … )`)이고 `IEphemeralStoreDriver` 로 공개된다.
     * @details "memory" 캐시의 데이터(같은 프로세스의 저장소끼리 나눠 쓴다)와 그 시계를 값으로 든다 — 예전에는 소유자 없는 엔진 수명 값(`ScopeOwned`)이었다. 저장소는 이 데이터를
     *          주소로 들므로 키트가 내려가기(자식 범위가 걷히기) 전에 소멸해야 한다(서버 조립이 그렇게 한다 — 저장소 코드도 이 키트에 있다).
     */
    class RespEphemeralStoreDriver final : public ISubsystem, public IEphemeralStoreDriver
    {
    public:
        static constexpr const utf8* kSubsystemName = "RespEphemeralStoreDriver";

        RespEphemeralStoreDriver()
            : _memoryClock{}
            , _memoryDatabase{ _memoryClock }
        {
        }

        // ISubsystem — 세울 것이 없다.
        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /**
         * @brief 저장소를 만들고 초기화합니다. 실패하면 nullptr 과 실패 이유를 반환합니다.
         * @param secret AUTH 비밀(`ServerSecret::read` 로 읽은 것)입니다. 쓰고 나면 호출한 쪽이 비웁니다.
         */
        unique_ptr<IEphemeralStore> createEphemeralStore( string_view driverName, string_view endpoint, string_view secret, string& outError ) override;

    private:
        SystemMonotonicClock    _memoryClock;    ///< "memory" 캐시의 시계 — 데이터보다 먼저 만들어지고 나중에 사라진다(선언 순서)
        MemoryEphemeralDatabase _memoryDatabase; ///< "memory" 캐시의 데이터 — 같은 프로세스의 저장소끼리 나눠 쓴다
    };

    SW_MODULE_SUBSYSTEM( Engine, RespEphemeralStoreDriver, IEphemeralStoreDriver );

    unique_ptr<IEphemeralStore> RespEphemeralStoreDriver::createEphemeralStore( string_view driverName, string_view endpoint, string_view secret, string& outError )
    {
        if ( driverName == kMemoryStoreDriverName )
            return make_unique<MemoryEphemeralStore>( &_memoryDatabase );
        if ( driverName != CacheStoreFactory::kRespDriverName )
        {
            outError = "cache driver '" + string( driverName ) + "' is not in this build (memory, resp)";
            return nullptr;
        }
        CacheEndpoint parsed;
        if ( CacheStoreFactory::parseEndpoint( endpoint, parsed, outError ) == false )
            return nullptr;
        unique_ptr<ITLSContext> tlsContext;
        if ( parsed._bTLS == SW_TRUE )
        {
            tlsContext = NetSecurity::createClientTLSContext( game::getSubsystem<INetSecurityProvider>(), parsed._trustFile, parsed._host, outError );
            if ( tlsContext == nullptr )
                return nullptr;
        }
        RespStoreConfig settings;
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
        {
            outError = "cache driver resp: this platform has no stream transport";
            return nullptr;
        }
        StreamTransportConfig transportSettings;
        transportSettings._ioThreadCount      = 1;
        transportSettings._maxConnections     = 4;
        transportSettings._idleTimeoutSeconds = 0.0; // 캐시 연결은 조용해도 유지한다. 구독 연결이 그렇다
        unique_ptr<RespEphemeralStore> store  = make_unique<RespEphemeralStore>( CacheStoreFactoryInternal::getRespClock() );
        if ( store->initialize( std::move( transport ), transportSettings, settings, std::move( tlsContext ), outError ) == false )
            return nullptr;
        return store;
    }

    bool CacheStoreFactory::parseEndpoint( string_view endpoint, CacheEndpoint& outEndpoint, string& outError )
    {
        outEndpoint               = CacheEndpoint{};
        const size_t      query   = endpoint.find( '?' );
        const string_view address = endpoint.substr( 0, query );
        const size_t      colon   = address.rfind( ':' );
        outEndpoint._host         = string( address.substr( 0, colon ) );
        if ( outEndpoint._host.empty() )
        {
            outError = "cache endpoint needs host:port";
            return false;
        }
        if ( PlatformSocketUtil::resolveAddress( address, kDefaultRespPort, outEndpoint._address ) == false )
        {
            outError = "cache endpoint '" + string( address ) + "' is not a host:port that resolves to IPv4";
            return false;
        }
        if ( query == string_view::npos )
            return true;
        string_view rest = endpoint.substr( query + 1 );
        while ( rest.empty() == false )
        {
            const size_t      ampersand = rest.find( '&' );
            const string_view argument  = rest.substr( 0, ampersand );
            rest                        = ampersand == string_view::npos ? string_view{} : rest.substr( ampersand + 1 );
            string_view key;
            string_view value;
            if ( StringUtil::trySplitKeyValue( argument, key, value ) == false )
            {
                outError = "cache endpoint: argument '" + string( argument ) + "' is not key=value";
                return false;
            }
            if ( CacheStoreFactoryInternal::applyQueryArgument( key, value, outEndpoint, outError ) == false )
                return false;
        }
        return true;
    }
} // namespace sw
