/**
 * @file ServiceScheduler.h
 * @brief 일일 · 주간 · 기간 작업 — 회차마다 **서버 여럿 중 정확히 하나**가 돕니다(저장소의 "없어야 한다" 조건으로 회차를 차지). 시각은 UTC 벽시계 밀리초를 넘겨받습니다.
 * @details - 회차 = 일정이 정한 시각(일일 HH:MM · 주간 요일 + HH:MM · 기간 [시작, 끝) 한 번). 서버가 꺼져 지나친 회차는 **가장 최근 하나만** 돈다(밀린 일일 보상 7 번 X).
 *          - 차지 레코드 표 `service_schedule`, 키 `<작업 id>/<회차 시각 16 진>` = {서버 id · 시작 · 끝 · 상태}. 돌던 서버가 죽으면 `_leaseMs` 뒤 다른 서버가 이어받는다(판 조건) —
 *            그래서 처리기는 멱등이어야 한다(원장 분개 키에 회차를 넣는다 — 예: `sched.daily_reward/<회차>`).
 *          - 처리기는 서비스 스레드(`pollCompletions` 를 부르는 스레드)에서 불리고, 저장 일은 스스로 맡긴다. 끝나면 `completeRun` 으로 알린다.
 *          - 차지 일의 `complete` 가 이 객체를 부르므로, 저장소의 완료를 모두 거두거나 저장소를 먼저 내린 뒤에 이 객체를 지운다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class IServiceStore;

    /** @brief 일정 종류입니다. */
    enum class ScheduleKind : uint8
    {
        Daily = 0,
        Weekly,
        Window
    };
} // namespace sw

namespace sw
{
    /** @brief 작업 하나의 일정입니다. */
    struct ScheduleDefinition
    {
        string       _jobID{}; ///< `[0-9a-z_.]`
        int64        _windowStartMs{ 0 };
        int64        _windowEndMs{ 0 };
        int64        _leaseMs{ 10 * 60 * 1000 };
        int32        _minuteOfDay{ 0 }; ///< UTC 0..1439
        int32        _dayOfWeek{ 0 };   ///< Weekly — 0 = 월요일(ISO)
        ScheduleKind _kind{ ScheduleKind::Daily };
    };
} // namespace sw

namespace sw
{
    /** @brief 차지한 회차 하나입니다. */
    struct ScheduledRun
    {
        string _jobID{};
        int64  _occurrenceMs{ 0 }; ///< 회차의 정한 시각
        uint64 _runToken{ 0 };     ///< `completeRun` 에 돌려준다(차지 레코드의 판)
    };
} // namespace sw

namespace sw
{
    /** @brief 회차를 차지했을 때 불리는 처리기입니다. */
    class SW_GF_API IScheduledJobHandler
    {
    public:
        IScheduledJobHandler()          = default;
        virtual ~IScheduledJobHandler() = default;

        IScheduledJobHandler( const IScheduledJobHandler& )            = delete;
        IScheduledJobHandler& operator=( const IScheduledJobHandler& ) = delete;

        virtual void onScheduledRun( const ScheduledRun& run ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ServiceScheduler
     * @brief 서버 하나의 예약 작업 표입니다. 서비스 스레드 하나가 `tick` 한다.
     */
    class SW_GF_API ServiceScheduler
    {
    public:
        ServiceScheduler();
        ~ServiceScheduler();

        ServiceScheduler( const ServiceScheduler& )            = delete;
        ServiceScheduler& operator=( const ServiceScheduler& ) = delete;

        void initialize( IServiceStore* pStore, uint64 serverID );
        void shutdown();

        /** @brief 작업을 올립니다. 일정이 틀리면(작업 id 문자 · 시각 범위 · 임대) 경고를 남기고 올리지 않는다 — 올렸으면 true 입니다. */
        bool registerJob( const ScheduleDefinition& definition, IScheduledJobHandler* pHandler );
        /** @brief 회차가 된 작업을 저장소에서 차지해 보고, 차지한 것만 처리기를 부릅니다(차지는 일 — 결과는 다음 `pollCompletions` 에). */
        void tick( int64 nowMs );
        /** @brief 처리기가 일을 마쳤음을 차지 레코드에 적습니다(끝 시각은 마지막 `tick` 시각. 임대가 지나 다른 서버가 이어받았으면 적지 않는다). */
        void completeRun( const ScheduledRun& run, bool bSucceeded );

        /** @brief 저장소에 맡겨 아직 거두지 않은 일의 수입니다. */
        int32 getPendingWorkCount() const { return _pendingWorkCount; }

        /** @brief @p nowMs 직전(포함)의 회차 시각입니다(시험 · 표시). 없으면 −1. */
        static int64                computeLatestOccurrence( const ScheduleDefinition& definition, int64 nowMs );
        static bool                 isValidDefinition( const ScheduleDefinition& definition );
        static const hashed_string& getTable(); ///< "service_schedule"

    private:
        class ClaimWork;
        class FinishWork;

        /** @brief 차지 일의 결과입니다. */
        enum class ClaimOutcome : uint8
        {
            Claimed = 0, ///< 이 서버가 돈다
            Finished,    ///< 이미 누군가 끝냈다
            Busy,        ///< 다른 서버가 도는 중 — 임대가 지나면 다시 본다
            Retry        ///< 저장소에 닿지 못했다
        };

        struct Job
        {
            ScheduleDefinition    _definition{};
            IScheduledJobHandler* _pHandler{ nullptr };
            int64                 _handledOccurrenceMs{ -1 }; ///< 이 서버가 돌았거나 끝난 것을 본 마지막 회차
            int64                 _retryOccurrenceMs{ -1 };   ///< `_retryAtMs` 가 걸린 회차
            int64                 _retryAtMs{ 0 };
            uint8                 _bClaimPending{ SW_FALSE };
        };

        void onClaimCompleted( size_t jobIndex, int64 occurrenceMs, ClaimOutcome outcome, uint64 runToken, int64 retryAtMs );
        void onWorkCompleted() { --_pendingWorkCount; }

        vector<Job>    _listJob;
        IServiceStore* _pStore;
        uint64         _serverID;
        int64          _lastTickMs;
        int32          _pendingWorkCount;
    };
} // namespace sw
