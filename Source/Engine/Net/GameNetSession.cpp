#include "pch.h"

#include "Engine/Net/GameNetSession.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/unordered_set.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Connection/NetEndpointThread.h"
#include "Core/Network/Message/NetStatsReport.h"
#include "Core/Network/Transport/PlatformSocketUtil.h"
#include "Core/Network/Transport/UDPNetTransport.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/Server/ServerConfig.h"
#include "Engine/DedicatedServer/ServerServiceRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Net/GameConnectSecurity.h"
#include "Engine/Reflection/EnumName.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "GameNetSession" );

    namespace
    {
        struct GameNetSessionInternal
        {
            /** @brief UDP 포트의 가장 큰 값입니다. */
            static constexpr int32 kMaxPort = 65535;
            /** @brief 리눅스만 한 포트를 `SO_REUSEPORT` 로 나눌 수 있습니다(`UDPPortSharing::ReusePort`). */
#if defined( SW_PLATFORM_LINUX )
            static constexpr bool kbCanSharePort = true;
#else
            static constexpr bool kbCanSharePort = false;
#endif
            /**
             * @brief 샤드 배치가 열 수 있는 모양인가입니다. 틀리면 오류 로그와 false 입니다(다른 배치로 대신 열지 않는다).
             * @details 서버는 샤드 1 개 이상, 클라이언트는 정확히 1 개다. 샤드 여럿은 고정된 첫 포트가 있어야 한다 — 배정이 "첫 포트 + 샤드" 를 알려 주므로
             *          운영체제가 고른 포트(0)로는 계약을 지킬 수 없다. 연이은 포트의 마지막이 UDP 포트 범위를 넘어도 안 된다.
             */
            static bool isValidShardLayout( const GameNetSessionParams& params )
            {
                const bool bServer        = params._role == GameNetRole::Server;
                const bool bBadShardCount = params._shardCount < 1 || ( bServer == false && params._shardCount != 1 );
                if ( bBadShardCount )
                {
                    SW_LOG_ERROR( "Net session shard count %# - a server opens 1 or more shards and a client exactly 1", params._shardCount );
                    return false;
                }
                const bool bBadPortSharing = params._bShardsSharePort && ( bServer == false || kbCanSharePort == false || params._shardCount < 2 );
                if ( bBadPortSharing )
                {
                    SW_LOG_ERROR( "Net session port sharing is for a Linux server with 2 or more shards (%# shard(s))", params._shardCount );
                    return false;
                }
                if ( params._shardCount > 1 && params._listenPort == 0 )
                {
                    SW_LOG_ERROR( "Net session %# shards on port 0 - shards need a fixed first port (the server assignment hands out first port + shard)", params._shardCount );
                    return false;
                }
                const int32 lastPort = params._bShardsSharePort ? static_cast<int32>( params._listenPort ) : static_cast<int32>( params._listenPort ) + params._shardCount - 1;
                if ( kMaxPort < lastPort )
                {
                    SW_LOG_ERROR( "Net session shards %#-%# run past the UDP port range", params._listenPort, lastPort );
                    return false;
                }
                return true;
            }
            /** @brief 비율(0~1)을 백분율로 바꾸는 배수입니다. */
            static constexpr float32 kPercent = 100.0f;
            /** @brief 초당 킬로비트(`NetConnectionStats::_sentBandwidth`)를 초당 바이트로 바꾸는 배수입니다(1000 / 8). */
            static constexpr float32 kBytesPerKilobit = 125.0f;
        };
    } // namespace

    /**
     * @brief `-gv_netListenPort=<포트>`: 게임 인스턴스가 이 UDP 포트를 열고 클라이언트를 받습니다(0=끄기). 전용 서버 · 리슨 호스트 모두.
     * @details 언리얼 `?listen` · `-port=` 의 자리. 리슨 호스트(App)는 이것으로만 연다. 전용 서버는 이것이 없으면 서버 설정(`ServerConfig::_gamePort`)을
     *          열고, 있으면 개발 덮어쓰기로 설정보다 우선한다(에디터 멀티 프로세스 재생이 고른 포트). 테스트 변수라 Shipping 에는 없다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_netListenPort, 0, "게임 네트워크 서버로 열 UDP 포트 (0=끄기)" );
    /**
     * @brief `-gv_netConnectAddress=<주소:포트>`: 게임 인스턴스가 그 서버에 클라이언트로 붙습니다(비면 끄기). 포트를 빼면 7777 입니다.
     * @details 배포 클라이언트도 이것으로 서버에 붙으므로 Shipping 에도 등록한다(언리얼 실행 인자 URL 의 자리).
     * @note PowerShell 은 점이 든 인자를 쪼갠다 — 따옴표로 감쌀 것.
     */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( sw::string, gv_netConnectAddress, "", "클라이언트로 붙을 게임 서버 주소 (주소:포트, 비면 끄기)" );
    /**
     * @brief `-gv_netLoopbackOnly=0`: `-gv_netListenPort` 로 연 서버의 UDP 소켓을 모든 인터페이스에 묶는다(기본 1 = 127.0.0.1 만).
     * @details 모든 인터페이스에 묶으면 Windows 방화벽이 실행 파일 경로마다 "허용" 창을 띄운다 — 워크트리 · 테스트 실행 파일이 많은 개발 환경에서는 매번 뜬다.
     *          다른 기계와 붙을 때만 0 으로 둔다. `gv_netListenPort` 와 함께 쓰는 개발 덮어쓰기라 같이 Shipping 에 없다.
     *          클라이언트는 서버 주소가, 서버 설정으로 연 전용 서버는 `ServerConfig::_listenAddress` 가 묶을 곳을 정한다 — 이 값을 읽지 않는다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_netLoopbackOnly, true, "게임 UDP 소켓을 127.0.0.1 에만 묶는다 (0=모든 인터페이스, 다른 기계와 붙을 때)" );
} // namespace sw

namespace sw
{
    /**
     * @brief 포트 샤드 하나 — 소켓, 그것을 감싼 데코레이터(있으면), 호스트, 네트워크 스레드입니다.
     * @details 선언 순서가 소멸 순서를 정한다: 스레드 → 호스트 → 데코레이터 → 소켓(데코레이터는 안쪽 전송을 빌려 쓰고, 호스트는 데코레이터를 빌려 쓴다).
     */
    struct GameNetSession::Shard
    {
        unique_ptr<UDPNetTransport> _transport{};
        unique_ptr<INetTransport>   _decorator{}; ///< `_transport` 를 감싼 전송(`setTransportDecorator` — 개발 도구의 회선 흉내). 있으면 호스트가 이것을 쓴다
        NetEndpoint                 _host{};
        NetEndpointThread           _thread{};
    };
} // namespace sw

