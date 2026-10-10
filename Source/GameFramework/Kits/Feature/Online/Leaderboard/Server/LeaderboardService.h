/**
 * @file LeaderboardService.h
 * @brief 순위표 로직 — 점수 · 통계는 영속(판 조건, 충돌이면 다시), 순위는 캐시 정렬 집합(잃으면 영속에서 다시 채움). 전송을 모릅니다(서비스 스레드 하나).
 * @details - 정본은 영속 `lb_score`(`<표>/<기간 16 진>/<계정 16 진>` → 점수 · 시각 · 이름). 순위는 캐시 `lb/<표>/<기간>`(멤버 = 계정 16 진, 오름차순 표는 부호를 뒤집는다).
 *            준비 표시(`lb/<표>/<기간>/ready`)가 없으면 영속에서 모두 읽어 다시 채운 뒤 답한다 — 그동안 온 읽기는 줄을 서고, 그동안 쓴 점수는 다시 채운 뒤 한 번 더 싣는다.
 *          - 갱신 Best · Latest · Sum, 기간 None · Daily · Weekly(`ServiceScheduler::computeLatestOccurrence` — 같은 규칙 하나, 기간 id = 기간 시작 시각).
 *          - 통계 `lb_stat`(`<계정>/<이름>`) — 바뀌면 그 통계에 건 표에 같은 값을 낸다.
 *          - 상위 · 내 둘레 조회는 표시 이름을 캐시 `lb/name/<계정>`(30 일)에서 함께 읽는다.
 *          - 업적: 통계가 문턱을 넘으면 `lb_achievement` 를 "없어야 함" 으로 + 보상 우편(`ServiceMail::stageSend` — 재원 발행, 멱등 키 `ach.<계정>.<업적>`)을
 *            한 트랜잭션에 — 두 번 넘어도 한 번. 시즌 정산: 예약 작업(`lb.settle.<표>.<시즌 16 진 8>`, 창 [끝, 끝 + 7 일))이 점수를 모두 읽어 순위(같은 점수는
 *            계정 id 오름차순)대로 결과 `lb_season_result`("없어야 함") + 구간 보상 우편을 15 명씩 한 트랜잭션에 — 결과가 있는 사람은 건너뛴다(다시 돌아도 한 번).
 *          PlayFab Statistics + Leaderboards · EOS Stats → Leaderboards · Nakama Leaderboards(best · set · incr)와 같은 자리다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/Base/Online/Schedule/ServiceScheduler.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Shared/LeaderboardTypes.h"

namespace sw
{
    struct EphemeralReply;

    class EphemeralStoreRouter;
    class IServiceStore;

    /** @brief 빌려 쓰는 것들입니다. */
    struct LeaderboardServiceDependencies
    {
        IServiceStore*        _pStore{ nullptr };  ///< 필수
        EphemeralStoreRouter* _pRouter{ nullptr }; ///< 필수 — 순위
    };
} // namespace sw

namespace sw
{
    /** @brief 요청 종류입니다(완료가 어느 요청의 것인지). */
    enum class LeaderboardOperation : uint8
    {
        Submit = 0,
        Top,
        Around,
        Stats,
        StatChange,
        Achievements,
        SeasonResult
    };
} // namespace sw

namespace sw
{
    /** @brief 요청 하나의 완료입니다. */
    struct LeaderboardCompletion
    {
        vector<LeaderboardEntry> _listEntry{};
        vector<LeaderboardStat>  _listStat{};
        vector<AchievementState> _listAchievement{};
        uint64                   _periodID{ 0 };
        uint64                   _requestTag{ 0 };
        int64                    _score{ 0 }; ///< Submit · StatChange — 적용 뒤 값
        LeaderboardResult        _result{ LeaderboardResult::Ok };
        LeaderboardOperation     _operation{ LeaderboardOperation::Submit };
    };
} // namespace sw

