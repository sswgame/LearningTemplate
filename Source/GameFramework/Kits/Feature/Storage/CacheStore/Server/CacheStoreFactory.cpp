#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/CacheStore/Server/CacheStoreFactory.h"

#include "Core/Container/StringUtil.h"
#include "Core/Memory/LifetimeScope.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Network/Transport/PlatformSocketUtil.h"

#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"
#include "GameFramework/Base/Online/Server/OnlineStoreDriverTable.h"
#include "GameFramework/Kits/Feature/Storage/CacheStore/Server/Driver/Resp/RespEphemeralStore.h"

namespace sw
{
    namespace
    {
        struct CacheStoreFactoryInternal
        {
            /**
             * @brief "memory" 캐시의 데이터의 소유자입니다 — 엔진 수명 범위가 소유해 엔진 종료 때 사라진다(처음 만든 "memory" 저장소가 만든다).
             * @details 같은 프로세스의 저장소끼리 나눠 쓴다. 저장소는 이 데이터를 주소로 들므로 엔진이 내려가기 전에 소멸해야 한다(서버 조립이 그렇게 한다).
             */
            static inline ScopeOwned<MemoryEphemeralDatabase> s_memoryDatabase{};

            static MemoryEphemeralDatabase& getMemoryDatabase()
            {
                return s_memoryDatabase.getRequired(); // 엔진 수명 범위가 닫힌 뒤의 접근은 결함이다 — "memory" 캐시를 쓸 엔진이 없다
            }

            /** @brief RESP 저장소에 주는 운영 시계입니다. 상태가 없어 저장소들이 나눠 쓴다. */
            static const RespMonotonicClock& getRespClock()
            {
                static const RespMonotonicClock s_clock{};
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
    unique_ptr<IEphemeralStore> CacheStoreFactory::createEphemeralStore( string_view driverName, string_view endpoint, string_view secret, string& outError )
    {
        if ( driverName == OnlineStoreDriverTable::kMemoryDriverName )
            return make_unique<MemoryEphemeralStore>( &CacheStoreFactoryInternal::getMemoryDatabase() );
        if ( driverName != kRespDriverName )
        {
            outError = "cache driver '" + string( driverName ) + "' is not in this build (memory, resp)";
            return nullptr;
        }
        CacheEndpoint parsed;
        if ( parseEndpoint( endpoint, parsed, outError ) == false )
            return nullptr;
        unique_ptr<ITLSContext> tlsContext;
        if ( parsed._bTLS == SW_TRUE )
        {
            tlsContext = NetSecurity::createClientTLSContext( parsed._trustFile, parsed._host, outError );
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

namespace sw
{
    // 모듈이 로드되면 서버 조립의 캐시 만들기를 이 팩토리에 묶고, 언로드되면 푼다.
    const OnlineStoreDriverBinding s_ephemeralStoreDriverBinding{ &CacheStoreFactory::createEphemeralStore, nullptr };
} // namespace sw
