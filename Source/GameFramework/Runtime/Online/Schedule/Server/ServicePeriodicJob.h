/**
 * @file ServicePeriodicJob.h
 * @brief 서비스의 주기 작업 하나(정리 · 다시 읽기 · 알림 가져가기)의 상태 기계입니다 — 주기 · 요청 · 도는 중 · 차지한 예약 회차.
 * @details 서비스마다 `_bXPending` · `_nextXMs` · `_bXRunHeld` 짝을 손으로 돌렸다(우편 셋, 경제 감사, 보존 쓸기, 다시 읽기 둘). 깃발 하나를 빠뜨리면 같은 일이 겹쳐 돌거나
 *          회차를 돌려주지 않아 다음 회차가 밀렸다. UE `FTSTicker` 의 지연 + 진행 중 표시, Unity `InvokeRepeating` 과 같은 자리이고, 도는 중이면 다시 시작하지 않는다.
 *          - 주기가 0 이면 요청(`request` · `holdRun`)으로만 돈다(예약 테이블이 회차를 부르는 작업).
 *          - 처음에는 주기가 곧 지난 상태다 — 주기가 있으면 첫 틱에 한 번 돈다.
 *          서비스의 틱(맡긴 스레드)에서만 쓴다. 저장소 일 자체는 `ServiceStoreTracker::submit` 으로 맡긴다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/Runtime/Online/Schedule/Server/Server/ServiceScheduler.h"

namespace sw
{
    /** @brief 주기 작업 하나의 일정입니다. */
    class ServicePeriodicJob
    {
    public:
        ServicePeriodicJob()
            : _run{}
            , _nextDueMs{ 0 }
            , _periodMs{ 0 }
            , _bRequested{ SW_FALSE }
            , _bRunning{ SW_FALSE }
            , _bRunHeld{ SW_FALSE }
        {
        }

        /** @brief 주기(ms)를 정합니다. 0 이면 요청으로만 돈다. */
        void  setPeriod( int64 periodMs ) { _periodMs = periodMs; }
        int64 getPeriod() const { return _periodMs; }

        /** @brief 다음 틱에 돌게 합니다(다른 서버가 바꿨다는 알림 · 상한까지 채워 남은 것이 있다). */
        void request() { _bRequested = SW_TRUE; }

        /** @brief 예약 테이블이 차지한 회차 @p run 을 들고 돌게 합니다. 끝나면 `takeHeldRun` 으로 돌려준다. */
        void holdRun( const ScheduledRun& run )
        {
            _run      = run;
            _bRunHeld = SW_TRUE;
            request();
        }

        /**
         * @brief 지금 돌 차례면 시작 표시를 하고 true 입니다 — 도는 중이 아니고, 요청됐거나 주기가 지났다.
         * @details 시작하면 요청 표시를 지우고 다음 주기를 @p nowMs 에서 센다.
         */
        [[nodiscard]] bool tryStart( UnixMilliseconds nowMs )
        {
            if ( _bRunning == SW_TRUE )
                return false;
            const bool bDue = _bRequested == SW_TRUE || ( _periodMs > 0 && nowMs >= _nextDueMs );
            if ( bDue == false )
                return false;
            _bRequested = SW_FALSE;
            _bRunning   = SW_TRUE;
            _nextDueMs  = nowMs + _periodMs;
            return true;
        }

        /** @brief 한 번 돈 것이 끝났습니다(실패여도 — 다음 주기에 다시 돈다). */
        void finish() { _bRunning = SW_FALSE; }

        /** @brief 차지한 회차가 있으면 @p outRun 에 넘기고 true 입니다. 넘긴 뒤 비운다 — 호출부가 `ServiceScheduler::completeRun` 에 돌려준다. */
        [[nodiscard]] bool takeHeldRun( ScheduledRun& outRun )
        {
            if ( _bRunHeld == SW_FALSE )
                return false;
            outRun    = _run;
            _bRunHeld = SW_FALSE;
            return true;
        }

        bool isRunning() const { return _bRunning == SW_TRUE; }

    private:
        ScheduledRun     _run;       ///< 차지한 회차이다
        UnixMilliseconds _nextDueMs; ///< 주기로 다음에 돌 시각이다
        int64            _periodMs;
        uint8            _bRequested : 1;
        uint8            _bRunning   : 1;
        uint8            _bRunHeld   : 1; ///< 예약 회차를 차지해 도는 중이다
    };
} // namespace sw
