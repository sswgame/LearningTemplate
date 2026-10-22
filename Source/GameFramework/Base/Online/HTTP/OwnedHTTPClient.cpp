#include "pch.h"

#include "GameFramework/Base/Online/HTTP/OwnedHTTPClient.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"

#include "GameFramework/Base/Online/HTTP/HTTPTypes.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"

namespace sw
{
    OwnedHTTPClient::OwnedHTTPClient()
        : _listTLSContext{}
        , _listHost{}
        , _client{}
    {
    }

    OwnedHTTPClient::~OwnedHTTPClient()
    {
        shutdown();
    }

    bool OwnedHTTPClient::initialize()
    {
        return _client.initialize( StreamTransportFactory::createPlatformTransport(), StreamTransportConfig{}, HTTPClientConfig{} );
    }

    void OwnedHTTPClient::shutdown()
    {
        _client.shutdown(); // 날아가던 요청을 끝내고 TLS 컨텍스트 빌림을 놓는다
        _listTLSContext.clear();
        _listHost.clear();
    }

    bool OwnedHTTPClient::registerHostTLS( string_view url, string_view trustFile, string& outError )
    {
        if ( url.empty() )
            return true;
        HTTPAddress address;
        if ( HTTPAddress::parse( url, address ) == false )
        {
            outError = "malformed URL '" + string( url ) + "'";
            return false;
        }
        if ( address._bSecure == SW_FALSE || VectorUtil::contains( _listHost, address._host ) )
            return true;
        if ( trustFile.empty() )
        {
            outError = "https URL '" + string( url ) + "' needs a trust file (a CA bundle PEM that verifies the provider)";
            return false;
        }
        unique_ptr<ITLSContext> context = NetSecurity::createClientTLSContext( trustFile, address._host, outError );
        if ( context == nullptr )
            return false;
        _client.registerTLSContext( address._host, context.get() );
        _listTLSContext.push_back( std::move( context ) );
        _listHost.push_back( address._host );
        return true;
    }
} // namespace sw