namespace sw
{
    GameNetSession::GameNetSession()
        : _listShard{}
        , _peers{}
        , _router{}
        , _routerSubscription{}
        , _connectSecurity{}
        , _tickWriter{}
        , _connectFuture{}
        , _listEventScratch{}
        , _listReplicaStat{}
        , _serverAddress{}
        , _credentials{}
        , _latestServerTick{ 0 }
        , _listenPort{ 0 }
        , _role{ GameNetRole::None }
        , _bTicketBound{ SW_FALSE }
        , _bConnectFailureReported{ SW_FALSE }
        , _shutdownNotice{}
        , _bShutdownNoticeKnown{ SW_FALSE }
        , _bServerTickKnown{ SW_FALSE }
    {
    }

    GameNetSession::~GameNetSession()
    {
        shutdown();
    }

    GameNetLaunch GameNetSession::makeLaunchParams( GameNetSessionParams& outParams )
    {
        outParams           = GameNetSessionParams{};
        const bool bListen  = gv_netListenPort != 0;
        const bool bConnect = gv_netConnectAddress.empty() == false;
        if ( bListen == false && bConnect == false )
        {
            // 개발 덮어쓰기가 없으면 전용 서버는 서버 설정의 게임 포트를 연다. 리슨 호스트(App) · 테스트는 서버 문맥이 없어 열지 않는다.
            const ServerServiceContext* pHostContext = engine::areEngineServicesBound() ? engine::findSubsystem<ServerServiceContext>() : nullptr;
            if ( pHostContext == nullptr || pHostContext->_pServerConfig == nullptr )
                return GameNetLaunch::None;
            return makeServerParams( *pHostContext->_pServerConfig, outParams ) ? GameNetLaunch::Open : GameNetLaunch::Invalid;
        }
        // 두 역할을 함께 주면 어느 쪽을 바랐는지 모른다 — 한쪽을 골라 띄우지 않는다.
        if ( bListen && bConnect )
        {
            SW_LOG_ERROR( "Both gv_netListenPort and gv_netConnectAddress are set - pick one role" );
            return GameNetLaunch::Invalid;
        }
        if ( bListen )
        {
            const bool bPortInRange = 0 < gv_netListenPort && gv_netListenPort <= GameNetSessionInternal::kMaxPort;
            if ( bPortInRange == false )
            {
                SW_LOG_ERROR( "gv_netListenPort %# is not a UDP port", gv_netListenPort );
                return GameNetLaunch::Invalid;
            }
            outParams._role          = GameNetRole::Server;
            outParams._listenPort    = static_cast<uint16>( gv_netListenPort );
            outParams._bLoopbackOnly = gv_netLoopbackOnly;
            return GameNetLaunch::Open;
        }
        // 기동 때 한 번 막고 푼다 — HTTP 클라이언트의 해석 스레드와 같은 해석기다(이름 · hosts 파일 · 점 넷 숫자).
        const bool bResolved = PlatformSocketUtil::resolveAddress( gv_netConnectAddress, kDefaultPort, outParams._serverAddress );
        if ( bResolved == false || outParams._serverAddress.isValid() == false )
        {
            SW_LOG_ERROR( "gv_netConnectAddress '%#' did not resolve to an IPv4 address with a port", gv_netConnectAddress );
            return GameNetLaunch::Invalid;
        }
        outParams._role = GameNetRole::Client;
        return GameNetLaunch::Open;
    }

