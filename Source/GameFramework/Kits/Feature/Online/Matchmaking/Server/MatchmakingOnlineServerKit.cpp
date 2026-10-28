#include "pch.h"

#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Rule/MatchModeCatalog.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchQueueService.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchServerAgent.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchmakingServer.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/PartyLobbyService.h"

// 매칭 키트의 서버 조립 — 파티 · 로비 · 대기열(권한 매처는 모드마다 캐시 임대) · 바인딩을 만든다. 다른 서버의 파티원에게 결과를 알리려고 접속 상태를 받는다.
// 전용 서버 배정은 디렉터리에 등록된 서버(런타임이 `_publicAddress` 로 등록한다)에서 고른다.
// 옵션:
//   catalog       모드 정의(리소스 경로, `<MatchModeCatalog>`).
//   mode          `<id>:<팀 수>x<팀 인원>`(예: `duo:2x2`) — 데이터 파일 없이 도는 도구 · 테스트용(여러 번 적는다).
//   maxPartySize  파티 최대 인원(기본 4).
//   reconnectGraceMs  떠난 계정을 파티와 로비에 남겨 둘 밀리초(기본 60000, 0 이면 바로 뺀다).
//   gameServer    이 서버가 매칭이 배정하는 전용 게임 서버다(기본 false). 배정 주제(`mm.assign.<서버 id>`)를 받아 배정받은 계정만
//                 게임 포트에 들인다 — 받아들이기 정책(`IConnectAdmission`)을 문맥에 내놓고 계정 키트의 `gameConnect` 가 쓴다(OnlineKits F5).
// 채팅 키트가 있으면 파티 채널 멤버를 맞춘다.

namespace sw
{
    namespace
    {
        struct MatchmakingOnlineServerKitInternal
        {
            static constexpr int32 kDefaultMaxPartySize     = 4;
            static constexpr int32 kDefaultReconnectGraceMs = 60000;
        };

        /**
         * @class MatchmakingOnlineServerKit
         * @brief 매칭 키트의 서버 조립입니다.
         */
        class MatchmakingOnlineServerKit final : public IOnlineServerKit
        {
        public:
            MatchmakingOnlineServerKit()
                : _catalog{}
                , _listMode{}
                , _partyLobby{}
                , _queue{}
                , _server{}
                , _agent{}
                , _maxPartySize{ MatchmakingOnlineServerKitInternal::kDefaultMaxPartySize }
                , _reconnectGraceMs{ MatchmakingOnlineServerKitInternal::kDefaultReconnectGraceMs }
                , _bGameServer{ false }
            {
            }

            [[nodiscard]] bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
            {
                string         catalogPath;
                vector<string> listCompact;
                if ( entry.readOptions( {
                                            {         "catalog",       catalogPath},
                                            {            "mode",       listCompact},
                                            {    "maxPartySize",     _maxPartySize},
                                            {"reconnectGraceMs", _reconnectGraceMs},
                                            {      "gameServer",      _bGameServer}
                },
                                        outError ) == false )
                    return false;
                if ( catalogPath.empty() == false )
                {
                    if ( _catalog.loadFromResource( ResourceID{ catalogPath } ) == false )
                    {
                        outError = "the match mode catalog '" + catalogPath + "' could not be read";
                        return false;
                    }
                    _listMode = _catalog.getModes();
                }
                for ( const string& compact : listCompact )
                {
                    MatchModeDefinition mode;
                    string              modeError;
                    if ( MatchModeCatalog::parseCompactMode( compact, mode, modeError ) == false )
                    {
                        outError = "mode option '" + compact + "': " + modeError;
                        return false;
                    }
                    _listMode.push_back( std::move( mode ) );
                }
                if ( _listMode.empty() )
                {
                    outError = "the matchmaking kit needs at least one mode (catalog or mode option)";
                    return false;
                }
                if ( _reconnectGraceMs < 0 )
                {
                    outError = "matchmaking option reconnectGraceMs must be 0 or more";
                    return false;
                }
                if ( registerHostService( context, entry, &_server, outError ) == false )
                    return false;
                if ( _bGameServer == false )
                    return true;
                // 배정받은 계정만 게임 포트에 들이는 정책이다. 계정 키트의 `gameConnect` 가 `start` 에서 찾는다.
                _agent.initialize( context._serverDescriptor );
                return exposeInterface<IConnectAdmission>( context, entry, _agent, outError );
            }

            bool start( OnlineServerKitContext& context, string& outError ) override
            {
                if ( context._pRouter == nullptr )
                {
                    outError = "matchmaking needs a cache (authority leases and server lists)";
                    return false;
                }
                if ( _partyLobby.initialize( context._pRouter, context._serverID, _maxPartySize ) == false )
                {
                    outError = "matchmaking option maxPartySize is out of range (the party lobby service logged the range)";
                    return false;
                }
                MatchQueueDependencies dependencies;
                dependencies._pRouter     = context._pRouter;
                dependencies._pBus        = context._pBus;
                dependencies._pPartyLobby = &_partyLobby;
                dependencies._serverID    = context._serverID;
                dependencies._pStatSource = context._kits.find<const IAccountStatSource>(); // 실력 모드의 통계이다. 순위표 키트가 없으면 실력 모드는 기동 오류다
                if ( _queue.initialize( dependencies, _listMode ) == false )
                {
                    outError = "matchmaking has a rated mode but the leaderboard kit (stat source) is not assembled";
                    _partyLobby.shutdown(); // 실패한 start 는 `stop` 이 보지 않으므로 세운 파티 · 로비를 여기서 내린다
                    return false;
                }
                _server.initialize( &_partyLobby, &_queue, _bGameServer ? &_agent : nullptr, context._kits.find<IAccountPresence>() ); // 배정 구독 · 시한은 서버가 돈다
                _server.setReconnectGrace( _reconnectGraceMs );
                _server.setGroupChannels( context._kits.find<IGroupChannelMembership>() );
                return true;
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
            MatchModeCatalog            _catalog;
            vector<MatchModeDefinition> _listMode;
            PartyLobbyService           _partyLobby;
            MatchQueueService           _queue;
            MatchmakingServer           _server;
            MatchServerAgent            _agent; ///< `gameServer` 일 때만 쓴다 — 배정받은 계정 목록(접속 인증기가 네트워크 스레드에서 묻는다)
            int32                       _maxPartySize;
            int32                       _reconnectGraceMs;
            bool                        _bGameServer;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( MatchmakingOnlineServerKit, OnlineServerKitName::kMatchmaking );
} // namespace sw
