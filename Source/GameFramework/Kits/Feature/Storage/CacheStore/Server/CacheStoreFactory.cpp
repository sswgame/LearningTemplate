#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/CacheStore/Server/CacheStoreFactory.h"

#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"
#include "GameFramework/Kits/Feature/Storage/CacheStore/Server/Driver/Resp/RespEphemeralStore.h"

namespace sw
{
    namespace
    {
        struct CacheStoreFactoryInternal
        {
            /** @brief "memory" 캐시의 데이터 — 같은 프로세스의 앞들이 나눠 쓴다. */
            static MemoryEphemeralDatabase& getMemoryDatabase()
            {
                static MemoryEphemeralDatabase s_database;
                return s_database;
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
                    const bool bOn  = value == "1";
                    const bool bOff = value == "0";
                    if ( bOn == false && bOff == false )
                    {
                        outError = "cache endpoint: tls must be 0 or 1";
                        return false;
                    }
                    outEndpoint._bTls = bOn ? SW_TRUE : SW_FALSE;
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
        if ( driverName == kMemoryDriverName )
            return make_unique<MemoryEphemeralStore>( &CacheStoreFactoryInternal::getMemoryDatabase() );
        if ( driverName != kRespDriverName )
        {
            outError = "cache driver '" + string( driverName ) + "' is not in this build (memory, resp)";
            return nullptr;
        }
        CacheEndpoint parsed;
        if ( parseEndpoint( endpoint, parsed, outError ) == false )
            return nullptr;
        unique_ptr<ITlsContext> tlsContext;
        if ( parsed._bTls == SW_TRUE )
        {
            tlsContext = NetSecurity::createClientTlsContext( parsed._trustFile, parsed._host, outError );
            if ( tlsContext == nullptr )
                return nullptr;
        }
        RespStoreSettings settings;
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
        StreamTransportSettings transportSettings;
        transportSettings._ioThreadCount      = 1;
        transportSettings._maxConnections     = 4;
        transportSettings._idleTimeoutSeconds = 0.0; // 캐시 연결은 조용해도 산다(구독 연결)
        unique_ptr<RespEphemeralStore> store  = make_unique<RespEphemeralStore>();
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
        const string_view hostText = outEndpoint._host == "localhost" ? string_view{ "127.0.0.1" } : string_view{ outEndpoint._host };
        string            addressText( hostText );
        if ( colon != string_view::npos )
            addressText.append( address.data() + colon, address.size() - colon );
        if ( NetAddress::parse( addressText, kDefaultRespPort, outEndpoint._address ) == false )
        {
            outError = "cache endpoint '" + string( address ) + "' is not an IPv4 host:port (or localhost)";
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
            const size_t equals         = argument.find( '=' );
            if ( equals == string_view::npos )
            {
                outError = "cache endpoint: argument '" + string( argument ) + "' has no value";
                return false;
            }
            if ( CacheStoreFactoryInternal::applyQueryArgument( argument.substr( 0, equals ), argument.substr( equals + 1 ), outEndpoint, outError ) == false )
                return false;
        }
        return true;
    }
} // namespace sw