    bool GameNetSession::makeServerParams( const ServerConfig& config, GameNetSessionParams& outParams )
    {
        outParams = GameNetSessionParams{};
        NetAddress listenAddress;
        if ( NetAddress::parse( config._listenAddress, 0, listenAddress ) == false )
        {
            SW_LOG_ERROR( "Server config _listenAddress '%#' is not an IPv4 address - the game port stays closed", config._listenAddress.c_str() );
            return false;
        }
        // UDP 전송은 루프백 · 모든 인터페이스 두 가지로만 묶는다. 다른 주소를 조용히 모든 인터페이스로 바꾸지 않는다.
        const bool bLoopbackOnly = listenAddress._ipv4 == NetAddress::makeLoopback( 0 )._ipv4;
        const bool bAnyInterface = listenAddress._ipv4 == NetAddress::makeAnyInterface( 0 )._ipv4;
        if ( bLoopbackOnly == false && bAnyInterface == false )
        {
            SW_LOG_ERROR( "Server config _listenAddress '%#' - the game port binds only 0.0.0.0 or 127.0.0.1", config._listenAddress.c_str() );
            return false;
        }
        if ( config._gamePort < 0 || GameNetSessionInternal::kMaxPort < config._gamePort )
        {
            SW_LOG_ERROR( "Server config _gamePort %# is not a UDP port - the game port stays closed", config._gamePort );
            return false;
        }
        // 샤드 배치(한 포트 나누기는 리눅스만 · 샤드 여럿은 고정 포트)는 `isValidShardLayout` 한 곳이 거절한다 — 다른 플랫폼에서 연이은 포트로 바꿔 열지 않는다.
        outParams._role             = GameNetRole::Server;
        outParams._listenPort       = static_cast<uint16>( config._gamePort );
        outParams._shardCount       = config._gamePortShardCount;
        outParams._bLoopbackOnly    = bLoopbackOnly;
        outParams._bShardsSharePort = config._bGamePortReuse;
        if ( GameNetSessionInternal::isValidShardLayout( outParams ) == false )
        {
            outParams = GameNetSessionParams{};
            return false;
        }
        if ( config._bGameConnectTicket )
        {
            // 접속 티켓(OnlineKits F5): 게임 포트를 암호화하고 연결을 계정 키트가 낸 티켓의 세션 비밀에 묶는다. 인증기는 서버 서비스가 선 뒤에 묶이므로
            // 자리(`GameConnectSecurity`)를 끼워 두고, 묶이기 전에 온 접속은 거절한다. 암호 제공자가 없으면(게임 인스턴스가 안 넣었다) 열지 않는다.
            INetSecurityProvider* pProvider = _connectSecurity.findProvider();
            if ( pProvider == nullptr )
            {
                SW_LOG_ERROR( "Server config _bGameConnectTicket needs the network security provider (GameFramework) - the game port stays closed" );
                outParams = GameNetSessionParams{};
                return false;
            }
            outParams._hostSettings._security._mode           = NetSecurityMode::Encrypted;
            outParams._hostSettings._security._pProvider      = pProvider;
            outParams._hostSettings._security._pAuthenticator = &_connectSecurity;
        }
#if defined( SW_SHIPPING )
        // U4: Shipping 서버는 평문(`LanUnsecured`) 게임 포트를 루프백 밖에 열지 않는다. 공개 포트는 접속 티켓(암호화 + 세션 비밀 결속)이 있어야 한다.
        // 언리얼 · 유니티 NGO 는 평문이 기본이고 암호화가 선택이다 — 여기서는 기본을 닫고 LAN 파티 같은 평문 운영은 루프백 · 개발 빌드로만 남긴다.
        const bool bPlainPublicPort = outParams._hostSettings._security._mode == NetSecurityMode::LanUnsecured && outParams._bLoopbackOnly == false;
        if ( bPlainPublicPort )
        {
            SW_LOG_ERROR( "A shipping server opens a non-loopback game port only with _bGameConnectTicket (encrypted, session-bound) - the game port stays closed" );
            outParams = GameNetSessionParams{};
            return false;
        }
#endif
        return true;
    }

