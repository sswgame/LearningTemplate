#include "pch.h"

#include "OnlineLoadBot/LoadBotLocalServer.h"

#include "Core/Network/Transport/IStreamTransport.h"

#include "Engine/Network/EngineNetSecurity.h"

#include "GameFramework/Base/Online/Bus/LocalServerBus.h"
#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"
#include "GameFramework/Base/Online/Directory/ServerRegistration.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Online/Server/Account/NetSecurityLoginCrypto.h"
#include "GameFramework/Kits/Online/Server/Account/Service/AccountServer.h"
#include "GameFramework/Kits/Online/Server/Account/Service/LoginService.h"
#include "GameFramework/Kits/Online/Server/Chat/ChatServer.h"
#include "GameFramework/Kits/Online/Server/Chat/ChatService.h"
#include "GameFramework/Kits/Online/Server/Leaderboard/LeaderboardServer.h"
#include "GameFramework/Kits/Online/Server/Leaderboard/LeaderboardService.h"
#include "GameFramework/Kits/Online/Server/LiveOps/LiveOpsServer.h"
#include "GameFramework/Kits/Online/Server/LiveOps/LiveOpsService.h"
#include "GameFramework/Kits/Online/Server/Matchmaking/Service/MatchQueueService.h"
#include "GameFramework/Kits/Online/Server/Matchmaking/Service/MatchmakingServer.h"
#include "GameFramework/Kits/Online/Server/Matchmaking/Service/PartyLobbyService.h"
#include "GameFramework/Kits/Online/Server/ServerDirectory/ServerDirectoryServer.h"
#include "GameFramework/Kits/Online/Server/ServerDirectory/ServerDirectoryService.h"
#include "GameFramework/Kits/Online/Server/Social/GuildService.h"
#include "GameFramework/Kits/Online/Server/Social/SocialServer.h"
#include "GameFramework/Kits/Online/Server/Social/SocialService.h"

namespace sw
{
    namespace
    {
        struct LoadBotLocalServerInternal
        {
            static constexpr int32  kUnlimitedAttemptBurst = 1000000; ///< 봇은 한 주소에서 온다 — 주소마다 제한을 사실상 끈다
            static constexpr uint32 kLightHashMemoryKiB    = 256;     ///< 시험용 가벼운 Argon2id
            static constexpr uint32 kLightHashIterations   = 1;
            static constexpr int32  kMatchTeamCount        = 2;
            static constexpr uint64 kGameServerId          = 0x100;
            static constexpr uint16 kGameServerPort        = 7777;
            static constexpr uint8  kDevTicketKeyByte      = 0x5A; ///< 개발용 표 주 키(모든 바이트 같은 값) — 이 조립 밖으로 나가지 않는다

            static MatchModeDefinition makeMode( string_view modeId, int32 teamSize )
            {
                MatchModeDefinition mode;
                mode._modeId    = string( modeId );
                mode._teamCount = kMatchTeamCount;
                mode._teamSize  = teamSize;
                return mode;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    /** @brief 조립의 부품 — 선언 순서가 만들 때 쓰는 순서다(데이터 → 앞 → 서비스 → 호스트). 내리기는 `shutdown` 이 순서를 정한다. */
    struct LoadBotLocalServer::Parts
    {
        MemoryServiceDatabase   _database;
        MemoryEphemeralDatabase _cacheDatabase;
        LocalServerBusHub       _busHub;
        MemoryServiceStore      _store;
        MemoryEphemeralStore    _cache;
        LocalServerBus          _bus;
        NetSecurityLoginCrypto  _crypto;
        LoginService            _loginService;
        AccountServer           _accountServer;
        ServerDirectoryService  _directoryService;
        ServerDirectoryServer   _directoryServer;
        ChatService             _chatService;
        ChatServer              _chatServer;
        SocialService           _socialService;
        GuildService            _guildService;
        SocialServer            _socialServer;
        LeaderboardService      _leaderboardService;
        LeaderboardServer       _leaderboardServer;
        PartyLobbyService       _partyLobby;
        MatchQueueService       _matchQueue;
        MatchmakingServer       _matchmakingServer;
        LiveOpsService          _liveOpsService;
        LiveOpsServer           _liveOpsServer;
        ServerRegistration      _gameServer;
        OnlineServiceHost       _host;

        explicit Parts( uint64 serverId )
            : _database{}
            , _cacheDatabase{}
            , _busHub{}
            , _store{ &_database }
            , _cache{ &_cacheDatabase }
            , _bus{ &_busHub, serverId }
            , _crypto{ &EngineNetSecurity::getProvider() }
            , _loginService{}
            , _accountServer{}
            , _directoryService{}
            , _directoryServer{}
            , _chatService{}
            , _chatServer{}
            , _socialService{}
            , _guildService{}
            , _socialServer{}
            , _leaderboardService{}
            , _leaderboardServer{}
            , _partyLobby{}
            , _matchQueue{}
            , _matchmakingServer{}
            , _liveOpsService{}
            , _liveOpsServer{}
            , _gameServer{}
            , _host{}
        {
        }
    };
} // namespace sw

namespace sw
{
    LoadBotLocalServer::LoadBotLocalServer()
        : _parts{}
        , _bInitialized{ SW_FALSE }
    {
    }

