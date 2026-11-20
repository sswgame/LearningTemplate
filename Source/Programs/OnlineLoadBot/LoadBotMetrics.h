/**
 * @file LoadBotMetrics.h
 * @brief 부하 테스트 지표 — 동작마다 지연 표본(마이크로초) · 오류 수, 오류 종류별 수, 알림 종류별 수, 연결 사건, 경기 id. 끝에 백분위 표와 JSON 을 냅니다.
 * @details 표본을 모두 들고 끝에 정렬한다(봇 수천 × 동작 수십 = 수십만 개 — 수 MB, 백분위가 정확하다). 실행기 스레드 하나에서 쓴다.
 *          오류 종류는 글 키다: 전송 · 공통 오류는 `code<OnlineError 번호>`, 업무 결과가 Ok 가 아니면 `<동작>.result<결과 번호>`.
 *          원장 경로의 지표(감사 O-12): `kBusy` 비율(봇이 받은 `code11` / 끝낸 동작), 저장소 Conflict 재시도율(서버 저장소 앞의 충돌 / 커밋), 끝의 원장 감사 균형,
 *          도착률 실행기가 풀이 모자라 버린 도착 수. 서버 쪽 값은 같은 프로세스 서버(`--local-server`)만 있어 `setServerStats` 로 받는다.
 */
#pragma once
#include "Core/Container/flat_map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "Programs/OnlineLoadBot/LoadBotScenario.h"

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
    /** @brief 같은 프로세스 서버에서 읽은 값입니다(`--server` 로 붙으면 서버를 읽을 수 없어 비어 있다 — `_bRead` 거짓). */
    struct LoadBotServerStats
    {
        int64 _commitCount{ 0 };           ///< 저장소 앞의 커밋 수(`ServiceStoreCommitStats`)
        int64 _conflictCount{ 0 };         ///< 그 가운데 버전 충돌 — 서비스가 다시 읽고 다시 했다
        int32 _assetCount{ 0 };            ///< 감사한 자산 수
        int32 _mismatchedAssetCount{ 0 };  ///< 보유 != 발행 - 소각 인 자산 수
        int32 _journalCount{ 0 };          ///< 감사가 훑은 분개 수
        int32 _negativeEscrowCount{ 0 };   ///< 음수 맡김(위반)
        int32 _unreadableRecordCount{ 0 }; ///< 읽지 못한 원장 레코드(위반)
        uint8 _bRead{ SW_FALSE };          ///< 서버 값을 읽었다(같은 프로세스 서버)
        uint8 _bBalanced{ SW_FALSE };      ///< 원장 감사가 균형이다(`LedgerAuditReport::isBalanced`)
    };
} // namespace sw

namespace sw
{
    /**
     * @class LoadBotMetrics
     * @brief 부하 테스트 지표입니다.
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
        void recordMatch( uint64 matchID );
        /** @brief 도착률 실행기가 풀이 바닥나 띄우지 못한 도착 하나(k6 `dropped_iterations`). */
        void recordDroppedArrival() { ++_droppedArrivalCount; }
        void setServerStats( const LoadBotServerStats& serverStats ) { _serverStats = serverStats; }

        void   summarize( vector<LoadBotActionSummary>& outListSummary ) const;
        string formatTable( int64 elapsedMs ) const;
        /** @brief 보고서 JSON 글입니다. 시나리오 이름 등에 잘못된 UTF-8 이 있으면 오류입니다(`JSONDocument::dump`). */
        [[nodiscard]] expected<string> formatJSON( const LoadBotScenario& scenario, int64 elapsedMs ) const;

        int64 getCompletedCount( LoadBotAction action ) const;
        /** @brief 오류로 끝난 동작 수입니다(동작마다). */
        int64 getErrorCount( LoadBotAction action ) const;
        int64 getTotalCompletedCount() const;
        int64 getTotalErrorCount() const;
        int64 getErrorKeyCount( string_view errorKey ) const;
        /** @brief 받은 푸시 수입니다(종류마다). 받은 적 없는 종류는 0 입니다. */
        int64 getPushCount( uint16 pushKind ) const;
        int64 getOpenedConnectionCount() const { return _openedCount; }
        int64 getFailedConnectionCount() const { return _failedCount; }
        int64 getDisconnectCount() const { return _disconnectCount; }
        /** @brief 서로 다른 경기 id 수입니다. */
        int64                     getDistinctMatchCount() const;
        int64                     getDroppedArrivalCount() const { return _droppedArrivalCount; }
        const LoadBotServerStats& getServerStats() const { return _serverStats; }
        /** @brief 봇이 받은 `kBusy`(저장소 충돌을 서버 재시도 안에 풀지 못함) 수입니다. */
        int64 getBusyCount() const;

        /** @brief 정렬한 표본의 백분위(가장 가까운 순위 — 0 < p ≤ 100)입니다. 비면 0. */
        static int64 computePercentile( const vector<int64>& listSortedSample, int32 percentile );

    private:
        vector<vector<int64>>   _listSampleByAction; ///< 동작 번호 → 지연 표본
        vector<int64>           _listErrorCountByAction;
        vector<uint64>          _listMatchID;
        flat_map<string, int64> _mapErrorKeyToCount;
        flat_map<uint16, int64> _mapPushKindToCount;
        int64                   _openedCount;
        int64                   _failedCount;
        int64                   _disconnectCount;
        int64                   _droppedArrivalCount;
        LoadBotServerStats      _serverStats;
    };
} // namespace sw
