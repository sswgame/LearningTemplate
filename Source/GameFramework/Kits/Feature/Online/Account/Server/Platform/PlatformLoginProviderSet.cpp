#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProviderSet.h"

#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"

#include "GameFramework/Base/Online/HTTP/HTTPClient.h"
#include "GameFramework/Base/Online/HTTP/HTTPTypes.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"

namespace sw
{
    PlatformLoginProviderSet::PlatformLoginProviderSet()
        : _listEntry{}
    {
    }

    PlatformLoginProviderSet::~PlatformLoginProviderSet()
    {
        shutdown();
    }

    bool PlatformLoginProviderSet::initialize( const vector<PlatformLoginProviderConfig>& listSettings, string_view trustFile, string& outError )
    {
        shutdown();
        for ( const PlatformLoginProviderConfig& settings : listSettings )
        {
            Entry& entry      = _listEntry.emplace_back();
            entry._httpClient = make_unique<HTTPClient>();
            if ( entry._httpClient->initialize( StreamTransportFactory::createPlatformTransport(), StreamTransportConfig{}, HTTPClientConfig{} ) == false )
            {
                outError = "cannot start the HTTP client of platform login provider '" + settings._name + "'";
                shutdown();
                return false;
            }
            vector<string> listHost;
            const bool     bTLSReady = registerHostTLS( entry, settings._jwksURL, trustFile, listHost, outError ) &&
                                   registerHostTLS( entry, settings._profileURL, trustFile, listHost, outError );
            if ( bTLSReady == false )
            {
                outError = "platform login provider '" + settings._name + "': " + outError;
                shutdown();
                return false;
            }
            entry._provider = PlatformLoginProviderFactory::create( settings, &NetSecurity::getProvider(), entry._httpClient.get() );
            if ( entry._provider == nullptr )
            {
                outError = "platform login provider '" + settings._name + "' has invalid settings";
                shutdown();
                return false;
            }
        }
        return true;
    }

    void PlatformLoginProviderSet::shutdown()
    {
        for ( Entry& entry : _listEntry )
        {
            if ( entry._httpClient != nullptr )
                entry._httpClient->shutdown(); // 날아가던 요청을 끝내고 TLS 컨텍스트를 놓는다
        }
        _listEntry.clear();
    }

    bool PlatformLoginProviderSet::registerHostTLS( Entry& entry, string_view url, string_view trustFile, vector<string>& inoutListHost, string& outError )
    {
        if ( url.empty() )
            return true;
        HTTPAddress address;
        if ( HTTPAddress::parse( url, address ) == false )
        {
            outError = "malformed URL '" + string( url ) + "'";
            return false;
        }
        if ( address._bSecure == SW_FALSE )
            return true;
        for ( const string& host : inoutListHost )
        {
            if ( host == address._host )
                return true;
        }
        if ( trustFile.empty() )
        {
            outError = "https URL '" + string( url ) + "' needs platformTrustFile (a CA bundle PEM that verifies the provider)";
            return false;
        }
        unique_ptr<ITLSContext> context = NetSecurity::createClientTLSContext( trustFile, address._host, outError );
        if ( context == nullptr )
            return false;
        entry._httpClient->registerTLSContext( address._host, context.get() );
        entry._listTLSContext.push_back( std::move( context ) );
        inoutListHost.push_back( address._host );
        return true;
    }
} // namespace sw
