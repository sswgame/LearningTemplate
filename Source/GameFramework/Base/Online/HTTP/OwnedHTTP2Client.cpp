#include "pch.h"

#include "GameFramework/Base/Online/HTTP/OwnedHTTP2Client.h"

#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"

#include "GameFramework/Base/Online/HTTP/HTTPTypes.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"

namespace sw
{
    OwnedHTTP2Client::OwnedHTTP2Client( const IClock& clock )
        : _listTLSContext{}
        , _uniqueSetHost{}
        , _client{ clock }
    {
    }

    OwnedHTTP2Client::~OwnedHTTP2Client()
    {
        shutdown();
    }

    bool OwnedHTTP2Client::initialize()
    {
        return _client.initialize( StreamTransportFactory::createPlatformTransport(), StreamTransportConfig{}, HTTP2ClientConfig{} );
    }

    void OwnedHTTP2Client::shutdown()
    {
        _client.shutdown(); // 날아가던 요청을 끝내고 TLS 컨텍스트 빌림을 놓는다
        _listTLSContext.clear();
        _uniqueSetHost.clear();
    }

    bool OwnedHTTP2Client::registerHostTLS( string_view url, string_view trustFile, string_view trustOption, string& outError )
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
        {
            outError = "HTTP/2 needs an https URL, got '" + string( url ) + "'";
            return false;
        }
        if ( _uniqueSetHost.find( address._host ) != _uniqueSetHost.end() )
            return true;
        if ( trustFile.empty() )
        {
            outError = "https URL '" + string( url ) + "' needs " + string( trustOption ) + " (a CA bundle PEM that verifies the provider)";
            return false;
        }
        unique_ptr<ITLSContext> context = NetSecurity::createClientTLSContext( trustFile, address._host, outError, HTTP2Client::kAlpnProtocol );
        if ( context == nullptr )
            return false;
        _client.registerTLSContext( address._host, context.get() );
        _listTLSContext.push_back( std::move( context ) );
        _uniqueSetHost.insert( address._host );
        return true;
    }
} // namespace sw
