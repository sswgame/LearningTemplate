#include "pch.h"

#include "GameFramework/Runtime/Online/Service/OnlineSession.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Network/Transport/PlatformSocketUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/WallClock.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/Server/ServerConfig.h"
#include "Engine/Console/DevCommandRegistry.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"
#include "GameFramework/Runtime/Online/Security/NetSecurity.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerRuntime.h"
#include "GameFramework/Runtime/Online/Service/OnlineClientRegistry.h"
#include "GameFramework/Runtime/Online/Service/OnlineServiceHost.h"

namespace sw
{
    namespace
    {
        struct OnlineSessionInternal
        {
            static constexpr const utf8* kPlatformText =
#if defined( SW_PLATFORM_WINDOWS )
                "windows";
#elif defined( SW_PLATFORM_LINUX )
                "linux";
#else
    #error "Unsupported platform"
#endif

            static OnlineSessionInfo makeInfo( const GameSettings& settings )
            {
                OnlineSessionInfo info;
                info._gameBuild = settings._onlineGameBuild;
                info._platform  = kPlatformText;
                return info;
            }

            static const utf8* describeState( OnlineClientState state )
            {
                switch ( state )
                {
                    case OnlineClientState::Disconnected:
                        return "disconnected";
                    case OnlineClientState::Connecting:
                        return "connecting";
                    case OnlineClientState::Negotiating:
                        return "negotiating";
                    case OnlineClientState::Ready:
                        return "ready";
                    case OnlineClientState::VersionMismatch:
                        return "version mismatch";
                    case OnlineClientState::HandshakeFailed:
                        return "handshake failed";
                }
                return "unknown";
            }

            // 개발 명령 - 게임 코드 없이 세션을 시나리오와 스크립트가 움직인다.
            static bool runConnect( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
                if ( pSession == nullptr )
                {
                    outReply = "online: no game instance";
                    return true;
                }
                string error;
                outReply = pSession->connect( error ) ? "online: connecting" : "online: " + error;
                return true;
            }

            static bool runDisconnect( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
                if ( pSession != nullptr )
                    pSession->disconnect();
                outReply = "online: disconnected";
                return true;
            }

            static bool runStatus( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                const OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
                if ( pSession == nullptr )
                {
                    outReply = "online: no game instance";
                    return true;
                }
                StringBuilder<constant::buffer::kMax256> reply;
                reply.appendFormat( "online: %#, %# kit clients, local server %#", describeState( pSession->getState() ), pSession->getClientCount(), pSession->hasLocalServer() ? "on" : "off" );
                outReply = string( reply.view() );
                return true;
            }

            static bool runLocalStart( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
                if ( pSession == nullptr )
                {
                    outReply = "online: no game instance";
                    return true;
                }
                string error;
                if ( pSession->startLocalServer( listArgument[0], error ) == false )
                {
                    SW_LOG_ERROR_ID( "OnlineSession.LocalServerFailed", "The local online server did not start: %#", error.c_str() );
                    outReply = "online: " + error;
                    return true;
                }
                outReply = "online: local server up";
                return true;
            }

            [[nodiscard]] static bool readState( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
                if ( pSession == nullptr )
                    return false;
                outValue = static_cast<float64>( pSession->getState() );
                return true;
            }

