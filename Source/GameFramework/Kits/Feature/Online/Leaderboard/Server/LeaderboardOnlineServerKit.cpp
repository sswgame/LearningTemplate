#include "pch.h"

#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardCatalog.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardServer.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardService.h"

// 순위표 키트의 서버 조립 — 순위표 서비스와 바인딩을 만들고, 보드 · 업적 정의 데이터를 등록하며, 시즌 정산을 예약 작업에 건다.
// 표시 이름과 접속 여부는 계정 키트, 다른 서버 계정의 업적 알림은 접속 상태를 받는다.
// 옵션:
//   catalog  보드 · 업적 정의(리소스 경로, `<LeaderboardCatalog>`).
//   board    기본 정의(내림차순 · Best · 초기화 없음 · 서버만 제출)의 보드 id(여러 번 적는다). 데이터 파일 없이 도는 도구 · 테스트용이다.
//   둘 다 없으면 보드가 없다(경고).

namespace sw
{
    SW_LOG_CALLER( "LeaderboardOnlineServerKit" );

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
                : _catalog{}
                , _listInlineBoardID{}
                , _service{}
                , _server{}
                , _bStarted{ false }
            {
            }

            bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
            {
                string catalogPath;
                if ( entry.readOptions( {
                                            {"catalog",        catalogPath},
                                            {  "board", _listInlineBoardID}
                },
                                        outError ) == false )
                    return false;
                if ( catalogPath.empty() == false && _catalog.loadFromResource( catalogPath ) == false )
                {
                    outError = "the leaderboard catalog '" + catalogPath + "' could not be read";
                    return false;
                }
                if ( catalogPath.empty() && _listInlineBoardID.empty() )
                    SW_LOG_WARNING( "Leaderboard kit: neither a catalog nor a board option - no boards are registered" );
                if ( registerHostService( context, entry, &_server, outError ) == false )
                    return false;
                context._pStatSource = &_service; // 매칭 키트가 실력 모드의 통계를 읽는다. 접속한 계정의 통계만 메모리에 있다
                return true;
            }

            bool start( OnlineServerKitContext& context, string& outError ) override
            {
                if ( context._pRouter == nullptr )
                {
                    outError = "the leaderboard needs a cache (ranks live in sorted sets)";
                    return false;
                }
                LeaderboardServiceDependencies dependencies;
                dependencies._pStore  = context._pStore;
                dependencies._pRouter = context._pRouter;
                _service.initialize( dependencies );
                _bStarted = true;
                _service.setScheduler( context._pScheduler ); // 보드 등록 전 — 시즌 보드의 정산 작업이 예약 테이블에 걸린다
                for ( const LeaderboardDefinition& board : _catalog.getBoards() )
                {
                    if ( _service.registerBoard( board ) == false )
                    {
                        outError = "leaderboard '" + board._boardID + "' breaks a board rule (see the warning above)";
                        return false;
                    }
                }
                for ( const string& boardID : _listInlineBoardID )
                {
                    LeaderboardDefinition board;
                    board._boardID = boardID;
                    if ( _service.registerBoard( board ) == false )
                    {
                        outError = "leaderboard '" + boardID + "' breaks a board rule (see the warning above)";
                        return false;
                    }
                }
                for ( const AchievementDefinition& achievement : _catalog.getAchievements() )
                {
                    if ( _service.registerAchievement( achievement ) == false )
                    {
                        outError = "achievement '" + achievement._achievementID + "' breaks an achievement rule (see the warning above)";
                        return false;
                    }
                }
                _server.initialize( &_service, context._pAccountDirectory, context._pPresence );
                return true;
            }

            void stop() override
            {
                if ( _bStarted )
                    _server.shutdown();
            }

            void shutdown() override
            {
                // 순위표 로직은 맡긴 저장 일을 회수한 뒤(저장소를 종료한 뒤) 종료한다 — 서비스 계약.
                if ( _bStarted )
                    _service.shutdown();
                _bStarted = false;
            }

        private:
            LeaderboardCatalog _catalog;
            vector<string>     _listInlineBoardID; ///< 옵션 `board` 의 id
            LeaderboardService _service;
            LeaderboardServer  _server;
            bool               _bStarted;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( LeaderboardOnlineServerKit, "leaderboard", OnlineServerKitOrder::kLeaderboard );
} // namespace sw