    LoadBotLocalServer::~LoadBotLocalServer() { shutdown(); }

    bool LoadBotLocalServer::initialize( IStreamTransport* pTransport, const LoadBotLocalServerSettings& settings, string& outError )
    {
        using Internal = LoadBotLocalServerInternal;
        SW_ASSERT( _bInitialized == SW_FALSE );
        _parts        = make_unique<Parts>( settings._serverId );
        Parts& parts  = *_parts;
        _bInitialized = SW_TRUE; // 실패해도 `shutdown` 이 올린 것까지 내린다

        LoginSettings loginSettings;
        loginSettings._attemptBurst = Internal::kUnlimitedAttemptBurst;
        if ( settings._bLightPasswordHash == SW_TRUE )
        {
            loginSettings._passwordHashParams._memoryKiB      = Internal::kLightHashMemoryKiB;
            loginSettings._passwordHashParams._iterationCount = Internal::kLightHashIterations;
        }
        uint8 arrTicketKey[LoginTicketAuthority::kMasterKeySize];
        Memory::set( arrTicketKey, Internal::kDevTicketKeyByte, sizeof( arrTicketKey ) );
        parts._loginService.initialize( &parts._store, &parts._crypto, loginSettings, arrTicketKey );
        parts._accountServer.initialize( &parts._loginService, AccountServerSettings{} );

        // 바인딩은 호스트 initialize 전에 올린다(메서드 영역 등록).
        const bool bRegistered = parts._host.registerService( &parts._accountServer ) && parts._host.registerService( &parts._directoryServer ) &&
                                 parts._host.registerService( &parts._chatServer ) && parts._host.registerService( &parts._socialServer ) &&
                                 parts._host.registerService( &parts._leaderboardServer ) && parts._host.registerService( &parts._matchmakingServer ) &&
                                 parts._host.registerService( &parts._liveOpsServer );
        if ( bRegistered == false )
        {
            outError = "load bot local server could not register its online services";
            return false;
        }

        OnlineServiceHostSettings hostSettings;
        hostSettings._transportSettings._ioThreadCount  = 0;
        hostSettings._transportSettings._maxConnections = settings._maxConnections;
        hostSettings._listenAddress                     = NetAddress::makeLoopback( settings._port );
        hostSettings._pServiceStore                     = &parts._store;
        hostSettings._pEphemeralStore                   = &parts._cache;
        hostSettings._pServerBus                        = &parts._bus;
        hostSettings._requestBurstPerRemote             = Internal::kUnlimitedAttemptBurst;
        hostSettings._requestBurstPerAccount            = Internal::kUnlimitedAttemptBurst;
        if ( parts._host.initialize( pTransport, hostSettings, outError ) == false )
            return false;

        EphemeralStoreRouter* pRouter   = parts._host.getEphemeralRouter();
        IAccountPresence*     pPresence = parts._accountServer.getPresence();

        ServerDirectoryDependencies directoryDependencies;
        directoryDependencies._pStore  = &parts._store;
        directoryDependencies._pRouter = pRouter;
        directoryDependencies._pBus    = &parts._bus;
        ServerDirectorySettings directorySettings;
        directorySettings._listServerKind.push_back( "game" );
        parts._directoryService.initialize( directoryDependencies, directorySettings );
        parts._directoryServer.initialize( &parts._directoryService );

        ChatServiceDependencies chatDependencies;
        chatDependencies._pStore     = &parts._store;
        chatDependencies._pBus       = &parts._bus;
        chatDependencies._pPresence  = pPresence;
        chatDependencies._pDirectory = &parts._loginService;
        chatDependencies._serverId   = settings._serverId;
        if ( parts._chatService.initialize( chatDependencies, ChatSettings{} ) == false )
        {
            outError = "load bot local server could not start the chat service";
            return false;
        }
        parts._chatServer.initialize( &parts._chatService );

        SocialServiceDependencies socialDependencies;
        socialDependencies._pStore  = &parts._store;
        socialDependencies._pRouter = pRouter;
        socialDependencies._pBus    = &parts._bus;
        parts._socialService.initialize( socialDependencies );
        parts._guildService.initialize( &parts._store );
        parts._socialServer.initialize( &parts._socialService, &parts._guildService, pPresence );

        LeaderboardServiceDependencies leaderboardDependencies;
        leaderboardDependencies._pStore  = &parts._store;
        leaderboardDependencies._pRouter = pRouter;
        parts._leaderboardService.initialize( leaderboardDependencies );
        vector<string> listBoardId = settings._listBoardId;
        if ( listBoardId.empty() )
            listBoardId.push_back( "kills" );
        for ( const string& boardId : listBoardId )
        {
            LeaderboardDefinition board;
            board._boardId = boardId;
            if ( parts._leaderboardService.registerBoard( board ) == false )
            {
                outError = "load bot local server rejected leaderboard '" + boardId + "'";
                return false;
            }
        }
        parts._leaderboardServer.initialize( &parts._leaderboardService, &parts._loginService, pPresence );

        parts._partyLobby.initialize( pRouter, settings._serverId );
        MatchQueueDependencies queueDependencies;
        queueDependencies._pRouter     = pRouter;
        queueDependencies._pBus        = &parts._bus;
        queueDependencies._pPartyLobby = &parts._partyLobby;
        queueDependencies._serverId    = settings._serverId;
        parts._matchQueue.initialize( queueDependencies, vector<MatchModeDefinition>{ Internal::makeMode( "solo", 1 ), Internal::makeMode( "duo", 2 ) } );
        parts._matchmakingServer.initialize( &parts._partyLobby, &parts._matchQueue, nullptr, pPresence );

        LiveOpsDependencies liveOpsDependencies;
        liveOpsDependencies._pStore = &parts._store;
        liveOpsDependencies._pBus   = &parts._bus;
        parts._liveOpsService.initialize( liveOpsDependencies );
        parts._liveOpsServer.initialize( &parts._liveOpsService );

        ServerDescriptor gameServer;
        gameServer._serverId = Internal::kGameServerId;
        gameServer._kind     = "game";
        gameServer._region   = settings._region;
        gameServer._address  = "127.0.0.1";
        gameServer._port     = Internal::kGameServerPort;
        gameServer._capacity = settings._gameServerCapacity;
        if ( parts._gameServer.initialize( pRouter, gameServer ) == false )
        {
            outError = "load bot local server could not register its game server";
            return false;
        }
        parts._gameServer.setState( ServerState::Open );
        return true;
    }