    bool GameNetSession::makeTicketClientParams( const NetAddress& serverAddress, const NetConnectCredentials& credentials, GameNetSessionParams& outParams ) const
    {
        outParams = GameNetSessionParams{};
        if ( serverAddress.isValid() == false || credentials._tokenSize <= 0 )
        {
            SW_LOG_ERROR( "A ticket client needs a server address and a game connect ticket" );
            return false;
        }
        INetSecurityProvider* pProvider = _connectSecurity.findProvider();
        if ( pProvider == nullptr )
        {
            SW_LOG_ERROR( "A ticket client needs the network security provider (GameFramework) - it does not connect without credentials instead" );
            return false;
        }
        outParams._role                              = GameNetRole::Client;
        outParams._serverAddress                     = serverAddress;
        outParams._credentials                       = credentials;
        outParams._hostSettings._security._mode      = NetSecurityMode::Encrypted;
        outParams._hostSettings._security._pProvider = pProvider;
        return true;
    }

    unique_ptr<GameNetSession::Shard> GameNetSession::createShard( const GameNetSessionParams& params, uint16 transportPort, int32 shardIndex )
    {
        unique_ptr<Shard> shard = make_unique<Shard>();
        shard->_transport       = make_unique<UDPNetTransport>();
        // 클라이언트는 서버 주소가 정한다 — 다른 기계의 서버면 그 답을 받아야 하므로 루프백에만 묶을 수 없다.
        const bool           bServer       = params._role == GameNetRole::Server;
        const bool           bLoopbackOnly = bServer ? params._bLoopbackOnly : params._serverAddress == NetAddress::makeLoopback( params._serverAddress._port );
        const UDPPortSharing sharing       = params._bShardsSharePort ? UDPPortSharing::ReusePort : UDPPortSharing::Exclusive;
        if ( shard->_transport->open( transportPort, bLoopbackOnly, sharing ) == false )
        {
            SW_LOG_ERROR( "Net session could not open UDP port %# (in use?)", transportPort );
            return nullptr;
        }
        INetTransport* pHostTransport = shard->_transport.get();
        // 건 데코레이터가 있으면 소켓을 감싼다(개발 도구의 회선 흉내 — 엔진은 무엇인지 모른다). nullptr 이면 그대로 쓴다.
        if ( _transportDecorator.isBound() )
            shard->_decorator = _transportDecorator( shardIndex, *shard->_transport );
        if ( shard->_decorator != nullptr )
            pHostTransport = shard->_decorator.get();
        shard->_host.initialize( pHostTransport, params._hostSettings );
        return shard;
    }

