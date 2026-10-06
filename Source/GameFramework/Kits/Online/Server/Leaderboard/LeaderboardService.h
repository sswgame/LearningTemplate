/**
 * @file LeaderboardService.h
 * @brief 순위표 로직 — 점수 · 통계는 영속(판 조건, 충돌이면 다시), 순위는 캐시 정렬 집합(잃으면 영속에서 다시 채움). 전송을 모릅니다(서비스 스레드 하나).
 * @details - 정본은 영속 `lb_score`(`<표>/<기간 16 진>/<계정 16 진>` → 점수 · 시각 · 이름). 순위는 캐시 `lb/<표>/<기간>`(멤버 = 계정 16 진, 오름차순 표는 부호를 뒤집는다).
 *            준비 표시(`lb/<표>/<기간>/ready`)가 없으면 영속에서 모두 읽어 다시 채운 뒤 답한다 — 그동안 온 읽기는 줄을 서고, 그동안 쓴 점수는 다시 채운 뒤 한 번 더 싣는다.
 *          - 갱신 Best · Latest · Sum, 기간 None · Daily · Weekly(`ServiceScheduler::computeLatestOccurrence` — 같은 규칙 하나, 기간 id = 기간 시작 시각).
 *          - 통계 `lb_stat`(`<계정>/<이름>`) — 바뀌면 그 통계에 건 표에 같은 값을 낸다.
 *          - 상위 · 내 둘레 조회는 표시 이름을 캐시 `lb/name/<계정>`(30 일)에서 함께 읽는다.
 *          PlayFab Statistics + Leaderboards · EOS Stats → Leaderboards · Nakama Leaderboards(best · set · incr)와 같은 자리다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Utility/EventBuffer.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Online/Leaderboard/LeaderboardTypes.h"

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
        StatChange
    };
} // namespace sw

namespace sw
{
    /** @brief 요청 하나의 완료입니다. */
    struct LeaderboardCompletion
    {
        vector<LeaderboardEntry> _listEntry{};
        vector<LeaderboardStat>  _listStat{};
        uint64                   _periodId{ 0 };
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
        AccountId _accountId{ kInvalidAccountId };
        int64     _score{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class LeaderboardService
     * @brief 순위표 로직입니다(서비스 스레드).
     */
    class SW_GF_API LeaderboardService
    {
    public:
        LeaderboardService();
        ~LeaderboardService();

        LeaderboardService( const LeaderboardService& )            = delete;
        LeaderboardService& operator=( const LeaderboardService& ) = delete;

        void initialize( const LeaderboardServiceDependencies& dependencies );
        /** @brief 기다리는 캐시 요청을 취소하고 메모리를 비웁니다. 맡긴 저장소 일은 부르는 쪽이 먼저 거둔다. */
        void shutdown();

        /** @brief 표를 올립니다(기동 때 — 게임 데이터). 규칙 밖이면 false. */
        [[nodiscard]] bool           registerBoard( const LeaderboardDefinition& definition );
        const LeaderboardDefinition* findBoard( string_view boardId ) const;
        /** @brief 지금 기간 id(기간 시작 시각 — None 은 0)입니다. */
        uint64 computePeriodId( const LeaderboardDefinition& definition, int64 nowMs ) const;

        /** @brief 점수를 냅니다(서버 게임 로직 — 클라이언트 제출은 바인딩이 `_bClientSubmit` 표만 넘긴다). 꼬리표 0 이면 완료가 없다. */
        void submitScore( string_view boardId, AccountId accountId, string_view displayName, int64 score, int64 nowMs, uint64 requestTag );
        void readTop( string_view boardId, int32 offset, int32 count, int64 nowMs, uint64 requestTag );
        /** @brief 내 순위 둘레(위 · 아래 @p radius 명, 나 포함)입니다. 점수가 없으면 NotRanked. */
        void readAround( string_view boardId, AccountId accountId, int32 radius, int64 nowMs, uint64 requestTag );
        /** @brief 통계를 바꿉니다(@p update 방식 — Best 는 큰 값). 그 통계에 건 표에 같은 값을 낸다. */
        void changeStat( AccountId accountId, string_view displayName, string_view statName, int64 value, LeaderboardUpdate update, int64 nowMs, uint64 requestTag );
        void readStats( AccountId accountId, uint64 requestTag );

        void  drainCompletions( vector<LeaderboardCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }
        int32 getPendingCount() const { return _pendingCount; }

        /** @brief 캐시 점수 — 오름차순 표는 부호를 뒤집는다(두 번 부르면 원래 값). */
        static int64  toCacheScore( LeaderboardOrder order, int64 score ) { return order == LeaderboardOrder::Ascending ? -score : score; }
        static string makeRankKey( string_view boardId, uint64 periodId );

        /** @brief 일의 `complete` 가 부릅니다(키트 안). */
        void applyScoreWrite( const string& boardId, uint64 periodId, AccountId accountId, const string& displayName, int64 finalScore, bool bChanged,
                              LeaderboardResult result, uint64 requestTag, LeaderboardOperation operation, int64 nowMs );
        void applyStats( uint64 requestTag, LeaderboardResult result, vector<LeaderboardStat>&& listStat );
        void applyRebuildRead( const string& rankKey, vector<LeaderboardScoreRow>&& listScore, bool bReadOk );

    private:
        struct PendingRead
        {
            string               _boardId{};
            AccountId            _accountId{ kInvalidAccountId };
            uint64               _periodId{ 0 };
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
        void finishQuery( uint64 queryId );
        void completeRead( const PendingRead& read, LeaderboardResult result );
        void pushCompletion( uint64 requestTag, LeaderboardOperation operation, LeaderboardResult result );
        void submitCacheScore( const string& rankKey, AccountId accountId, int64 cacheScore );

        unordered_map<string, LeaderboardDefinition>       _mapBoard;
        unordered_map<string, vector<PendingRead>>         _mapRankKeyToWaitingRead; ///< 다시 채우는 동안 줄 선 읽기
        unordered_map<string, vector<LeaderboardScoreRow>> _mapRankKeyToLateWrite;   ///< 다시 채우는 동안 쓴 점수(캐시 점수) — 채운 뒤 한 번 더
        unordered_map<uint64, PendingRead>                 _mapReadyRequestToRead;
        unordered_map<uint64, uint64>                      _mapRequestToQuery; ///< 캐시 요청 → 질의(순위 · 범위 · 이름)
        unordered_map<uint64, int32>                       _mapNameRequestToIndex;
        unordered_map<uint64, RankQuery>                   _mapQuery;
        EventBuffer<LeaderboardCompletion>                 _completionBuffer;
        LeaderboardServiceDependencies                     _dependencies;
        uint64                                             _nextQueryId;
        int32                                              _pendingCount;
    };
} // namespace sw
