#include "pch.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/HTTP/OwnedHTTP2Client.h"

#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPTypes.h"
#include "GameFramework/Runtime/Online/Security/NetSecurity.h"

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
        HTTPClientConfig settings;
        settings._maxConcurrentRequests = HTTPConstant::kHTTP2MaxConcurrentRequests;
        return _client.initialize( StreamTransportFactory::createPlatformTransport(), StreamTransportConfig{}, settings );
    }

    void OwnedHTTP2Client::shutdown()
    {
        _client.shutdown(); // 날아가던 요청을 끝내고 TLS 컨텍스트 빌림을 놓는다
        _listTLSContext.clear();
        _uniqueSetHost.clear();
    }

    expected<void> OwnedHTTP2Client::registerHostTLS( string_view url, string_view trustFile, string_view trustOption )
    {
        if ( url.empty() )
            return {};
        HTTPAddress address;
        if ( HTTPAddress::parse( url, address ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "malformed URL", url } );
        if ( address._bSecure == SW_FALSE )
            return unexpected( Error{ ErrorKind::InvalidArgument, "HTTP/2 needs an https URL", url } );
        if ( _uniqueSetHost.find( address._host ) != _uniqueSetHost.end() )
            return {};
        if ( trustFile.empty() )
        {
            Error error{ ErrorKind::InvalidArgument, "an https URL needs a CA bundle PEM that verifies the provider", trustOption };
            error.appendDetail( " for " ).appendDetail( url );
            return unexpected( error );
        }
        expected<unique_ptr<ITLSContext>> context =
            NetSecurity::createClientTLSContext( game::getSubsystem<INetSecurityProvider>(), trustFile, address._host, HTTP2Client::kALPNProtocol );
        if ( context.has_value() == false )
            return unexpected( context.error() );
        _client.registerTLSContext( address._host, context->get() );
        _listTLSContext.push_back( std::move( *context ) );
        _uniqueSetHost.insert( address._host );
        return {};
    }
} // namespace sw
