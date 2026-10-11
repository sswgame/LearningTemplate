#include "pch.h"

#include "GameFramework/Base/Foundation/Framework/Net/GameNetSession.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Network/Transport/UDPNetTransport.h"

#include "Engine/Console/DevCommandRegistry.h"

namespace sw
{
    SW_LOG_CALLER( "GameNetSession" );

    namespace
    {
        struct GameNetSessionInternal
        {
            /** @brief UDP 포트의 가장 큰 값입니다. */
            static constexpr int32 kMaxPort = 65535;
            /** @brief 지금 열린 세션 — 개발 명령 `net.status` 가 읽는다(게임 스레드에서만 쓰고 읽는다). */
            static inline GameNetSession* s_pActiveSession{ nullptr };

#if SW_DEV_COMMANDS_ENABLED
            /** @brief `net.status` — 이 프로세스의 게임 네트워크 역할 · 포트 · 연결 수입니다. */
            static bool runStatus( const vector<string>& listArgument, string& outReply )
            {
                (void)listArgument;
                const GameNetSession* pSession = s_pActiveSession;
                if ( pSession == nullptr || pSession->isActive() == false )
                {
                    outReply = "net session: none (start with -gv_netListenPort=<port> or -gv_netConnectAddress=<host:port>)";
                    return true;
                }
                outReply = string( "net session: " ) + toString( pSession->getRole() ) + ", connected " + to_string( pSession->getConnectedCount() );
                if ( pSession->getRole() == GameNetRole::Server )
                    outReply += ", port " + to_string( pSession->getListenPort() );
                return true;
            }
#endif
        };
    } // namespace

    /**
     * @brief `-gv_netListenPort=<포트>`: 게임 인스턴스가 이 UDP 포트를 열고 클라이언트를 받습니다(0=끄기). 전용 서버 · 리슨 호스트 모두.
     * @details 언리얼 `?listen` · `-port=` 의 자리. 기본은 꺼짐 — 한 기계에서 서버를 여럿 띄우는 시험이 포트로 부딪히지 않게.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_netListenPort, 0, "게임 네트워크 서버로 열 UDP 포트 (0=끄기)" );
    /**
     * @brief `-gv_netConnectAddress=<주소:포트>`: 게임 인스턴스가 그 서버에 클라이언트로 붙습니다(비면 끄기). 포트를 빼면 7777 입니다.
     * @note PowerShell 은 점이 든 인자를 쪼갠다 — 따옴표로 감쌀 것.
     */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_netConnectAddress, "", "클라이언트로 붙을 게임 서버 주소 (주소:포트, 비면 끄기)" );
} // namespace sw

namespace sw
{
    const utf8* toString( GameNetRole role )
    {
        switch ( role )
        {
            case GameNetRole::None:
                return "None";
            case GameNetRole::Server:
                return "Server";
            case GameNetRole::Client:
                return "Client";
        }
        return "Unknown";
    }

    GameNetSession::GameNetSession()
        : _host{}
        , _thread{}
        , _router{}
        , _transport{ nullptr }
        , _connectFuture{}
        , _listEventScratch{}
        , _serverAddress{}
        , _listenPort{ 0 }
        , _role{ GameNetRole::None }
        , _bConnectFailureReported{ SW_FALSE }
    {
    }

    GameNetSession::~GameNetSession()
    {
        shutdown();
    }

    bool GameNetSession::makeParamsFromGlobalVariables( GameNetSessionParams& outParams )
    {
        outParams           = GameNetSessionParams{};
        const bool bListen  = gv_netListenPort > 0;
        const bool bConnect = gv_netConnectAddress.empty() == false;
        if ( bListen == false && bConnect == false )
            return false;
        if ( bListen )
        {
            if ( GameNetSessionInternal::kMaxPort < gv_netListenPort )
            {
                SW_LOG_WARNING( "gv_netListenPort %# is not a UDP port - no net session", gv_netListenPort );
                return false;
            }
            if ( bConnect )
                SW_LOG_WARNING( "Both gv_netListenPort and gv_netConnectAddress are set - starting as the server on port %#", gv_netListenPort );
            outParams._role       = GameNetRole::Server;
            outParams._listenPort = static_cast<uint16>( gv_netListenPort );
            return true;
        }
        if ( NetAddress::parse( gv_netConnectAddress, kDefaultPort, outParams._serverAddress ) == false || outParams._serverAddress.isValid() == false )
        {
            SW_LOG_WARNING( "gv_netConnectAddress '%#' is not an IPv4 address with a port - no net session", gv_netConnectAddress );
            return false;
        }
        outParams._role = GameNetRole::Client;
        return true;
    }