    bool GameNetSession::initialize( const GameNetSessionParams& params )
    {
        shutdown();
        if ( params._role == GameNetRole::None )
            return false;
        if ( GameNetSessionInternal::isValidShardLayout( params ) == false )
            return false;
        const bool bHasTicket = params._credentials._tokenSize > 0;
        if ( bHasTicket && ( params._role != GameNetRole::Client || params._hostSettings._security._mode != NetSecurityMode::Encrypted ) )
        {
            // 티켓은 암호화된 클라이언트 연결에만 실린다 — 평문 연결에 비밀을 실어 보내지 않는다.
            SW_LOG_ERROR( "Net session params carry a game connect ticket but are not an encrypted client - use makeTicketClientParams" );
            return false;
        }
        const bool           bServer = params._role == GameNetRole::Server;
        vector<NetEndpoint*> listHost;
        for ( int32 shardIndex = 0; shardIndex < params._shardCount; ++shardIndex )
        {
            // 샤드 n 은 첫 포트 + n(한 포트를 나누면 모두 첫 포트). 클라이언트는 운영체제가 고른 포트 하나다.
            const uint16      shardOffset   = params._bShardsSharePort ? uint16{ 0 } : static_cast<uint16>( shardIndex );
            const uint16      transportPort = bServer ? static_cast<uint16>( params._listenPort + shardOffset ) : uint16{ 0 };
            unique_ptr<Shard> shard         = createShard( params, transportPort, shardIndex );
            if ( shard == nullptr )
            {
                shutdown();
                return false;
            }
            listHost.push_back( &shard->_host );
            _listShard.push_back( std::move( shard ) );
        }
        if ( _peers.initialize( listHost, params._hostSettings._maxConnections ) == false )
        {
            shutdown();
            return false;
        }
        if ( bServer )
        {
            for ( const unique_ptr<Shard>& shard : _listShard )
            {
                if ( shard->_host.listen() == false )
                {
                    SW_LOG_ERROR( "Net session could not listen on UDP port %#", shard->_transport->getLocalAddress()._port );
                    shutdown();
                    return false;
                }
            }
            _listenPort = _listShard.front()->_transport->getLocalAddress()._port;
        }
        else
        {
            // 엔진 영역의 서버 틱은 클라이언트만 받는다 — 키트 · 게임 영역과 겹치지 않으므로 거절되면 이 라우터를 다른 누가 엔진 영역에 썼다는 결함이다.
            _routerSubscription = _router.registerHandler( this );
            if ( _routerSubscription.isActive() == false )
            {
                SW_LOG_ERROR( "Net session could not claim the engine message range - another handler holds it" );
                shutdown();
                return false;
            }
            _serverAddress = params._serverAddress;
            _credentials   = params._credentials;
            _bTicketBound  = bHasTicket ? SW_TRUE : SW_FALSE;
            _connectFuture = startClientConnect();
        }
        for ( const unique_ptr<Shard>& shard : _listShard )
        {
            if ( shard->_thread.start( &shard->_host ) == false )
            {
                SW_LOG_ERROR( "Net session could not start its network thread" );
                shutdown();
                return false;
            }
        }
        _role = params._role;
        if ( _role == GameNetRole::Client )
            SW_LOG_INFO( "Net session connecting to %#", _serverAddress.toString() );
        else if ( getShardCount() == 1 )
            SW_LOG_INFO( "Net session listening on UDP port %#", _listenPort );
        else if ( params._bShardsSharePort )
            SW_LOG_INFO( "Net session listening on UDP port %# (%# shards share the port)", _listenPort, getShardCount() );
        else
            SW_LOG_INFO( "Net session listening on UDP port %# (%# shards, ports %#-%#)", _listenPort, getShardCount(), _listenPort, _listenPort + getShardCount() - 1 );
        return true;
    }

    bool GameNetSession::openFromLaunch()
    {
        if ( isActive() )
            return true; // 리로드로 다시 선 게임 인스턴스 — 열린 세션과 연결을 이어받는다
        GameNetSessionParams params;
        switch ( makeLaunchParams( params ) )
        {
            case GameNetLaunch::None:
                return true;
            case GameNetLaunch::Open:
                return initialize( params ); // 실패는 initialize 가 알린다
            case GameNetLaunch::Invalid:
                return false;
        }
        return false;
    }

    void GameNetSession::shutdown()
    {
        if ( _role == GameNetRole::None && _listShard.empty() )
            return;
        // 스레드를 먼저 멈춘다 — 끊기 패킷은 disconnectAll 이 바로 보낸다(update 를 돌 스레드가 없어도).
        for ( const unique_ptr<Shard>& shard : _listShard )
        {
            shard->_thread.stop();
        }
        // 마지막 update 뒤에 쌓인 연결 사건도 로그에 남긴다 — 접속 직후 종료 명령이 같은 틱에 오면 update 없이 여기로 온다.
        // 처리기에는 나눠 주지 않는다(키트 처리기는 onShutdown 에서 내려갔다).
        if ( _role != GameNetRole::None )
        {
            _listEventScratch.clear();
            vector<NetEndpointEvent> listShardEvent;
            for ( int32 shardIndex = 0; shardIndex < getShardCount(); ++shardIndex )
            {
                listShardEvent.clear();
                _listShard[static_cast<size_t>( shardIndex )]->_host.drainEvents( listShardEvent );
                for ( NetEndpointEvent event : listShardEvent )
                {
                    event._connectionID = _peers.makeConnectionID( shardIndex, event._connectionID );
                    _listEventScratch.push_back( event );
                }
            }
            logEvents( EventPhase::Closing );
        }
        for ( const unique_ptr<Shard>& shard : _listShard )
        {
            shard->_host.disconnectAll();
        }
        if ( _role != GameNetRole::None )
            SW_LOG_INFO( "Net session closed (%#)", getEnumName( _role ) );
        _routerSubscription.reset();
        _peers.shutdown();
        _listShard.clear(); // 샤드마다 스레드 → 호스트 → 흉내 → 소켓 순으로 내린다(`Shard` 의 선언 순서)
        _connectFuture = TaskFuture<NetConnectResult>{};
        _credentials   = NetConnectCredentials{};
        _bTicketBound  = SW_FALSE;
        _listEventScratch.clear();
        _latestServerTick        = 0;
        _listenPort              = 0;
        _role                    = GameNetRole::None;
        _bConnectFailureReported = SW_FALSE;
        _bServerTickKnown        = SW_FALSE;
        _bShutdownNoticeKnown    = SW_FALSE;
    }