namespace sw
{
    /** @brief 계정 하나의 점수입니다(다시 채우기 · 정산). */
    struct LeaderboardScoreRow
    {
        AccountID _accountID{ kInvalidAccountID };
        int64     _score{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 업적 달성 하나(서버가 알림으로 보낸다)입니다. */
    struct AchievementUnlock
    {
        AchievementState _state{};
        AccountID        _accountID{ kInvalidAccountID };
    };

    using SettlementDelegate = Delegate<void( bool )>;
} // namespace sw

namespace sw
{
    /**
     * @class LeaderboardService
     * @brief 순위표 로직입니다(서비스 스레드).
     */
    class SW_GF_API LeaderboardService final : public IScheduledJobHandler
    {
    public:
        LeaderboardService();
        ~LeaderboardService() override;

        LeaderboardService( const LeaderboardService& )            = delete;
        LeaderboardService& operator=( const LeaderboardService& ) = delete;

        void initialize( const LeaderboardServiceDependencies& dependencies );
        /** @brief 기다리는 캐시 요청을 취소하고 메모리를 비웁니다. 맡긴 저장소 일은 부르는 쪽이 먼저 거둔다. */
        void shutdown();

        /** @brief 표를 올립니다(기동 때 — 게임 데이터). 규칙 밖이면 false. */
        [[nodiscard]] bool           registerBoard( const LeaderboardDefinition& definition );
        const LeaderboardDefinition* findBoard( string_view boardID ) const;
        /** @brief 지금 기간 id(기간 시작 시각 — None 은 0)입니다. */
        uint64 computePeriodID( const LeaderboardDefinition& definition, int64 nowMs ) const;

        /** @brief 점수를 냅니다(서버 게임 로직 — 클라이언트 제출은 바인딩이 `_bClientSubmit` 표만 넘긴다). 꼬리표 0 이면 완료가 없다. */
        void submitScore( string_view boardID, AccountID accountID, string_view displayName, int64 score, int64 nowMs, uint64 requestTag );
        void readTop( string_view boardID, int32 offset, int32 count, int64 nowMs, uint64 requestTag );
        /** @brief 내 순위 둘레(위 · 아래 @p radius 명, 나 포함)입니다. 점수가 없으면 NotRanked. */
        void readAround( string_view boardID, AccountID accountID, int32 radius, int64 nowMs, uint64 requestTag );
        /** @brief 통계를 바꿉니다(@p update 방식 — Best 는 큰 값). 그 통계에 건 표에 같은 값을 낸다. */
        void changeStat( AccountID accountID, string_view displayName, string_view statName, int64 value, LeaderboardUpdate update, int64 nowMs, uint64 requestTag );
        void readStats( AccountID accountID, uint64 requestTag );

        /** @brief 시즌 정산 작업을 올릴 예약 표입니다(빌려 쓴다 — `registerBoard` 전에. 없으면 정산은 `settleSeason` 을 부르는 쪽이). */
        void setScheduler( ServiceScheduler* pScheduler ) { _pScheduler = pScheduler; }
        /** @brief 업적을 올립니다(기동 때 — 게임 데이터). 규칙 밖이면 false. */
        [[nodiscard]] bool registerAchievement( const AchievementDefinition& definition );
        void               readAchievements( AccountID accountID, uint64 requestTag );
        /** @brief 시즌 결과 하나(점수 · 순위 — 완료의 항목 하나, 기간 id = 시즌)입니다. 정산 전이거나 점수가 없었으면 NotRanked. */
        void readSeasonResult( string_view boardID, uint32 seasonID, AccountID accountID, uint64 requestTag );
        /** @brief 시즌 하나를 정산합니다(예약 작업이 부른다 — 시험 · 운영은 직접). 끝나면 @p onDone( 모두 정산했나 ). */
        void settleSeason( string_view boardID, uint32 seasonID, int64 nowMs, const SettlementDelegate& onDone );
        void drainAchievementUnlocks( vector<AchievementUnlock>& outListUnlock ) { _unlockBuffer.drainTo( outListUnlock ); }
        /** @brief 시즌 정산 작업 id 입니다(`lb.settle.<표>.<시즌 16 진 8>`). */
        static string makeSettlementJobID( string_view boardID, uint32 seasonID );

