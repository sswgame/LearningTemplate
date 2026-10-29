#include "pch.h"

#include "Server/ServerAdminChannel.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Process/ProcessEnvironment.h"
#include "Core/Process/ShutdownSignal.h"

#include "Engine/Config/Server/ServerConfig.h"
#include "Engine/DedicatedServer/ServiceHealthRegistry.h"
#include "Engine/Observability/DebugChannel.h"

namespace sw
{
    SW_LOG_CALLER( "Server" );

    namespace
    {
        struct ServerAdminChannelInternal
        {
            /** @brief 관리 채널 연결이 이만큼(초) 말이 없으면 닫는다 - 열어 두고 잊은 연결이 쌓이지 않게. */
            static constexpr float64 kAdminIdleTimeoutSeconds = 300.0;
        };
    } // namespace

    ServerAdminChannel::ServerAdminChannel()
        : _transport{ nullptr }
        , _channel{ nullptr }
        , _pHealthRegistry{ nullptr }
        , _status{}
        , _bMaintenance{ false }
    {
    }

    ServerAdminChannel::~ServerAdminChannel() = default;

    bool ServerAdminChannel::start( const ServerConfig& config, ServiceHealthRegistry& healthRegistry, const ServerAdminStatusDelegate& status )
    {
        if ( config._adminChannelPort <= 0 )
            return true;
        // 토큰은 환경에서만 읽는다(설정 파일 · 명령줄에 두지 않는다). 없거나 비면 채널을 토큰 없이 여는 대신 기동을 멈춘다.
        string token;
        if ( ProcessEnvironment::tryReadNonEmpty( config._adminChannelTokenEnvironment, token ) == false )
        {
            SW_LOG_ERROR( "The admin channel is enabled (_adminChannelPort=%#) but the token environment variable '%#' is not set or empty - the server does not start without a token",
                          config._adminChannelPort, config._adminChannelTokenEnvironment.c_str() );
            return false;
        }
        _pHealthRegistry = &healthRegistry;
        _status          = status;
        _transport       = StreamTransportFactory::createPlatformTransport();
        _channel         = make_unique<DebugChannel>();
        DebugChannelConfig settings;
        settings._bindAddress = NetAddress::makeLoopback( static_cast<uint16>( config._adminChannelPort ) );
        settings._token       = std::move( token );
        StreamTransportConfig transportSettings;
        transportSettings._idleTimeoutSeconds = ServerAdminChannelInternal::kAdminIdleTimeoutSeconds; // 열어 두고 잊은 연결을 닫는다
        const bool bStarted                   = _channel->initialize( _transport.get(), transportSettings, settings,
                                                                      SW_DELEGATE_LAMBDA( DebugChannelExecuteDelegate, [this]( string_view line, string& outReply )
                          { return executeCommand( line, outReply ); } ) );
        if ( bStarted == false )
        {
            SW_LOG_ERROR( "The admin channel (_adminChannelPort=%#) could not start", config._adminChannelPort );
            _channel.reset();
            _transport.reset();
            return false;
        }
        SW_LOG_INFO( "Admin channel listening on loopback port %# (token from environment variable %#)", _channel->getListenPort(), config._adminChannelTokenEnvironment.c_str() );
        return true;
    }

    void ServerAdminChannel::stop()
    {
        if ( _channel != nullptr )
            _channel->shutdown();
        _channel.reset();
        _transport.reset();
        _pHealthRegistry = nullptr;
        _status          = ServerAdminStatusDelegate{};
    }

    void ServerAdminChannel::pump()
    {
        if ( _channel != nullptr )
            (void)_channel->pump();
    }

    bool ServerAdminChannel::executeCommand( string_view line, string& outReply )
    {
        const string_view command  = StringUtil::trim( line );
        const size_t      space    = command.find( ' ' );
        const string_view name     = space == string_view::npos ? command : command.substr( 0, space );
        const string_view argument = space == string_view::npos ? string_view{} : StringUtil::trim( command.substr( space + 1 ) );
        if ( name == "help" )
        {
            outReply = "admin commands: status | maintenance on|off|state | shutdown";
            return true;
        }
        if ( name == "status" )
        {
            const ServerAdminStatus status = _status();
            outReply                       = "tick " + to_string( status._tickCount ) + " connections " + to_string( status._connectionCount ) + " maintenance " + ( _bMaintenance ? "on" : "off" ) + " draining " +
                       ( status._bDraining ? "yes" : "no" );
            return true;
        }
        if ( name == "maintenance" )
        {
            if ( argument == "state" )
            {
                outReply = _bMaintenance ? "maintenance on" : "maintenance off";
                return true;
            }
            if ( argument != "on" && argument != "off" )
            {
                outReply = "usage: maintenance on|off|state";
                return false;
            }
            if ( _status()._bDraining )
            {
                outReply = "the server is draining for shutdown - maintenance mode cannot change";
                return false;
            }
            _bMaintenance = argument == "on";
            _pHealthRegistry->setDraining( _bMaintenance ); // /readyz 503 이면 부하 분산기가 새 접속을 보내지 않는다. 이미 접속한 사람은 그대로다.
            SW_LOG_INFO( "Admin channel: maintenance mode %#", _bMaintenance ? "on" : "off" );
            outReply = _bMaintenance ? "maintenance on" : "maintenance off";
            return true;
        }
        if ( name == "shutdown" )
        {
            SW_LOG_INFO( "Admin channel: graceful shutdown requested" );
            ShutdownSignal::request( ShutdownCause::Terminate ); // 종료 신호(SIGTERM)와 같은 길 - 드레인 뒤 정상 종료
            outReply = "shutdown requested";
            return true;
        }
        outReply = "unknown admin command (help lists them)";
        return false;
    }
} // namespace sw