    void GameNetSession::update()
    {
        if ( _role == GameNetRole::None )
            return;
        reportConnectResult();
        _listEventScratch.clear();
        (void)_router.pump( _peers, nullptr, &_listEventScratch ); // 처리기가 없는 메시지는 버린다(맡은 키트가 없다)
        logEvents( EventPhase::Running );
    }

    void GameNetSession::logEvents( EventPhase phase ) const
    {
        for ( const NetEndpointEvent& event : _listEventScratch )
        {
            if ( _role == GameNetRole::Server )
            {
                if ( event._kind == NetEndpointEvent::Kind::Connected )
                    SW_LOG_INFO( "Net session client %# connected from %# (%# connected)", event._connectionID,
                                 _peers.getConnectionAddress( event._connectionID ).toString(), _peers.getConnectedCount() );
                else
                    SW_LOG_INFO( "Net session client %# disconnected (%#, %# connected)", event._connectionID, toString( event._reason ), _peers.getConnectedCount() );
            }
            else if ( event._kind == NetEndpointEvent::Kind::Disconnected && phase == EventPhase::Running )
            {
                SW_LOG_WARNING( "Net session lost the server %# (%#)", _serverAddress.toString(), toString( event._reason ) );
            }
            else if ( event._kind == NetEndpointEvent::Kind::Disconnected )
            {
                // 세션은 엔진 수명이라 종료의 끝에서 닫힌다 — 같이 끝나는 서버(에디터 netplay.stop)가 먼저 나간 끊김이 여기서 읽힌다. 이상이 아니다.
                SW_LOG_INFO( "Net session closing after the server %# left (%#)", _serverAddress.toString(), toString( event._reason ) );
            }
        }
    }

    int32 GameNetSession::getConnectedCount() const
    {
        return _role == GameNetRole::None ? 0 : _peers.getConnectedCount();
    }

    INetTransport* GameNetSession::findTransportDecorator( int32 shardIndex )
    {
        const bool bOutOfRange = shardIndex < 0 || getShardCount() <= shardIndex;
        return bOutOfRange ? nullptr : _listShard[static_cast<size_t>( shardIndex )]->_decorator.get();
    }

    NetEndpoint& GameNetSession::getClientHost()
    {
        return _listShard.front()->_host;
    }

    TaskFuture<NetConnectResult> GameNetSession::startClientConnect()
    {
        return _bTicketBound == SW_TRUE ? getClientHost().connectAsync( _serverAddress, _credentials ) : getClientHost().connectAsync( _serverAddress );
    }

    void GameNetSession::sendServerTick( uint64 tick )
    {
        if ( _role != GameNetRole::Server )
            return;
        BitWriter& writer = _tickWriter.begin( GameNetSessionMessage::kServerTick );
        writer.writeVarUint( tick );
        (void)_tickWriter.broadcast( _peers, NetChannelType::UnreliableSequenced ); // 잃어도 다음 틱이 더 새 번호를 싣는다
    }

    void GameNetSession::sendShutdownNotice( const GameShutdownNotice& notice )
    {
        if ( _role != GameNetRole::Server )
            return;
        BitWriter& writer = _tickWriter.begin( GameNetSessionMessage::kShutdownNotice );
        writer.writeBits( static_cast<uint32>( notice._reason ), 8 );
        writer.writeVarUint( notice._graceSeconds );
        const int32 sentCount = _tickWriter.broadcast( _peers, NetChannelType::ReliableOrdered ); // 끊기 전에 사유가 닿아야 한다
        SW_LOG_INFO( "Net session told %# clients the server is shutting down (reason %#, grace %# s)", sentCount, static_cast<uint32>( notice._reason ), notice._graceSeconds );
    }