        // IScheduledJobHandler
        void onScheduledRun( const ScheduledRun& run ) override;

        void  drainCompletions( vector<LeaderboardCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }
        int32 getPendingCount() const { return _pendingCount; }

        /** @brief 캐시 점수 — 오름차순 표는 부호를 뒤집는다(두 번 부르면 원래 값). */
        static int64  toCacheScore( LeaderboardOrder order, int64 score ) { return order == LeaderboardOrder::Ascending ? -score : score; }
        static string makeRankKey( string_view boardID, uint64 periodID );

        /** @brief 일의 `complete` 가 부릅니다(키트 안). */
        void applyScoreWrite( const string& boardID, uint64 periodID, AccountID accountID, const string& displayName, int64 finalScore, bool bChanged,
                              LeaderboardResult result, uint64 requestTag, LeaderboardOperation operation, int64 nowMs );
        void applyStats( uint64 requestTag, LeaderboardResult result, vector<LeaderboardStat>&& listStat );
        void applyRebuildRead( const string& rankKey, vector<LeaderboardScoreRow>&& listScore, bool bReadOk );
        void applyAchievementUnlock( AccountID accountID, const AchievementState& achievement, bool bUnlocked );
        void applyAchievements( uint64 requestTag, LeaderboardResult result, vector<AchievementState>&& listAchievement );
        void applySeasonResult( uint64 requestTag, uint32 seasonID, LeaderboardResult result, const LeaderboardEntry& entry );
        void applySettlement( const string& jobID, const SettlementDelegate& onDone, bool bSucceeded );

    private:
        struct PendingRead
        {
            string               _boardID{};
            AccountID            _accountID{ kInvalidAccountID };
            uint64               _periodID{ 0 };
            uint64               _requestTag{ 0 };
            int32                _offset{ 0 };
            int32                _count{ 0 };
            LeaderboardOperation _operation{ LeaderboardOperation::Top };
        };

        struct RankQuery
        {
            vector<LeaderboardEntry> _listEntry{};
            PendingRead              _read{};
            int32                    _outstandingNameCount{ 0 };
        };

        void startRead( const PendingRead& read );
        void onReadyReply( const EphemeralReply& reply );
        void onRankReply( const EphemeralReply& reply );
        void onRangeReply( const EphemeralReply& reply );
        void onNameReply( const EphemeralReply& reply );
        void finishQuery( uint64 queryID );
        void completeRead( const PendingRead& read, LeaderboardResult result );
        void pushCompletion( uint64 requestTag, LeaderboardOperation operation, LeaderboardResult result );
        void submitCacheScore( const string& rankKey, AccountID accountID, int64 cacheScore );

        unordered_map<string, LeaderboardDefinition>       _mapBoard;
        unordered_map<string, vector<PendingRead>>         _mapRankKeyToWaitingRead; ///< 다시 채우는 동안 줄 선 읽기
        unordered_map<string, vector<LeaderboardScoreRow>> _mapRankKeyToLateWrite;   ///< 다시 채우는 동안 쓴 점수(캐시 점수) — 채운 뒤 한 번 더
        unordered_map<uint64, PendingRead>                 _mapReadyRequestToRead;
        unordered_map<uint64, uint64>                      _mapRequestToQuery; ///< 캐시 요청 → 질의(순위 · 범위 · 이름)
        unordered_map<uint64, int32>                       _mapNameRequestToIndex;
        unordered_map<uint64, RankQuery>                   _mapQuery;
        unordered_map<string, AchievementDefinition>       _mapAchievement;
        unordered_map<string, ScheduledRun>                _mapJobToRun; ///< 정산 작업 id → 차지한 회차(끝나면 completeRun)
        EventBuffer<AchievementUnlock>                     _unlockBuffer;
        EventBuffer<LeaderboardCompletion>                 _completionBuffer;
        LeaderboardServiceDependencies                     _dependencies;
        ServiceScheduler*                                  _pScheduler;
        uint64                                             _nextQueryID;
        int32                                              _pendingCount;
    };
} // namespace sw
