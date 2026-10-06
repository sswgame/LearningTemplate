/**
 * @file LoadBotMetrics.h
 * @brief 부하 시험 지표 — 동작마다 지연 표본(마이크로초) · 오류 수, 오류 종류별 수, 알림 종류별 수, 연결 사건, 경기 id. 끝에 백분위 표와 JSON 을 냅니다.
 * @details 표본을 모두 들고 끝에 정렬한다(봇 수천 × 동작 수십 = 수십만 개 — 수 MB, 백분위가 정확하다). 실행기 스레드 하나에서 쓴다.
 *          오류 종류는 글 키다: 전송 · 공통 오류는 `code<OnlineError 번호>`, 업무 결과가 Ok 가 아니면 `<동작>.result<결과 번호>`.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "OnlineLoadBot/LoadBotScenario.h"

namespace sw
{
    /** @brief 동작 하나의 요약입니다. */
    struct LoadBotActionSummary
    {
        string _actionName{};
        int64  _p50Us{ 0 };
        int64  _p95Us{ 0 };
        int64  _p99Us{ 0 };
        int64  _maxUs{ 0 };
        int64  _count{ 0 };
        int64  _errorCount{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class LoadBotMetrics
     * @brief 부하 시험 지표입니다.
     */
    class LoadBotMetrics
    {
    public:
        LoadBotMetrics();

        /** @brief 끝난 동작 하나 — @p errorKey 가 비면 성공입니다. */
        void recordLatency( LoadBotAction action, int64 latencyUs, string_view errorKey );
        void recordPush( uint16 pushKind );
        void recordConnection( bool bOpened );
        void recordDisconnect();
        void recordMatch( uint64 matchId );

        void   summarize( vector<LoadBotActionSummary>& outListSummary ) const;
        string formatTable( int64 elapsedMs ) const;
        string formatJson( const LoadBotScenario& scenario, int64 elapsedMs ) const;

        int64 getCompletedCount( LoadBotAction action ) const;
        int64 getErrorCount( LoadBotAction action ) const;
        int64 getTotalCompletedCount() const;
        int64 getTotalErrorCount() const;
        int64 getErrorKeyCount( string_view errorKey ) const;
        int64 getPushCount( uint16 pushKind ) const;
        int64 getOpenedConnectionCount() const { return _openedCount; }
        int64 getFailedConnectionCount() const { return _failedCount; }
        int64 getDisconnectCount() const { return _disconnectCount; }
        /** @brief 서로 다른 경기 id 수입니다. */
        int64 getDistinctMatchCount() const;

        /** @brief 정렬한 표본의 백분위(가장 가까운 순위 — 0 < p ≤ 100)입니다. 비면 0. */
        static int64 computePercentile( const vector<int64>& listSortedSample, int32 percentile );

    private:
        vector<vector<int64>> _listSampleByAction; ///< 동작 번호 → 지연 표본
        vector<int64>         _listErrorCountByAction;
        vector<uint64>        _listMatchId;
        map<string, int64>    _mapErrorKeyToCount;
        map<uint16, int64>    _mapPushKindToCount;
        int64                 _openedCount;
        int64                 _failedCount;
        int64                 _disconnectCount;
    };
} // namespace sw