            [[nodiscard]] static bool readClientCount( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
                if ( pSession == nullptr )
                    return false;
                outValue = static_cast<float64>( pSession->getClientCount() );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    OnlineSession::OnlineSession( const GameSettings& settings )
        : _settings{ settings }
        , _clock{}
        , _localServer{}
        , _tlsContext{}
        , _transport{}
        , _client{}
        , _listClient{}
        , _bConnected{ SW_FALSE }
    {
    }

    OnlineSession::~OnlineSession()
    {
        disconnect();
    }

    SubsystemInitResult OnlineSession::initializeSubsystem()
    {
        const OnlineSessionInfo      info = OnlineSessionInternal::makeInfo( _settings );
        vector<OnlineClientFactory*> listFactory;
        OnlineClientRegistry::collectFactories( listFactory );
        for ( OnlineClientFactory* pFactory : listFactory )
        {
            ClientEntry entry;
            entry._client   = pFactory->_pCreate( _client, info );
            entry._typeHash = pFactory->_typeHash;
            if ( entry._client == nullptr || _client.registerClientService( entry._client.get() ) == false )
            {
                SW_LOG_ERROR_ID( "OnlineSession.ClientNotRegistered", "Online client '%#' could not be registered (no client or its method range is taken)", pFactory->_pName );
                return SubsystemInitResult::Failed;
            }
            _listClient.push_back( std::move( entry ) );
        }
        return SubsystemInitResult::Succeeded;
    }

    void OnlineSession::shutdownSubsystem()
    {
        disconnect();
        _listClient.clear();
    }

    bool OnlineSession::connect( string& outError )
    {
        const string& address = _settings._onlineServerAddress; // 개발 구성의 실행 인자 덮어쓰기는 게임 인스턴스가 이미 넣었다(`IGameSettingsOverride`)
        if ( address.empty() )
        {
            outError = "no server address (onlineServerAddress in gamesettings)";
            return false;
        }
        if ( _settings._onlineGameBuild.empty() )
        {
            outError = "no game build text (onlineGameBuild in gamesettings)";
            return false;
        }
        NetAddress serverAddress;
        if ( PlatformSocketUtil::resolveAddress( address, OnlineServiceHostConfig::kDefaultPort, serverAddress ) == false )
        {
            outError = "server address '" + address + "' did not resolve";
            return false;
        }
        disconnect();
        _transport = StreamTransportFactory::createPlatformTransport();
        if ( _transport == nullptr )
        {
            outError = "this platform has no stream transport";
            return false;
        }
        // 서버의 서비스 스트림은 늘 TLS 1.3 이다. 믿을 인증서가 비면 개발 구성만 개발 인증서를 못 박아 믿고, 배포 구성은 여기서 오류다.
        _tlsContext = NetSecurity::createClientTLSContext( game::getSubsystem<INetSecurityProvider>(), _settings._onlineTrustFile, "", outError );
        if ( _tlsContext == nullptr )
            return false;
        return connectTo( _transport.get(), OnlineClientSecurity::tls( *_tlsContext ), serverAddress, outError );
    }

    bool OnlineSession::connectTo( IStreamTransport* pTransport, const OnlineClientSecurity& security, const NetAddress& address, string& outError )
    {
        _client.shutdown();
        OnlineServiceClientConfig settings{ security };
        settings._transportSettings._ioThreadCount = 0; // `update` 가 전송을 돌린다
        settings._serverAddress                    = address;
        settings._gameBuild                        = _settings._onlineGameBuild;
        _bConnected                                = _client.initialize( pTransport, settings, outError ) ? SW_TRUE : SW_FALSE;
        return _bConnected == SW_TRUE;
    }

    bool OnlineSession::startLocalServer( string_view serverConfigPath, string& outError )
    {
        if ( build::kShipping )
        {
            outError = "the in-process online server is a development tool";
            return false;
        }
        const string              serverPath{ serverConfigPath };
        const string              onlinePath = OnlineServerConfig::makeConfigPath( serverConfigPath );
        ServerConfig              serverConfig;
        OnlineServerRuntimeConfig settings;
        if ( ConfigManager::readConfigFileInto( serverPath, serverConfig ) != ConfigReadResult::Loaded || ConfigManager::readConfigFileInto( onlinePath, settings._config ) != ConfigReadResult::Loaded )
        {
            outError = "server config '" + serverPath + "' or its online config '" + onlinePath + "' could not be read";
            return false;
        }
        if ( OnlineServerRuntime::resolveStores( serverConfig, settings._config, settings, outError ) == false )
            return false;
        disconnect();
        settings._config._ioThreadCount = 0; // `update` 가 전송을 돌린다
        settings._bTLS                  = SW_FALSE;
        settings._pClock                = &_clock;
        settings._listenAddress         = NetAddress::makeLoopback( 0 );
        _localServer                    = make_unique<OnlineServerRuntime>();
        if ( _localServer->initialize( std::move( settings ), WallClock::nowUnixMilliseconds(), outError ) == false ) // 예외 - 서비스 시각의 뿌리다(`OnlineServerService` 와 같은 자리)
        {
            disconnect();
            return false;
        }
        _transport = StreamTransportFactory::createPlatformTransport();
        if ( _transport == nullptr )
        {
            outError = "this platform has no stream transport";
            disconnect();
            return false;
        }
        return connectTo( _transport.get(), OnlineClientSecurity::plaintextLoopback(), NetAddress::makeLoopback( _localServer->getListenPort() ), outError );
    }

    void OnlineSession::disconnect()
    {
        _client.shutdown();
        _bConnected = SW_FALSE;
        _transport.reset();
        _tlsContext.reset();
        if ( _localServer != nullptr )
            _localServer->shutdown();
        _localServer.reset();
    }

    void OnlineSession::update()
    {
        if ( _bConnected == SW_TRUE )
            updateAt( _clock.nowMilliseconds() );
    }

    void OnlineSession::updateAt( MonotonicMilliseconds nowMs )
    {
        if ( _bConnected == SW_FALSE )
            return;
        _client.update( nowMs );
        if ( _localServer != nullptr )
            _localServer->update( WallClock::nowUnixMilliseconds() );
    }

    IServiceStore* OnlineSession::findLocalServerStore() const
    {
        return _localServer != nullptr ? _localServer->getContext()._pStore : nullptr;
    }

    IOnlineClientService* OnlineSession::findByType( uint64 typeHash ) const
    {
        for ( const ClientEntry& entry : _listClient )
        {
            if ( entry._typeHash == typeHash )
                return entry._client.get();
        }
        return nullptr;
    }
} // namespace sw

namespace sw
{
    SW_DEV_COMMAND( OnlineConnect, "online.connect", "online.connect", "Connect the online session to the server in gamesettings", &OnlineSessionInternal::runConnect );
    SW_DEV_COMMAND( OnlineDisconnect, "online.disconnect", "online.disconnect", "Close the online session and stop its in-process server", &OnlineSessionInternal::runDisconnect );
    SW_DEV_COMMAND( OnlineStatus, "online.status", "online.status", "Show the online session state, its kit client count and whether an in-process server runs", &OnlineSessionInternal::runStatus );
    SW_DEV_COMMAND( OnlineLocalStart, "online.local.start", "online.local.start <server config path>", "Start an in-process online server from that server config and connect the session to it (development only)", &OnlineSessionInternal::runLocalStart );

    SW_AUTOMATION_WATCH( onlineSessionState, "Online.SessionState", "Online session state: 0 disconnected, 1 connecting, 2 negotiating, 3 ready, 4 version mismatch, 5 handshake failed", &OnlineSessionInternal::readState );
    SW_AUTOMATION_WATCH( onlineSessionClientCount, "Online.SessionClientCount", "Kit clients the online session built from the loaded kits", &OnlineSessionInternal::readClientCount );
} // namespace sw
