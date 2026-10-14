#include "pch.h"

#include "GameFramework/Base/Foundation/Framework/Net/GameNetSession.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Message/NetStatsReport.h"
#include "Core/Network/Transport/UDPNetTransport.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    SW_LOG_CALLER( "GameNetSession" );

    namespace
    {
        struct GameNetSessionInternal
        {
            /** @brief UDP 포트의 가장 큰 값입니다. */
            static constexpr int32 kMaxPort = 65535;
            /** @brief 비율(0~1)을 백분율로 바꾸는 배수입니다. */
            static constexpr float32 kPercent = 100.0f;
            /** @brief 초당 킬로비트(`NetConnectionStats::_sentBandwidth`)를 초당 바이트로 바꾸는 배수입니다(1000 / 8). */
            static constexpr float32 kBytesPerKilobit = 125.0f;
            /** @brief `net.traffic` 이 한 번에 보낼 수 있는 메시지 상한(신뢰 창 `NetConnection::kReliableWindow` 를 넘으면 보내기가 거절된다). */
            [[maybe_unused]] static constexpr int32 kMaxTrafficMessageCount = 200; // 개발 명령만 쓴다(Shipping 에는 없다)
            /**
             * @brief `net.traffic` 메시지의 종류 바이트입니다. 프레임워크 영역(`NetMessageRange::kFramework`) 바로 아래라 처리기가 없고, 받는 쪽 라우터가 버린다.
             * @details 재전송 수치를 보려고 신뢰 메시지를 흘리는 용도다 — 키트가 없는 게임(Empty)의 세션은 연결 유지 패킷만 오가서 다시 보낼 것이 없다.
             */
            [[maybe_unused]] static constexpr uint8 kTrafficMessageKind = NetMessageRange::kFramework - 1;
            /** @brief 지금 게임 인스턴스의 세션입니다(게임 범위의 서브시스템) — 개발 명령 `net.*` 이 읽는다(게임 스레드). 없으면 nullptr 입니다. */
            [[maybe_unused]] static GameNetSession* findActiveSession() { return game::getService<GameNetSession>(); } // 개발 명령만 쓴다(Shipping 에는 없다)

#if SW_DEV_COMMANDS_ENABLED
            /** @brief 명령을 받을 세션 — 없으면 답을 채우고 nullptr 입니다. */
            static GameNetSession* findCommandSession( string& outReply )
            {
                GameNetSession* pSession = findActiveSession();
                if ( pSession == nullptr || pSession->isActive() == false )
                {
                    outReply = "net session: none (start with -gv_netListenPort=<port> or -gv_netConnectAddress=<host:port>)";
                    return nullptr;
                }
                return pSession;
            }

            /** @brief 보내는 쪽 · 받는 쪽 조건을 한 줄로 씁니다(`net.emulation` 의 답). */
            static string describeEmulation( const NetEmulationTransport& emulation )
            {
                return "net emulation up [" + emulation.getDefaultConditions().makeOverrideText() + "] down [" + emulation.getIncomingConditions().makeOverrideText() + "]" +
                       ( emulation.isInOutage() ? " (outage)" : "" );
            }

            /**
             * @brief `net.emulation` — 이 프로세스의 회선 흉내를 바꿉니다.
             * @details - `net.emulation` : 지금 조건
             *          - `net.emulation <up|down|both> [<프리셋>] [key=value …]` : 프리셋에서 시작해(없으면 지금 조건에서) 덮어쓰기를 적용한다. up 은 보내는 쪽, down 은 받는 쪽이다
             *          - `net.emulation spike <초>` : 그동안 보내고 받는 것을 모두 버린다(0 이면 끝낸다)
             *          - `net.emulation stats` : `NetEmulationReport` 한 줄
             */
            static bool runEmulation( const vector<string>& listArgument, string& outReply )
            {
                GameNetSession* pSession = findCommandSession( outReply );
                if ( pSession == nullptr )
                    return true;
                NetEmulationTransport* pEmulation = pSession->findEmulation();
                if ( pEmulation == nullptr )
                {
                    outReply = "net emulation is not available in this build";
                    return true;
                }
                if ( listArgument.empty() )
                {
                    outReply = describeEmulation( *pEmulation );
                    return true;
                }
                const string& verb = listArgument[0];
                if ( verb == "stats" )
                {
                    outReply = pSession->makeEmulationReport().makeLine();
                    return true;
                }
                if ( verb == "spike" )
                {
                    float32 seconds = 0.0f;
                    if ( listArgument.size() != 2 || StringUtil::parseFloat( listArgument[1], seconds ) == false || seconds < 0.0f )
                        return false;
                    pEmulation->startOutage( static_cast<float64>( seconds ) );
                    outReply = "net emulation spike " + listArgument[1] + " s";
                    return true;
                }
                const bool bUp   = verb == "up" || verb == "both";
                const bool bDown = verb == "down" || verb == "both";
                if ( bUp == false && bDown == false )
                    return false;
                size_t                 overrideStart = 1;
                NetEmulationConditions preset;
                const bool             bPreset = listArgument.size() > 1 && listArgument[1].find( '=' ) == string::npos;
                if ( bPreset )
                {
                    if ( NetEmulationConditions::tryGetPreset( listArgument[1], preset ) == false )
                    {
                        outReply = "unknown preset '" + listArgument[1] + "' (" + NetEmulationConditions::getPresetNames() + ")";
                        return true;
                    }
                    overrideStart = 2;
                }
                string overrideText;
                for ( size_t index = overrideStart; index < listArgument.size(); ++index )
                {
                    overrideText += listArgument[index];
                    overrideText += ' ';
                }
                // 방향마다 시작점(프리셋 또는 지금 조건)에 같은 덮어쓰기를 얹는다. 하나라도 틀리면 아무것도 바꾸지 않는다.
                NetEmulationConditions upConditions   = bPreset ? preset : pEmulation->getDefaultConditions();
                NetEmulationConditions downConditions = bPreset ? preset : pEmulation->getIncomingConditions();
                string                 error;
                if ( NetEmulationConditions::parseOverrides( overrideText, upConditions, error ) == false ||
                     NetEmulationConditions::parseOverrides( overrideText, downConditions, error ) == false )
                {
                    outReply = "net.emulation: " + error;
                    return true;
                }
                if ( bUp )
                    pEmulation->setDefaultConditions( upConditions );
                if ( bDown )
                    pEmulation->setIncomingConditions( downConditions );
                outReply = describeEmulation( *pEmulation );
                return true;
            }

            /** @brief `net.disconnect` — 이 프로세스의 모든 연결을 끊습니다(서버는 모든 클라이언트를, 클라이언트는 서버를). */
            static bool runDisconnect( const vector<string>& listArgument, string& outReply )
            {
                (void)listArgument;
                GameNetSession* pSession = findCommandSession( outReply );
                if ( pSession == nullptr )
                    return true;
                pSession->disconnectPeers();
                outReply = "net session disconnected";
                return true;
            }

            /** @brief `net.reconnect` — 끊긴 클라이언트가 다시 붙기 시작합니다. */
            static bool runReconnect( const vector<string>& listArgument, string& outReply )
            {
                (void)listArgument;
                GameNetSession* pSession = findCommandSession( outReply );
                if ( pSession == nullptr )
                    return true;
                outReply = pSession->reconnect() ? "net session reconnecting" : "net session: only a disconnected client reconnects";
                return true;
            }

            /**
             * @brief `net.traffic <메시지 수> [<바이트>]` — 붙은 상대마다 그 크기의 신뢰 메시지를 그만큼 보냅니다(재전송 · 손실 수치를 보는 부하).
             * @details 작은 메시지는 패킷 하나에 여럿 실려 패킷 수가 적다 — 손실을 확실히 맞히려면 메시지를 패킷 하나 크기(1024 바이트까지)로 키운다.
             */
            static bool runTraffic( const vector<string>& listArgument, string& outReply )
            {
                int32      messageCount = 0;
                int32      messageBytes = 1;
                const bool bValid       = ( listArgument.size() == 1 || listArgument.size() == 2 ) && StringUtil::parseInt( listArgument[0], messageCount ) && messageCount >= 1 &&
                                    ( listArgument.size() == 1 || StringUtil::parseInt( listArgument[1], messageBytes ) );
                if ( bValid == false || messageBytes < 1 )
                    return false;
                GameNetSession* pSession = findCommandSession( outReply );
                if ( pSession == nullptr )
                    return true;
                messageCount = MathUtil::min( messageCount, kMaxTrafficMessageCount );
                vector<uint8> payloadBytes( static_cast<size_t>( MathUtil::min( messageBytes, NetConnection::kMaxSingleMessageSize ) ), uint8{ 0 } );
                payloadBytes[0] = kTrafficMessageKind;
                vector<int32> listConnection;
                pSession->getHost().collectConnected( listConnection );
                int32 sentCount = 0;
                for ( const int32 connectionID : listConnection )
                {
                    for ( int32 index = 0; index < messageCount; ++index )
                    {
                        const bool bSent = pSession->getHost().sendMessage( connectionID, NetChannelType::ReliableOrdered, payloadBytes.data(), static_cast<int32>( payloadBytes.size() ) );
                        sentCount += bSent ? 1 : 0;
                    }
                }
                outReply = "net traffic queued " + to_string( sentCount ) + " reliable message(s) to " + to_string( listConnection.size() ) + " connection(s)";
                return true;
            }

            /** @brief `net.stats` — 이 프로세스의 연결 · 트래픽 수치 한 줄(`NetStatsReport`)입니다. 에디터 Profiler 탭이 표준 입력으로 주기적으로 묻습니다. */
            static bool runStats( const vector<string>& listArgument, string& outReply )
            {
                (void)listArgument;
                const GameNetSession* pSession = findCommandSession( outReply );
                if ( pSession == nullptr )
                    return true;
                outReply = pSession->makeStatsReport().makeLine();
                return true;
            }

            /** @brief `net.status` — 이 프로세스의 게임 네트워크 역할 · 포트 · 연결 수입니다. */
            static bool runStatus( const vector<string>& listArgument, string& outReply )
            {
                (void)listArgument;
                const GameNetSession* pSession = findActiveSession();
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
     * @details 언리얼 `?listen` · `-port=` 의 자리. 기본은 꺼짐 — 한 기계에서 서버를 여럿 띄우는 테스트가 포트로 부딪히지 않게.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_netListenPort, 0, "게임 네트워크 서버로 열 UDP 포트 (0=끄기)" );
    /**
     * @brief `-gv_netConnectAddress=<주소:포트>`: 게임 인스턴스가 그 서버에 클라이언트로 붙습니다(비면 끄기). 포트를 빼면 7777 입니다.
     * @note PowerShell 은 점이 든 인자를 쪼갠다 — 따옴표로 감쌀 것.
     */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_netConnectAddress, "", "클라이언트로 붙을 게임 서버 주소 (주소:포트, 비면 끄기)" );
    /**
     * @brief `-gv_netLoopbackOnly=0`: 게임 UDP 소켓을 모든 인터페이스에 묶는다(기본 1 = 127.0.0.1 만).
     * @details 모든 인터페이스에 묶으면 Windows 방화벽이 실행 파일 경로마다 "허용" 창을 띄운다 — 워크트리 · 테스트 실행 파일이 많은 개발 환경에서는 매번 뜬다.
     *          다른 기계와 붙을 때만 0 으로 둔다. 클라이언트는 서버 주소가 루프백이 아닐 때 이 값과 상관없이 모든 인터페이스에 묶는다(답을 받아야 한다).
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_netLoopbackOnly, true, "게임 UDP 소켓을 127.0.0.1 에만 묶는다 (0=모든 인터페이스, 다른 기계와 붙을 때)" );
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
        , _emulation{ nullptr }
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
        outParams                = GameNetSessionParams{};
        outParams._bLoopbackOnly = gv_netLoopbackOnly;
        const bool bListen       = gv_netListenPort > 0;
        const bool bConnect      = gv_netConnectAddress.empty() == false;
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
        // 클라이언트가 다른 기계의 서버에 붙으면 그 서버의 답을 받아야 하므로 루프백에만 묶을 수 없다.
        const bool bRemoteServer = params._role == GameNetRole::Client && params._serverAddress != NetAddress::makeLoopback( params._serverAddress._port );
        const bool bLoopbackOnly = params._bLoopbackOnly && bRemoteServer == false;
        if ( _transport->open( transportPort, bLoopbackOnly ) == false )
        {
            SW_LOG_ERROR( "Net session could not open UDP port %# (in use?)", transportPort );
            _transport.reset();
            return false;
        }
        INetTransport* pHostTransport = _transport.get();
#if !defined( SW_SHIPPING )
        // 회선 조절(개발 명령 net.emulation)이 실행 중에 조건을 바꿀 수 있게 늘 감싼다. 조건이 꺼져 있으면 그대로 넘긴다.
        _emulation = make_unique<NetEmulationTransport>( _transport.get() );
        _emulation->setDefaultConditions( NetEmulationConditions::makeFromGlobalVariables() );
        pHostTransport = _emulation.get();
#endif
        _host.initialize( pHostTransport, params._hostSettings );
        if ( params._role == GameNetRole::Server )
        {
            if ( _host.listen() == false )
            {
                SW_LOG_ERROR( "Net session could not listen on UDP port %#", transportPort );
                _emulation.reset();
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
            _emulation.reset();
            _transport.reset();
            return false;
        }
        _role = params._role;
        if ( _role == GameNetRole::Server )
            SW_LOG_INFO( "Net session listening on UDP port %#", _listenPort );
        else
            SW_LOG_INFO( "Net session connecting to %#", _serverAddress.toString() );
        return true;
    }

    SubsystemInitResult GameNetSession::initializeSubsystem()
    {
        GameNetSessionParams params;
        if ( makeParamsFromGlobalVariables( params ) )
            (void)initialize( params ); // 실패는 initialize 가 알린다 — 게임은 혼자 뜬다
        return SubsystemInitResult::Succeeded;
    }

    void GameNetSession::shutdown()
    {
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
        _emulation.reset(); // 안쪽 전송보다 먼저 — 흉내는 안쪽을 빌려 쓴다
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

    void GameNetSession::disconnectPeers()
    {
        if ( _role == GameNetRole::None )
            return;
        _connectFuture = TaskFuture<NetConnectResult>{}; // 붙는 중이던 것도 버린다 — 끊긴 채로 남는다
        _host.disconnectAll();
        SW_LOG_INFO( "Net session disconnected its peers on request (%#)", toString( _role ) );
    }

    bool GameNetSession::reconnect()
    {
        if ( _role != GameNetRole::Client || _connectFuture.isValid() || _host.getConnectedCount() > 0 )
            return false;
        _bConnectFailureReported = SW_FALSE;
        _connectFuture           = _host.connectAsync( _serverAddress );
        SW_LOG_INFO( "Net session reconnecting to %#", _serverAddress.toString() );
        return true;
    }

    NetEmulationReport GameNetSession::makeEmulationReport() const
    {
        NetEmulationReport report;
        if ( _role == GameNetRole::None )
            return report;
        if ( _emulation != nullptr )
        {
            const NetEmulationStats stats = _emulation->getStats();
            report._sentPacketCount       = stats._sentCount;
            report._droppedPacketCount    = stats._droppedCount + stats._incomingDroppedCount;
        }
        vector<int32> listConnection;
        _host.collectConnected( listConnection );
        report._connectionCount = static_cast<int32>( listConnection.size() );
        for ( const int32 connectionID : listConnection )
        {
            NetConnectionStats stats;
            if ( _host.getConnectionStats( connectionID, stats ) == false )
                continue;
            report._resentMessageCount += stats._resentMessageCount;
            report._measuredLossPercent = MathUtil::max( report._measuredLossPercent, stats._packetLoss * GameNetSessionInternal::kPercent );
            report._rttMilliseconds     = MathUtil::max( report._rttMilliseconds, stats._rtt * constant::kMillisecondsPerSecond );
        }
        return report;
    }

    NetStatsReport GameNetSession::makeStatsReport() const
    {
        NetStatsReport report;
        if ( _role == GameNetRole::None )
            return report;
        vector<int32> listConnection;
        _host.collectConnected( listConnection );
        report._connectionCount = static_cast<int32>( listConnection.size() );
        for ( const int32 connectionID : listConnection )
        {
            NetConnectionStats stats;
            if ( _host.getConnectionStats( connectionID, stats ) == false )
                continue;
            report._sentPacketCount += stats._sentPacketCount;
            report._receivedPacketCount += stats._receivedPacketCount;
            report._resentMessageCount += stats._resentMessageCount;
            report._sentBytesPerSecond += stats._sentBandwidth * GameNetSessionInternal::kBytesPerKilobit;
            report._receivedBytesPerSecond += stats._receivedBandwidth * GameNetSessionInternal::kBytesPerKilobit;
            report._lossPercent     = MathUtil::max( report._lossPercent, stats._packetLoss * GameNetSessionInternal::kPercent );
            report._rttMilliseconds = MathUtil::max( report._rttMilliseconds, stats._rtt * constant::kMillisecondsPerSecond );
        }
        report.populateKinds( _router );
        return report;
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
    SW_DEV_COMMAND( NetEmulation, "net.emulation", "net.emulation [up|down|both [<preset>] [key=value ...] | spike <seconds> | stats]",
                    "Change the net session's line emulation at run time (presets: off, lan, wifi, lte, badwifi, disconnected)", &GameNetSessionInternal::runEmulation );
    SW_DEV_COMMAND( NetDisconnect, "net.disconnect", "net.disconnect", "Drop every connection of the net session (a client stays disconnected until net.reconnect)",
                    &GameNetSessionInternal::runDisconnect );
    SW_DEV_COMMAND( NetReconnect, "net.reconnect", "net.reconnect", "Connect a disconnected client to its server again", &GameNetSessionInternal::runReconnect );
    SW_DEV_COMMAND( NetTraffic, "net.traffic", "net.traffic <messages> [<bytes>]", "Send that many reliable messages (1 to 1024 bytes) to every peer (load to watch resends)",
                    &GameNetSessionInternal::runTraffic );
    SW_DEV_COMMAND( NetStats, "net.stats", "net.stats", "Show the net session's connection and traffic numbers in one line (packets, bytes per second, RTT, loss, message kinds)",
                    &GameNetSessionInternal::runStats );
} // namespace sw
