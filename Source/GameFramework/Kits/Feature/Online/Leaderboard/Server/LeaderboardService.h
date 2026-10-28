/**
 * @file LeaderboardService.h
 * @brief 순위표 로직입니다. 점수와 통계는 영속 저장소에 버전 조건으로 쓰며 충돌이 나면 다시 시도합니다. 순위는 캐시 정렬 집합에 두며 잃으면 영속 저장소에서 다시 채웁니다. 전송은 모르며 서비스 스레드 하나에서 씁니다.
 * @details - 원본은 영속 테이블 `lb_score` 이다. 키는 `<보드>/<기간 16진>/<계정 16진>` 이고 값은 점수, 시각, 이름이다.
 *            순위는 캐시 `lb/<보드>/<기간>` 에 두며 멤버는 계정 16진 문자열이다. 오름차순 보드는 부호를 뒤집어 저장한다.
 *            준비 표시(`lb/<보드>/<기간>/ready`)가 없으면 영속 저장소에서 모두 읽어 다시 채운 뒤 응답한다. 그동안 온 읽기는 대기시키고, 그동안 쓴 점수는 다시 채운 뒤 한 번 더 싣는다.
 *          - 갱신 방식은 Best, Latest, Sum 이고 기간은 None, Daily, Weekly 이다. 기간은 `ServiceScheduler::computeLatestOccurrence` 라는 같은 규칙 하나로 구하며 기간 id 는 기간 시작 시각이다.
 *          - 통계 `lb_stat`(`<계정>/<이름>`)이 바뀌면 그 통계에 연결된 보드에 같은 값을 낸다.
 *          - 상위와 내 주변 조회는 표시 이름을 캐시 `lb/name/<계정>`(30일)에서 함께 읽는다.
 *          - 이 서버에 접속한 계정의 통계는 접속 때 읽어 메모리에 두고(`loadAccountStats`) 바뀌면 같이 고친다. 매칭 실력(`IAccountStatSource`)이 이것을 읽는다.
 *          - 업적은 통계가 임계값을 넘으면 `lb_achievement` 를 "없어야 함" 조건으로 쓰고 보상 우편(`ServiceMail::stageSend`, 재원 발행, 멱등 키 `ach.<계정>.<업적>`)을 한 트랜잭션에 넣는다. 두 번 넘어도 한 번만 처리된다.
 *          - 시즌 정산은 예약 작업(`lb.settle.<보드>.<시즌 16진 8자리>`, 창 [끝, 끝 + 7일))이 한다. 점수를 모두 읽어 순위(같은 점수는 계정 id 오름차순)대로 결과 `lb_season_result`("없어야 함")와 구간 보상 우편을 15명씩 한 트랜잭션에 쓴다.
 *            결과가 이미 있는 사람은 건너뛰므로 다시 돌아도 한 번만 처리된다.
 *
 *          PlayFab Statistics 와 Leaderboards, EOS Stats 와 Leaderboards, Nakama Leaderboards(best, set, incr)에 해당한다.
 */
#pragma once
#include "Core/Container/EventBuffer.h"
#include "Core/Container/IDAllocator.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/Base/Online/Identity/AccountStatSource.h"
#include "GameFramework/Base/Online/Schedule/ServiceScheduler.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Runtime/LeaderboardTypes.h"

namespace sw
{
    struct EphemeralReply;

    class EphemeralStoreRouter;
    class IServiceStore;

    /** @brief 빌려 쓰는 객체들입니다. */
    struct LeaderboardServiceDependencies
    {
        IServiceStore*        _pStore{ nullptr };  ///< 필수
        EphemeralStoreRouter* _pRouter{ nullptr }; ///< 필수이다. 순위에 쓴다
    };
} // namespace sw

namespace sw
{
    /** @brief 요청 종류입니다. 완료가 어느 요청의 것인지 나타냅니다. */
    enum class LeaderboardOperation : uint8
    {
        Submit = 0,
        Top,
        Around,
        Stats,
        StatChange,
        Achievements,
        SeasonResult,
        ScoresOf,
        AchievementProgress
    };
} // namespace sw

namespace sw
{
    /** @brief 요청 하나의 완료입니다. */
    struct LeaderboardCompletion
    {
        vector<LeaderboardEntry>    _listEntry{};
        vector<LeaderboardStat>     _listStat{};
        vector<AchievementState>    _listAchievement{};
        vector<AchievementProgress> _listProgress{};
        uint64                      _periodID{ 0 };
        uint64                      _requestTag{ 0 };
        int64                       _score{ 0 }; ///< Submit 과 StatChange 의 적용 뒤 값이다
        LeaderboardResult           _result{ LeaderboardResult::Ok };
        LeaderboardOperation        _operation{ LeaderboardOperation::Submit };
    };
} // namespace sw