    bool GameNetSession::initialize( const GameNetSessionParams& params )
    {
        shutdown();
        if ( params._role == GameNetRole::None )
            return false;
        _transport                 = make_unique<UDPNetTransport>();
        const uint16 transportPort = params._role == GameNetRole::Server ? params._listenPort : 0;
        if ( _transport->open( transportPort ) == false )
        {
            SW_LOG_ERROR( "Net session could not open UDP port %# (in use?)", transportPort );
            _transport.reset();
            return false;
        }
        _host.initialize( _transport.get(), params._hostSettings );
        if ( params._role == GameNetRole::Server )
        {
            if ( _host.listen() == false )
            {
                SW_LOG_ERROR( "Net session could not listen on UDP port %#", transportPort );
                _transport.reset();
                return false;
            }
            _listenPort = _transport->getLocalAddress()._port;
        }
        else
        {
            _serverAddress = params._serverAddress;
            _connectFuture = _host.connectAsync( _serverAddress );
        }
        if ( _thread.start( &_host ) == false )
        {
            SW_LOG_ERROR( "Net session could not start its network thread" );
            _host.disconnectAll();
            _transport.reset();
            return false;
        }
        _role                                    = params._role;
        GameNetSessionInternal::s_pActiveSession = this;
        if ( _role == GameNetRole::Server )
            SW_LOG_INFO( "Net session listening on UDP port %#", _listenPort );
        else
            SW_LOG_INFO( "Net session connecting to %#", _serverAddress.toString() );
        return true;
    }

    void GameNetSession::shutdown()
    {
        if ( GameNetSessionInternal::s_pActiveSession == this )
            GameNetSessionInternal::s_pActiveSession = nullptr;
        if ( _role == GameNetRole::None && _transport == nullptr )
            return;
        // 스레드를 먼저 멈춘다 — 끊기 패킷은 disconnectAll 이 바로 보낸다(update 를 돌 스레드가 없어도).
        _thread.stop();
        // 마지막 update 뒤에 쌓인 연결 사건도 로그에 남긴다 — 접속 직후 종료 명령이 같은 틱에 오면 update 없이 여기로 온다.
        // 처리기에는 나눠 주지 않는다(키트 처리기는 onShutdown 에서 내려갔다).
        if ( _role != GameNetRole::None )
        {
            _listEventScratch.clear();
            _host.drainEvents( _listEventScratch );
            logEvents();
        }
        _host.disconnectAll();
        if ( _role != GameNetRole::None )
            SW_LOG_INFO( "Net session closed (%#)", toString( _role ) );
        _transport.reset();
        _connectFuture = TaskFuture<NetConnectResult>{};
        _listEventScratch.clear();
        _listenPort              = 0;
        _role                    = GameNetRole::None;
        _bConnectFailureReported = SW_FALSE;
    }

    void GameNetSession::update()
    {
        if ( _role == GameNetRole::None )
            return;
        reportConnectResult();
        _listEventScratch.clear();
        (void)_router.pump( _host, nullptr, &_listEventScratch ); // 처리기가 없는 메시지는 버린다(맡은 키트가 없다)
        logEvents();
    }

    void GameNetSession::logEvents() const
    {
        for ( const NetHostEvent& event : _listEventScratch )
        {
            if ( _role == GameNetRole::Server )
            {
                if ( event._kind == NetHostEvent::Kind::Connected )
                    SW_LOG_INFO( "Net session client %# connected from %# (%# connected)", event._connectionID,
                                 _host.getConnectionAddress( event._connectionID ).toString(), _host.getConnectedCount() );
                else
                    SW_LOG_INFO( "Net session client %# disconnected (%#, %# connected)", event._connectionID, toString( event._reason ), _host.getConnectedCount() );
            }
            else if ( event._kind == NetHostEvent::Kind::Disconnected )
            {
                SW_LOG_WARNING( "Net session lost the server %# (%#)", _serverAddress.toString(), toString( event._reason ) );
            }
        }
    }

    int32 GameNetSession::getConnectedCount() const
    {
        return _role == GameNetRole::None ? 0 : _host.getConnectedCount();
    }

    void GameNetSession::reportConnectResult()
    {
        if ( _connectFuture.isValid() == false || _connectFuture.isReady() == false )
            return;
        const NetConnectResult result = _connectFuture.get();
        _connectFuture                = TaskFuture<NetConnectResult>{};
        if ( result.isConnected() )
        {
            SW_LOG_INFO( "Net session connected to %# as client %#", _serverAddress.toString(), result._clientIndex );
            return;
        }
        // 시간 초과는 다시 시도한다 — 같이 띄운 서버가 아직 기동 중일 수 있다(멀티 프로세스 재생 · 느린 Debug 기동). 거절 · 판 불일치는 다시 해도 같다.
        const bool bRetry = result._reason == NetDisconnectReason::Timeout;
        if ( _bConnectFailureReported == SW_FALSE || bRetry == false )
            SW_LOG_WARNING( "Net session could not connect to %# (%#)%#", _serverAddress.toString(), toString( result._reason ), bRetry ? " - retrying" : "" );
        _bConnectFailureReported = SW_TRUE;
        if ( bRetry )
            _connectFuture = _host.connectAsync( _serverAddress );
    }
} // namespace sw

namespace sw
{
    SW_DEV_COMMAND( NetStatus, "net.status", "net.status", "Show the game net session role, port and connection count", &GameNetSessionInternal::runStatus );
} // namespace sw
