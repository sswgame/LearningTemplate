#include "pch.h"

#include "GameFramework/Runtime/Online/HTTP/OwnedHTTPClient.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPTypes.h"
#include "GameFramework/Runtime/Online/Security/NetSecurity.h"

namespace sw
{
    OwnedHTTPClient::OwnedHTTPClient( const IClock& clock )
        : _listTLSContext{}
        , _listHost{}
        , _client{ clock }
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

    expected<void> OwnedHTTPClient::registerHostTLS( string_view url, string_view trustFile, string_view trustOption )
    {
        if ( url.empty() )
            return {};
        HTTPAddress address;
        if ( HTTPAddress::parse( url, address ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "malformed URL", url } );
        if ( address._bSecure == SW_FALSE || VectorUtil::contains( _listHost, address._host ) )
            return {};
        if ( trustFile.empty() )
        {
            Error error{ ErrorKind::InvalidArgument, "an https URL needs a CA bundle PEM that verifies the provider", trustOption };
            error.appendDetail( " for " ).appendDetail( url );
            return unexpected( error );
        }
        expected<unique_ptr<ITLSContext>> context = NetSecurity::createClientTLSContext( game::getSubsystem<INetSecurityProvider>(), trustFile, address._host );
        if ( context.has_value() == false )
            return unexpected( context.error() );
        _client.registerTLSContext( address._host, context->get() );
        _listTLSContext.push_back( std::move( *context ) );
        _listHost.push_back( address._host );
        return {};
    }
} // namespace sw