namespace sw
{
    /** @brief 계정 하나의 점수입니다. 다시 채우기와 정산에서 씁니다. */
    struct LeaderboardScoreRow
    {
        AccountID _accountID{ kInvalidAccountID };
        int64     _score{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 업적 달성 하나입니다. 서버가 알림으로 보냅니다. */
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
     * @brief 순위표 로직입니다. 서비스 스레드에서 씁니다.
     */
    class SW_KIT_SERVER_API LeaderboardService final : public IAccountStatSource
    {
    public:
        LeaderboardService();
        ~LeaderboardService() override;

        LeaderboardService( const LeaderboardService& )            = delete;
        LeaderboardService& operator=( const LeaderboardService& ) = delete;

        void initialize( const LeaderboardServiceDependencies& dependencies );
        /** @brief 기다리는 캐시 요청을 취소하고 메모리를 비웁니다. 맡긴 저장소 작업은 호출하는 쪽이 먼저 회수합니다. */
        void shutdown();

        /** @brief 보드를 등록합니다. 기동할 때 게임 데이터로 부르며, 규칙에 맞지 않으면 false 를 반환합니다. */
        [[nodiscard]] bool           registerBoard( const LeaderboardDefinition& definition );
        const LeaderboardDefinition* findBoard( string_view boardID ) const;
        /** @brief 현재 기간 id 를 반환합니다. 기간 시작 시각이며 None 이면 0입니다. */
        uint64 computePeriodID( const LeaderboardDefinition& definition, UnixMilliseconds nowMs ) const;

        /** @brief 점수를 냅니다. 서버 게임 로직이 부르며, 클라이언트 제출은 바인딩이 `_bClientSubmit` 보드만 넘깁니다. 요청 태그가 0이면 완료가 없습니다. */
        void submitScore( string_view boardID, AccountID accountID, string_view displayName, int64 score, UnixMilliseconds nowMs, uint64 requestTag );
        void readTop( string_view boardID, int32 offset, int32 count, UnixMilliseconds nowMs, uint64 requestTag );
        /** @brief 내 순위 주변을 조회합니다. 위아래로 @p radius 명씩이며 나를 포함합니다. 점수가 없으면 NotRanked 입니다. */
        void readAround( string_view boardID, AccountID accountID, int32 radius, UnixMilliseconds nowMs, uint64 requestTag );
        /** @brief 통계를 바꿉니다. @p update 방식을 따르며 Best 는 큰 값을 씁니다. 그 통계에 연결된 보드에 같은 값을 냅니다. */
        void changeStat( AccountID accountID, string_view displayName, string_view statName, int64 value, LeaderboardUpdate update, UnixMilliseconds nowMs, uint64 requestTag );
        void readStats( AccountID accountID, uint64 requestTag );
        /** @brief 접속한 계정의 통계를 읽어 메모리에 둡니다. 바인딩이 계정이 붙을 때 부릅니다. 읽기가 끝나기 전에는 `findStat` 이 NotLoaded 입니다. */
        void loadAccountStats( AccountID accountID );
        /** @brief 떠난 계정의 통계를 메모리에서 지웁니다. */
        void dropAccountStats( AccountID accountID );
        // IAccountStatSource
        AccountStatLookup findStat( AccountID accountID, string_view statName, int64& outValue ) const override;

        /** @brief 시즌 정산 작업을 등록할 예약 테이블을 지정합니다. 빌려 쓰며 `registerBoard` 전에 지정합니다. 없으면 `settleSeason` 을 부르는 쪽이 정산합니다. */
        void setScheduler( ServiceScheduler* pScheduler ) { _pScheduler = pScheduler; }
        /** @brief 업적을 등록합니다. 기동할 때 게임 데이터로 부르며, 규칙에 맞지 않으면 false 를 반환합니다. */
        [[nodiscard]] bool registerAchievement( const AchievementDefinition& definition );
        void               readAchievements( AccountID accountID, uint64 requestTag );
        /** @brief 등록한 업적마다 진행률(연결된 통계 값)과 달성 시각을 읽습니다. 숨긴 업적은 달성 전에 가립니다. id 순서입니다. */
        void readAchievementProgress( AccountID accountID, uint64 requestTag );
        /**
         * @brief 계정 목록의 이번 기간 점수를 읽어 목록 안 순위로 돌려줍니다(친구 순위표). 점수가 없는 계정은 빠집니다.
         * @details 전체 순위가 아니라 목록 안의 순위입니다. 영속 점수(`lb_score`)를 계정마다 읽으므로 캐시를 잃어도 같은 결과입니다.
         *          같은 점수는 계정 id 오름차순입니다(정산과 같은 규칙). 목록이 비었거나 상한을 넘으면 Invalid 입니다.
         */
        void readScoresOf( string_view boardID, const vector<AccountID>& listAccountID, UnixMilliseconds nowMs, uint64 requestTag );
        /** @brief 시즌 결과 하나를 조회합니다. 점수와 순위이며 완료의 항목 하나이고 기간 id 가 시즌입니다. 정산 전이거나 점수가 없었으면 NotRanked 입니다. */
        void readSeasonResult( string_view boardID, uint32 seasonID, AccountID accountID, uint64 requestTag );
        /** @brief 시즌 하나를 정산합니다. 예약 작업이 부르며 테스트와 운영은 직접 부릅니다. 끝나면 @p onDone 을 부르며 인자는 모두 정산했는지 여부입니다. */
        void settleSeason( string_view boardID, uint32 seasonID, UnixMilliseconds nowMs, const SettlementDelegate& onDone );
        void drainAchievementUnlocks( vector<AchievementUnlock>& outListUnlock ) { _unlockBuffer.drainTo( outListUnlock ); }
        /** @brief 시즌 정산 작업 id 를 만듭니다. `lb.settle.<보드>.<시즌 16진 8자리>` 형식입니다. */
        static string makeSettlementJobID( string_view boardID, uint32 seasonID );

        // 예약 작업
        void onScheduledRun( const ScheduledRun& run );

        void  drainCompletions( vector<LeaderboardCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }
        int32 getPendingCount() const { return _pendingCount; }

        /**
         * @brief 기간 보드의 캐시 순위(정렬 집합과 준비 표시)를 남겨 둘 시간(밀리초)입니다. 초기화가 없는 보드는 0(지우지 않음)입니다.
         * @details 기간이 끝난 뒤 하루까지 남기고 캐시가 지운다(만료). 지난 기간의 순위는 읽지 않고, 시즌 결과는 영속 저장소(`lb_season_result`)가 원본이기 때문이다.
         *          기간마다 정렬 집합이 쌓여 캐시를 채우지 않게 하는 것이다(Redis 순위표의 기간 키 + EXPIRE 관례).
         */
        int64 computeRankRetentionMs( const LeaderboardDefinition& definition, uint64 periodID, UnixMilliseconds nowMs ) const;
        /** @brief 캐시 점수를 구합니다. 오름차순 보드는 부호를 뒤집으며 두 번 부르면 원래 값이 됩니다. */
        static int64  toCacheScore( LeaderboardOrder order, int64 score ) { return order == LeaderboardOrder::Ascending ? -score : score; }
        static string makeRankKey( string_view boardID, uint64 periodID );

        /** @brief 작업의 `complete` 가 호출합니다. 키트 안에서만 씁니다. */
        void applyScoreWrite( const string& boardID, uint64 periodID, AccountID accountID, const string& displayName, int64 finalScore, bool bChanged,
                              LeaderboardResult result, uint64 requestTag, LeaderboardOperation operation, UnixMilliseconds nowMs );
        void applyStats( uint64 requestTag, LeaderboardResult result, vector<LeaderboardStat>&& listStat );
        void applyStatCache( AccountID accountID, bool bReadOk, ServiceStoreCompletion completion, vector<LeaderboardStat>&& listStat );
        void applyRebuildRead( const string& rankKey, vector<LeaderboardScoreRow>&& listScore, bool bReadOk );
        void applyAchievementUnlock( AccountID accountID, const AchievementState& achievement, bool bUnlocked );
        void applyAchievements( uint64 requestTag, LeaderboardResult result, vector<AchievementState>&& listAchievement );
        void applyAchievementProgress( uint64 requestTag, LeaderboardResult result, vector<AchievementProgress>&& listProgress );
        void applyScoresOf( uint64 requestTag, uint64 periodID, LeaderboardResult result, vector<LeaderboardEntry>&& listEntry );
        void applySeasonResult( uint64 requestTag, uint32 seasonID, LeaderboardResult result, const LeaderboardEntry& entry );
        /** @brief 정산의 순위 읽기가 끝났습니다. 순위대로 정렬한 점수를 받아 첫 페이지를 맡깁니다. */
        void applySettlementRanking( uint64 settlementID, bool bReadOk, vector<LeaderboardScoreRow>&& listRanked );
        /** @brief 정산 페이지 하나가 끝났습니다. 다 썼으면 다음 페이지를, 실패했거나 끝이면 정산을 마칩니다. */
        void applySettlementPage( uint64 settlementID, bool bCommitted );

    private:
        struct PendingRead
        {
            string               _boardID{};
            AccountID            _accountID{ kInvalidAccountID };
            uint64               _periodID{ 0 };
            uint64               _requestTag{ 0 };
            UnixMilliseconds     _nowMs{ 0 }; ///< 요청 시각 — 다시 채운 순위의 만료를 정한다
            int32                _offset{ 0 };
            int32                _count{ 0 };
            LeaderboardOperation _operation{ LeaderboardOperation::Top };
        };

        /** @brief 접속한 계정 하나의 통계입니다. 읽기가 끝나야 `_bLoaded` 가 켜진다. */
        struct AccountStats
        {
            unordered_map<string, int64> _mapValue{};
            uint8                        _bLoaded{ SW_FALSE };
        };

        struct RankQuery
        {
            vector<LeaderboardEntry> _listEntry{};
            PendingRead              _read{};
            int32                    _outstandingNameCount{ 0 };
        };

        /**
         * @brief 진행 중인 시즌 정산 하나입니다. 순위를 매긴 점수를 들고 페이지마다 저장소 일 하나를 맡깁니다.
         * @details 시즌 하나를 일 하나로 돌리면 참가가 수십만일 때 저장소 워커 하나를 오래 잡아 다른 요청이 기다린다. 페이지로 나누면 페이지 사이에 다른 일이 낀다.
         */
        struct SettlementProgress
        {
            vector<LeaderboardScoreRow> _listRanked{};
            LeaderboardDefinition       _board{};
            LeaderboardSeason           _season{};
            string                      _jobID{}; ///< 예약 작업에서 왔으면 그 id, 직접 부른 정산이면 빈 글
            SettlementDelegate          _onDone{};
            UnixMilliseconds            _nowMs{ 0 };
            size_t                      _nextIndex{ 0 }; ///< 다음 페이지의 첫 사람(0 부터, 순위 - 1)
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
        /** @brief 정산 하나를 시작합니다. 순위 읽기 일을 맡깁니다. */
        void startSettlement( const LeaderboardDefinition& board, const LeaderboardSeason& season, string jobID, UnixMilliseconds nowMs, const SettlementDelegate& onDone );
        /** @brief 다음 페이지를 맡기거나, 남은 사람이 없으면 성공으로 마칩니다. */
        void submitNextSettlementPage( uint64 settlementID );
        /** @brief 정산을 마치고 예약 작업과 완료 델리게이트에 알립니다. */
        void finishSettlement( uint64 settlementID, bool bSucceeded );
        void applySettlement( const string& jobID, const SettlementDelegate& onDone, bool bSucceeded );

        unordered_map<string, LeaderboardDefinition>       _mapBoard;
        unordered_map<string, vector<PendingRead>>         _mapRankKeyToWaitingRead; ///< 다시 채우는 동안 대기 중인 읽기이다
        unordered_map<string, vector<LeaderboardScoreRow>> _mapRankKeyToLateWrite;   ///< 다시 채우는 동안 쓴 점수(캐시 점수)이다. 채운 뒤 한 번 더 싣는다
        unordered_map<uint64, PendingRead>                 _mapReadyRequestToRead;
        unordered_map<uint64, uint64>                      _mapRequestToQuery; ///< 캐시 요청에서 질의(순위, 범위, 이름)로 가는 맵이다
        unordered_map<uint64, int32>                       _mapNameRequestToIndex;
        unordered_map<uint64, RankQuery>                   _mapQuery;
        unordered_map<string, AchievementDefinition>       _mapAchievement;
        unordered_map<AccountID, AccountStats>             _mapAccountStats;        ///< 이 서버에 접속한 계정의 통계이다
        unordered_map<string, ScheduledRun>                _mapJobToRun;            ///< 정산 작업 id 에서 차지한 회차로 가는 맵이다. 끝나면 completeRun 을 부른다
        unordered_map<uint64, SettlementProgress>          _mapSettlement;          ///< 진행 중인 정산(정산 번호 → 진행)
        unordered_set<string>                              _uniqueExpiringRankKeys; ///< 이 프로세스가 만료를 건 기간 순위 키(기간마다 한 번)
        EventBuffer<AchievementUnlock>                     _unlockBuffer;
        EventBuffer<LeaderboardCompletion>                 _completionBuffer;
        LeaderboardServiceDependencies                     _dependencies;
        Subscription                                       _replyOwner; ///< 라우터 답의 주인 몫(`initialize` 가 열고 `shutdown` 이 놓는다)
        ServiceScheduler*                                  _pScheduler;
        IDAllocator<uint64>                                _queryIDAllocator;
        IDAllocator<uint64>                                _settlementIDAllocator;
        int32                                              _pendingCount;
    };
} // namespace sw
