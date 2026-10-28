#include "pch.h"

#include "GameFramework/PlugIn/Editor/Admin/AdminToolConnection.h"

#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/IStreamTransport.h"

#include "GameFramework/Base/Online/Security/NetSecurity.h"

namespace sw::editor
{
    AdminToolConnection::AdminToolConnection()
        : _tlsContext{}
        , _pTransport{}
        , _client{}
        , _admin{}
        , _onReady{}
        , _state{ AdminToolConnectionState::Disconnected }
        , _bServiceRegistered{ false }
        , _bReadyReported{ false }
    {
    }

    AdminToolConnection::~AdminToolConnection()
    {
        close();
    }

    bool AdminToolConnection::open( string_view address, string_view gameBuild, string& outError, string_view tlsTrustFile )
    {
        close();
        NetAddress serverAddress;
        if ( NetAddress::parse( address, 0, serverAddress ) == false || serverAddress.isValid() == false )
        {
            outError = "GM address needs host:port (for example 127.0.0.1:7778)";
            _state   = AdminToolConnectionState::Failed;
            return false;
        }
        _pTransport = StreamTransportFactory::createPlatformTransport();
        if ( _pTransport == nullptr )
        {
            outError = "this platform has no stream transport";
            _state   = AdminToolConnectionState::Failed;
            return false;
        }
        if ( _bServiceRegistered == false )
        {
            _admin.initialize( &_client );
            if ( _client.registerClientService( &_admin ) == false )
            {
                outError = "the GM client could not be registered (method range already taken)";
                _state   = AdminToolConnectionState::Failed;
                return false;
            }
            _bServiceRegistered = true;
        }
        _tlsContext = NetSecurity::createClientTLSContext( tlsTrustFile, "", outError );
        if ( _tlsContext == nullptr )
        {
            _state = AdminToolConnectionState::Failed;
            return false;
        }
        OnlineServiceClientConfig settings{ OnlineClientSecurity::tls( *_tlsContext ) };
        settings._transportSettings._ioThreadCount = 0; // 에디터 프레임의 update 가 전송을 돌린다
        settings._serverAddress                    = serverAddress;
        settings._gameBuild                        = string( gameBuild );
        if ( _client.initialize( _pTransport.get(), settings, outError ) == false )
        {
            _state = AdminToolConnectionState::Failed;
            return false;
        }
        _bReadyReported = false;
        _state          = AdminToolConnectionState::Connecting;
        return true;
    }

    void AdminToolConnection::close()
    {
        _client.shutdown();
        _pTransport.reset();
        _tlsContext.reset();
        _bReadyReported = false;
        _state          = AdminToolConnectionState::Disconnected;
    }

    void AdminToolConnection::update( MonotonicMilliseconds nowMs )
    {
        if ( _state != AdminToolConnectionState::Connecting && _state != AdminToolConnectionState::Ready )
            return;
        _client.update( nowMs );
        const OnlineClientState clientState = _client.getState();
        if ( clientState == OnlineClientState::VersionMismatch || clientState == OnlineClientState::HandshakeFailed )
        {
            _state = AdminToolConnectionState::Failed; // 같은 빌드끼리 다시 걸어도 같다 — 사용자가 주소나 빌드를 고쳐 다시 연다
            return;
        }
        if ( _client.isReady() == false )
        {
            _state          = AdminToolConnectionState::Connecting;
            _bReadyReported = false; // 끊겼다 다시 붙으면 로그인을 다시 한다
            return;
        }
        _state = AdminToolConnectionState::Ready;
        if ( _bReadyReported == false )
        {
            _bReadyReported = true;
            if ( _onReady.isBound() )
                _onReady.get()( _client );
        }
    }

    const utf8* AdminToolConnection::getStateText() const
    {
        switch ( _state )
        {
            case AdminToolConnectionState::Disconnected:
            {
                return "Disconnected";
            }
            case AdminToolConnectionState::Connecting:
            {
                return "Connecting";
            }
            case AdminToolConnectionState::Ready:
            {
                return _onReady.isBound() ? "Ready (login hook ran)" : "Ready (no login hook - GM commands are refused as not logged in)";
            }
            case AdminToolConnectionState::Failed:
            {
                return "Failed";
            }
        }
        return "Unknown";
    }
} // namespace sw::editor