    void LoadBotLocalServer::shutdown()
    {
        if ( _bInitialized == SW_FALSE )
            return;
        _bInitialized = SW_FALSE;
        Parts& parts  = *_parts;
        // 키트 — 저장 일을 거두고 캐시 · 접속 상태 요청을 취소하고 구독을 푼다(호스트 · 저장소가 살아 있을 때).
        parts._gameServer.shutdown();
        parts._liveOpsServer.shutdown();
        parts._liveOpsService.shutdown();
        parts._matchmakingServer.shutdown();
        parts._matchQueue.shutdown();
        parts._partyLobby.shutdown();
        parts._leaderboardServer.shutdown();
        parts._leaderboardService.shutdown();
        parts._socialServer.shutdown();
        parts._guildService.shutdown();
        parts._socialService.shutdown();
        parts._chatServer.shutdown();
        parts._chatService.shutdown();
        parts._directoryServer.shutdown();
        parts._directoryService.shutdown();
        // 호스트 → 저장소 → (남은 완료가 로그인 서비스를 부른다) → 계정
        parts._host.shutdown();
        parts._store.shutdown();
        (void)parts._store.pollCompletions();
        parts._accountServer.shutdown();
        parts._loginService.shutdown();
        parts._cache.shutdown();
        _parts.reset();
    }

    void LoadBotLocalServer::tick( int64 nowMs )
    {
        if ( _bInitialized == SW_FALSE )
            return;
        _parts->_host.tick( nowMs );
        _parts->_gameServer.tick( nowMs );
    }

    OnlineServiceHost& LoadBotLocalServer::getHost() { return _parts->_host; }

    uint16 LoadBotLocalServer::getListenPort() const { return _bInitialized == SW_TRUE ? _parts->_host.getListenPort() : 0; }
} // namespace sw