    bool GameNetSession::tryGetShutdownNotice( GameShutdownNotice& outNotice ) const
    {
        if ( _role != GameNetRole::Client || _bShutdownNoticeKnown == SW_FALSE )
            return false;
        outNotice = _shutdownNotice;
        return true;
    }

    bool GameNetSession::tryGetServerTick( uint64& outTick ) const
    {
        if ( _role != GameNetRole::Client || _bServerTickKnown == SW_FALSE )
            return false;
        outTick = _latestServerTick;
        return true;
    }

    NetHandleResult GameNetSession::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        if ( context._kind == GameNetSessionMessage::kShutdownNotice )
        {
            const uint32 reasonValue  = static_cast<uint32>( body.readBits( 8 ) );
            const uint32 graceSeconds = body.readVarUint32();
            if ( body.hasOverflowed() || reasonValue > static_cast<uint32>( GameShutdownReason::ServerStopped ) )
                return NetHandleResult::Malformed;
            _shutdownNotice._reason       = static_cast<GameShutdownReason>( reasonValue );
            _shutdownNotice._graceSeconds = graceSeconds;
            _bShutdownNoticeKnown         = SW_TRUE;
            SW_LOG_INFO( "Net session client was told the server is shutting down (reason %#, grace %# s)", reasonValue, graceSeconds );
            return NetHandleResult::Handled;
        }
        const uint64 tick = body.readVarUint();
        if ( body.hasOverflowed() )
            return NetHandleResult::Malformed;
        // 순서 있는 비신뢰 채널이라 옛 번호는 이미 걸러졌다. 다시 띄운 서버의 작은 번호도 그대로 받는다(새 연결의 첫 번호다).
        _latestServerTick = tick;
        _bServerTickKnown = SW_TRUE;
        return NetHandleResult::Handled;
    }

    void GameNetSession::disconnectPeers()
    {
        if ( _role == GameNetRole::None )
            return;
        _connectFuture = TaskFuture<NetConnectResult>{}; // 붙는 중이던 것도 버린다 — 끊긴 채로 남는다
        for ( const unique_ptr<Shard>& shard : _listShard )
        {
            shard->_host.disconnectAll();
        }
        SW_LOG_INFO( "Net session disconnected its peers on request (%#)", getEnumName( _role ) );
    }

    bool GameNetSession::reconnect()
    {
        const bool bCannotReconnect = _role != GameNetRole::Client || _connectFuture.isValid() || getClientHost().getConnectedCount() > 0;
        if ( bCannotReconnect )
            return false;
        if ( _bTicketBound == SW_TRUE && _credentials._tokenSize <= 0 )
        {
            SW_LOG_WARNING( "Net session cannot reconnect to %# - the server refused the game connect ticket, ask for a new one", _serverAddress.toString() );
            return false;
        }
        _bConnectFailureReported = SW_FALSE;
        _connectFuture           = startClientConnect();
        SW_LOG_INFO( "Net session reconnecting to %#", _serverAddress.toString() );
        return true;
    }

    NetEmulationReport GameNetSession::makeConnectionReport() const
    {
        NetEmulationReport report;
        if ( _role == GameNetRole::None )
            return report;
        vector<int32> listConnection;
        _peers.collectConnected( listConnection );
        report._connectionCount = static_cast<int32>( listConnection.size() );
        for ( const int32 connectionID : listConnection )
        {
            NetConnectionStats stats;
            if ( _peers.getConnectionStats( connectionID, stats ) == false )
                continue;
            report._resentMessageCount += stats._resentMessageCount;
            report._measuredLossPercent = MathUtil::max( report._measuredLossPercent, stats._packetLoss * GameNetSessionInternal::kPercent );
            report._rttMilliseconds     = MathUtil::max( report._rttMilliseconds, stats._rtt * constant::kMillisecondsPerSecond );
        }
        return report;
    }

    NetStatsReport GameNetSession::makeStatsReport()
    {
        NetStatsReport report;
        if ( _role == GameNetRole::None )
            return report;
        vector<int32> listConnection;
        _peers.collectConnected( listConnection );
        report._connectionCount = static_cast<int32>( listConnection.size() );
        for ( const int32 connectionID : listConnection )
        {
            NetConnectionStats stats;
            if ( _peers.getConnectionStats( connectionID, stats ) == false )
                continue;
            report._sentPacketCount += stats._sentPacketCount;
            report._receivedPacketCount += stats._receivedPacketCount;
            report._resentMessageCount += stats._resentMessageCount;
            report._sentBytesPerSecond += stats._sentBandwidth * GameNetSessionInternal::kBytesPerKilobit;
            report._receivedBytesPerSecond += stats._receivedBandwidth * GameNetSessionInternal::kBytesPerKilobit;
            report._lossPercent     = MathUtil::max( report._lossPercent, stats._packetLoss * GameNetSessionInternal::kPercent );
            report._rttMilliseconds = MathUtil::max( report._rttMilliseconds, stats._rtt * constant::kMillisecondsPerSecond );
        }
        NetMessageKindTallyTable sentTally;
        _peers.addSentKindTally( sentTally );
        report.populateKinds( _router.getKindTally(), sentTally );
        // 상한 안에서 앞 절반은 지난 보고 뒤 늘어난 바이트가 큰 것(표의 초당 바이트와 같은 기준), 나머지는 누적 바이트가 큰 것.
        const auto increaseOf = []( const ReplicaStat& stat )
        {
            return stat._entry._byteCount > stat._reportedByteCount ? stat._entry._byteCount - stat._reportedByteCount : uint64{ 0 };
        };
        vector<const ReplicaStat*> listOrder;
        for ( const ReplicaStat& stat : _listReplicaStat )
        {
            listOrder.push_back( &stat );
        }
        const size_t cap = static_cast<size_t>( NetStatsReport::kMaxReplicaEntry );
        VectorUtil::sort( listOrder, [&increaseOf]( const ReplicaStat* pLeft, const ReplicaStat* pRight )
        {
            const uint64 leftIncrease  = increaseOf( *pLeft );
            const uint64 rightIncrease = increaseOf( *pRight );
            return leftIncrease != rightIncrease ? leftIncrease > rightIncrease : pLeft->_entry._name < pRight->_entry._name;
        } );
        const size_t                      recentCount = ( cap + 1 ) / 2;
        vector<const ReplicaStat*>        listPicked;
        unordered_set<const ReplicaStat*> uniqueSetPicked;
        for ( size_t index = 0; index < listOrder.size() && listPicked.size() < recentCount; ++index )
        {
            if ( increaseOf( *listOrder[index] ) > 0 && uniqueSetPicked.insert( listOrder[index] ).second )
                listPicked.push_back( listOrder[index] );
        }
        VectorUtil::sort( listOrder, []( const ReplicaStat* pLeft, const ReplicaStat* pRight )
        {
            return pLeft->_entry._byteCount != pRight->_entry._byteCount ? pLeft->_entry._byteCount > pRight->_entry._byteCount : pLeft->_entry._name < pRight->_entry._name;
        } );
        for ( size_t index = 0; index < listOrder.size() && listPicked.size() < cap; ++index )
        {
            if ( uniqueSetPicked.insert( listOrder[index] ).second )
                listPicked.push_back( listOrder[index] );
        }
        for ( const ReplicaStat* pStat : listPicked )
        {
            report._listReplicaEntry.push_back( pStat->_entry );
        }
        VectorUtil::sort( report._listReplicaEntry, []( const NetStatsReplicaEntry& left, const NetStatsReplicaEntry& right )
        {
            return left._byteCount != right._byteCount ? left._byteCount > right._byteCount : left._name < right._name;
        } );
        for ( ReplicaStat& stat : _listReplicaStat )
        {
            stat._reportedByteCount = stat._entry._byteCount;
        }
        return report;
    }

    bool GameNetSession::setReplicaStats( string_view name, uint64 updateCount, uint64 byteCount )
    {
        if ( NetStatsReport::isValidReplicaName( name ) == false )
        {
            SW_LOG_WARNING( "Net session: replica stats name '%#' is not [a-z0-9_.-] - the entry is not reported", name );
            return false;
        }
        for ( ReplicaStat& stat : _listReplicaStat )
        {
            if ( stat._entry._name == name )
            {
                stat._entry._updateCount = updateCount;
                stat._entry._byteCount   = byteCount;
                return true;
            }
        }
        if ( _listReplicaStat.size() >= kMaxReplicaSources )
        {
            SW_LOG_WARNING( "Net session: more than %# replica stats names - '%#' is not reported", static_cast<int32>( kMaxReplicaSources ), name );
            return false;
        }
        _listReplicaStat.push_back( ReplicaStat{
            NetStatsReplicaEntry{ string( name ), updateCount, byteCount },
            0
        } );
        return true;
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
            _connectFuture = startClientConnect();
        else
            _credentials = NetConnectCredentials{}; // 거절된 티켓은 다시 내밀지 않는다 — 새 티켓이 필요하다
    }
} // namespace sw
