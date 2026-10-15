#include "pch.h"

#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Rule/MatchModeCatalog.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchQueueService.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchmakingServer.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/PartyLobbyService.h"

// 매칭 키트의 서버 조립 — 파티 · 로비 · 대기열(권한 매처는 모드마다 캐시 임대) · 바인딩을 올린다. 다른 서버의 파티원에게 결과를 알리려고 접속 상태를 받는다.
// 전용 서버 배정은 디렉터리에 등록된 서버(런타임이 `_publicAddress` 로 등록한다)에서 고른다.
// 옵션:
//   catalog       모드 정의(리소스 경로, `<MatchModeCatalog>`).
//   mode          `<id>:<팀 수>x<팀 인원>`(예: `duo:2x2`) — 데이터 파일 없이 도는 도구 · 테스트용(여러 번 적는다).
//   maxPartySize  파티 최대 인원(기본 4).

namespace sw
{
    namespace
    {
        struct MatchmakingOnlineServerKitInternal
        {
            static constexpr int32 kDefaultMaxPartySize = 4;
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
                , _maxPartySize{ MatchmakingOnlineServerKitInternal::kDefaultMaxPartySize }
                , _bStarted{ false }
            {
            }

            bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
            {
                string         catalogPath;
                vector<string> listCompact;
                if ( entry.readOptions( {
                                            {     "catalog",   catalogPath},
                                            {        "mode",   listCompact},
                                            {"maxPartySize", _maxPartySize}
                },
                                        outError ) == false )
                    return false;
                if ( catalogPath.empty() == false )
                {
                    if ( _catalog.loadFromResource( catalogPath ) == false )
                    {
                        outError = "the match mode catalog '" + catalogPath + "' could not be read";
                        return false;
                    }
                    _listMode = _catalog.getModes();
                }
                for ( const string& compact : listCompact )
                {
                    MatchModeDefinition mode;
                    if ( MatchModeCatalog::parseCompactMode( compact, mode ) == false )
                    {
                        outError = "mode option '" + compact + "' must be <id>:<team count>x<team size>";
                        return false;
                    }
                    _listMode.push_back( std::move( mode ) );
                }
                if ( _listMode.empty() )
                {
                    outError = "the matchmaking kit needs at least one mode (catalog or mode option)";
                    return false;
                }
                if ( registerHostService( context, entry, &_server, outError ) == false )
                    return false;
                return true;
            }

            bool start( OnlineServerKitContext& context, string& outError ) override
            {
                if ( context._pRouter == nullptr )
                {
                    outError = "matchmaking needs a cache (authority leases and server lists)";
                    return false;
                }
                _partyLobby.initialize( context._pRouter, context._serverID, _maxPartySize );
                MatchQueueDependencies dependencies;
                dependencies._pRouter     = context._pRouter;
                dependencies._pBus        = context._pBus;
                dependencies._pPartyLobby = &_partyLobby;
                dependencies._serverID    = context._serverID;
                _queue.initialize( dependencies, _listMode );
                _server.initialize( &_partyLobby, &_queue, nullptr, context._pPresence );
                _bStarted = true;
                return true;
            }

            void stop() override
            {
                if ( _bStarted == false )
                    return;
                _server.shutdown();
                _queue.shutdown();
                _partyLobby.shutdown();
                _bStarted = false;
            }

        private:
            MatchModeCatalog            _catalog;
            vector<MatchModeDefinition> _listMode;
            PartyLobbyService           _partyLobby;
            MatchQueueService           _queue;
            MatchmakingServer           _server;
            int32                       _maxPartySize;
            bool                        _bStarted;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( MatchmakingOnlineServerKit, "matchmaking", OnlineServerKitOrder::kMatchmaking );
} // namespace sw
