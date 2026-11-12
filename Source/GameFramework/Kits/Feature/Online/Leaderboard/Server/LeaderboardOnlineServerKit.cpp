#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardCatalog.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardKitConfig.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardServer.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardService.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"

// 순위표 키트의 서버 조립 — 순위표 서비스와 바인딩을 만들고, 보드 · 업적 정의 데이터를 등록하며, 시즌 정산을 예약 작업에 건다.
// 표시 이름과 접속 여부는 계정 키트, 다른 서버 계정의 업적 알림은 접속 상태를 받는다.
// 설정은 `LeaderboardKitConfig` 이다.

namespace sw
{
    namespace
    {
        /**
         * @class LeaderboardOnlineServerKit
         * @brief 순위표 키트의 서버 조립입니다.
         */
        class LeaderboardOnlineServerKit final : public IOnlineServerKit
        {
        public:
            LeaderboardOnlineServerKit()
                : _pCatalog{}
                , _listInlineBoardID{}
                , _service{}
                , _server{}
            {
            }

            [[nodiscard]] expected<void> initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry ) override
            {
                LeaderboardKitConfig kitConfig;
                if ( expected<void> result = entry.readSettings( kitConfig ); result.has_value() == false )
                    return result;
                const string& catalogPath = kitConfig._catalog;
                _listInlineBoardID        = std::move( kitConfig._listBoardID );
                _pCatalog                 = catalogPath.empty() ? nullptr : SharedAssetCache<LeaderboardCatalog>::acquire( catalogPath );
                if ( catalogPath.empty() == false && _pCatalog == nullptr )
                    return unexpected( Error{ ErrorKind::NotFound, "the leaderboard catalog could not be read", catalogPath } );
                if ( catalogPath.empty() && _listInlineBoardID.empty() )
                    SW_LOG_WARNING_ID( "LeaderboardOnlineServerKit.LeaderboardKitNeitherCatalog", "Leaderboard kit: neither _catalog nor _listBoardID - no boards are registered" );
                // 매칭 키트가 실력 모드의 통계를 읽는다. 접속한 계정의 통계만 메모리에 있다.
                return registerServerService( context, entry, &_server )
                    .and_then( [&]
                { return exposeInterface<IAccountStatSource>( context, entry, _service ); } )
                    .and_then( [&]
                { return addTTLRule( context, entry, LeaderboardService::makeScoreTTLRule( kitConfig._pastScoreTTL.getExpireAfterMs() ), kitConfig._pastScoreTTL ); } )
                    .and_then( [&]
                { return addTTLRule( context, entry, LeaderboardService::makeSeasonResultTTLRule( kitConfig._seasonResultTTL.getExpireAfterMs() ), kitConfig._seasonResultTTL ); } );
            }

            expected<void> start( OnlineServerKitContext& context ) override
            {
                if ( context._pRouter == nullptr )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the leaderboard needs a cache (ranks live in sorted sets)" } );
                LeaderboardServiceDependencies dependencies;
                dependencies._pStore  = context._pStore;
                dependencies._pRouter = context._pRouter;
                _service.initialize( dependencies );
                _service.setScheduler( context._pScheduler ); // 보드 등록 전 — 시즌 보드의 정산 작업이 예약 테이블에 걸린다
                if ( expected<void> definitions = registerDefinitions(); definitions.has_value() == false )
                {
                    _service.shutdown(); // 실패한 start 는 `stop` · `shutdown` 이 보지 않으므로 세운 서비스를 여기서 내린다
                    return definitions;
                }
                _server.initialize( &_service, context._kits.find<IAccountDirectory>(), context._kits.find<IAccountPresence>(), context._pMetricRegistry );
                return {};
            }

            /** @brief 종료 드레인이 기다릴 진행 중인 일 — 저장소에 맡긴 기록 쓰기. */
            int32 getOpenJobCount() const override { return _service.getPendingCount(); }

            void stop() override
            {
                if ( isStarted() )
                    _server.shutdown();
            }

            void shutdown() override
            {
                // 순위표 로직은 맡긴 저장 일을 회수한 뒤(저장소를 종료한 뒤) 종료한다 — 서비스 계약.
                if ( isStarted() )
                    _service.shutdown();
            }

            span<const utf8* const> getOptionalKits() const override
            {
                static constexpr const utf8* kArrKit[] = { OnlineServerKitName::kAccount }; // 표시 이름 · 접속 여부 · 접속 상태
                return kArrKit;
            }

        private:
            /** @brief 보드 · 업적 정의를 서비스에 올립니다. 규칙을 어긴 정의가 있으면 대상이 그 id 인 오류입니다. */
            [[nodiscard]] expected<void> registerDefinitions()
            {
                const vector<LeaderboardDef>        listNoBoard;
                const vector<OnlineAchievementDef>  listNoAchievement;
                const vector<LeaderboardDef>&       listBoard       = _pCatalog != nullptr ? _pCatalog->getBoards() : listNoBoard;
                const vector<OnlineAchievementDef>& listAchievement = _pCatalog != nullptr ? _pCatalog->getAchievements() : listNoAchievement;
                for ( const LeaderboardDef& board : listBoard )
                {
                    if ( _service.registerBoard( board ) == false )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "the leaderboard breaks a board rule (see the warning above)", board._boardID } );
                }
                for ( const string& boardID : _listInlineBoardID )
                {
                    LeaderboardDef board;
                    board._boardID = boardID;
                    if ( _service.registerBoard( board ) == false )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "the leaderboard breaks a board rule (see the warning above)", boardID } );
                }
                for ( const OnlineAchievementDef& achievement : listAchievement )
                {
                    if ( _service.registerAchievement( achievement ) == false )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "the achievement breaks an achievement rule (see the warning above)", achievement._achievementID } );
                }
                return {};
            }

        private:
            shared_ptr<const LeaderboardCatalog> _pCatalog;          ///< 설정 `_catalog` 의 공유 캐시 한 벌(없으면 nullptr)
            vector<string>                       _listInlineBoardID; ///< 설정 `_listBoardID`
            LeaderboardService                   _service;
            LeaderboardServer                    _server;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( LeaderboardOnlineServerKit, OnlineServerKitName::kLeaderboard );
} // namespace sw
