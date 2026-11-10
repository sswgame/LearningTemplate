#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/MatchmakingKitConfig.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Rule/MatchModeCatalog.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchQueueService.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchServerAgent.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchmakingServer.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/PartyLobbyService.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"

// 매칭 키트의 서버 조립 — 파티 · 로비 · 대기열(권한 매처는 모드마다 캐시 임대) · 바인딩을 만든다. 다른 서버의 파티원에게 결과를 알리려고 접속 상태를 받는다.
// 전용 서버 배정은 디렉터리에 등록된 서버(런타임이 `_publicAddress` 로 등록한다)에서 고른다.
// 설정은 `MatchmakingKitConfig` 이다.
// 채팅 키트가 있으면 파티 채널 멤버를 맞춘다.

namespace sw
{
    namespace
    {
        /**
         * @class MatchmakingOnlineServerKit
         * @brief 매칭 키트의 서버 조립입니다.
         */
        class MatchmakingOnlineServerKit final : public IOnlineServerKit
        {
        public:
            MatchmakingOnlineServerKit()
                : _pCatalog{}
                , _listMode{}
                , _partyLobby{}
                , _queue{}
                , _server{}
                , _agent{}
                , _maxPartySize{ 0 }
                , _reconnectGraceMs{ 0 }
                , _bGameServer{ SW_FALSE }
            {
            }

            [[nodiscard]] expected<void> initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry ) override
            {
                MatchmakingKitConfig kitConfig;
                if ( expected<void> result = entry.readSettings( kitConfig ); result.has_value() == false )
                    return result;
                const string& catalogPath = kitConfig._catalog;
                _maxPartySize             = kitConfig._maxPartySize;
                _reconnectGraceMs         = kitConfig._reconnectGraceMs;
                _bGameServer              = kitConfig._bGameServer ? SW_TRUE : SW_FALSE;
                if ( catalogPath.empty() == false )
                {
                    _pCatalog = SharedAssetCache<MatchModeCatalog>::acquire( catalogPath );
                    if ( _pCatalog == nullptr )
                        return unexpected( Error{ ErrorKind::NotFound, "the match mode catalog could not be read", catalogPath } );
                    _listMode = _pCatalog->getModes();
                }
                _listMode.insert( _listMode.end(), kitConfig._listMode.begin(), kitConfig._listMode.end() );
                if ( _listMode.empty() )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the matchmaking kit needs at least one mode (_catalog or _listMode)" } );
                if ( expected<void> result = registerServerService( context, entry, &_server ); result.has_value() == false )
                    return result;
                if ( _bGameServer == SW_FALSE )
                    return {};
                // 배정받은 계정만 게임 포트에 들이는 정책이다. 계정 키트의 `gameConnect` 가 `start` 에서 찾는다.
                _agent.initialize( context._serverDescriptor );
                return exposeInterface<IConnectAdmission>( context, entry, _agent );
            }

            expected<void> start( OnlineServerKitContext& context ) override
            {
                if ( context._pRouter == nullptr )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "matchmaking needs a cache (authority leases and server lists)" } );
                if ( _partyLobby.initialize( context._pRouter, context._serverID, _maxPartySize ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "matchmaking setting _maxPartySize is out of range (the party lobby service logged the range)" } );
                MatchQueueDependencies dependencies;
                dependencies._pRouter     = context._pRouter;
                dependencies._pBus        = context._pBus;
                dependencies._pPartyLobby = &_partyLobby;
                dependencies._serverID    = context._serverID;
                dependencies._pStatSource = context._kits.find<IAccountStatSource>(); // 실력 모드의 통계이다. 순위표 키트가 없으면 실력 모드는 기동 오류다
                if ( _queue.initialize( dependencies, _listMode ) == false )
                {
                    _partyLobby.shutdown(); // 실패한 start 는 `stop` 이 보지 않으므로 세운 파티 · 로비를 여기서 내린다
                    return unexpected( Error{ ErrorKind::InvalidArgument, "matchmaking has a rated mode but the leaderboard kit (stat source) is not assembled" } );
                }
                _server.initialize( &_partyLobby, &_queue, _bGameServer == SW_TRUE ? &_agent : nullptr, context._kits.find<IAccountPresence>(), context._pMetricRegistry ); // 배정 구독 · 시한은 서버가 돈다
                _server.setReconnectGrace( _reconnectGraceMs );
                _server.setGroupChannels( context._kits.find<IGroupChannelMembership>() );
                return {};
            }

            /** @brief 종료 드레인이 기다릴 진행 중인 일 — 큐에 선 이 서버의 티켓과 파티 · 로비 갱신. */
            int32 getOpenWorkCount() const override { return _queue.getLocalTicketCount() + _partyLobby.getPendingCount(); }

            void beginShutdown( const ServerShutdownNotice& notice ) override
            {
                // 새 일을 막는 길은 없고(클라이언트는 `kShutdownNotice` 로 먼저 사유를 받는다) 드레인이 끝나기를 기다리는 일의 수만 남긴다 — 운영자가 왜 오래 걸리는지 로그로 본다.
                SW_LOG_INFO_ID( "MatchmakingOnlineServerKit.MatchmakingKitShutdownDrain", "Matchmaking kit: shutdown drain starts with %# open job(s), grace %# s", getOpenWorkCount(), notice._graceSeconds );
            }

            void stop() override
            {
                if ( isStarted() == false )
                    return;
                _server.shutdown();
                _queue.shutdown();
                _partyLobby.shutdown();
            }

            span<const utf8* const> getOptionalKits() const override
            {
                // 접속 상태, 실력 모드의 통계, 파티 채널 멤버
                static constexpr const utf8* kArrKit[] = { OnlineServerKitName::kAccount, OnlineServerKitName::kLeaderboard, OnlineServerKitName::kChat };
                return kArrKit;
            }

        private:
            shared_ptr<const MatchModeCatalog> _pCatalog; ///< 설정 `_catalog` 의 공유 캐시 한 벌
            vector<MatchModeDef>               _listMode;
            PartyLobbyService                  _partyLobby;
            MatchQueueService                  _queue;
            MatchmakingServer                  _server;
            MatchServerAgent                   _agent; ///< `gameServer` 일 때만 쓴다 — 배정받은 계정 목록(접속 인증기가 네트워크 스레드에서 묻는다)
            int32                              _maxPartySize;
            int32                              _reconnectGraceMs;
            uint8                              _bGameServer : 1;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( MatchmakingOnlineServerKit, OnlineServerKitName::kMatchmaking );
} // namespace sw
