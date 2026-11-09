/**
 * @file BusyClock.h
 * @brief 일이 하나라도 진행 중이던 벽시계 시간을 세는 시계입니다.
 * @details 여러 스레드가 겹쳐 일해도 겹친 구간은 한 번만 센다(구간의 합집합). 일의 시간 합을 세면 병렬 컴파일 넷이 1 초를 4 초로 센다.
 *          자동화의 시나리오 시한이 셰이더 컴파일 시간을 빼는 데 쓴다 — 빈 캐시의 첫 실행이 일하는 중에 시한으로 지지 않게
 *          (언리얼 자동화도 셰이더 컴파일을 기다리는 동안을 테스트 시간으로 보지 않는다: `FShaderCompilingManager::FinishAllCompilation`).
 *          시각은 부르는 쪽이 넘긴다(`MonotonicClock::nowNanoseconds`) — 테스트가 시각을 고정한다.
 */
#pragma once
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/MonotonicClock.h"

namespace sw
{
    /**
     * @class BusyClock
     * @brief `begin` ~ `end` 구간의 합집합 길이를 셉니다(스레드 안전). 파일 머리말 참고.
     */
    class BusyClock
    {
    public:
        BusyClock()
            : _mutex{}
            , _activeCount{ 0 }
            , _busySinceNanoseconds{ 0 }
            , _busyNanoseconds{ 0 }
        {
        }

        /** @brief 일 하나를 시작합니다. 진행 중인 일이 없었으면 바쁜 구간이 이때 시작합니다. */
        void begin( int64 nowNanoseconds )
        {
            ScopedLock<mutex> lock{ _mutex };
            if ( _activeCount++ == 0 )
                _busySinceNanoseconds = nowNanoseconds;
        }

        /** @brief 일 하나를 끝냅니다. 마지막 일이었으면 바쁜 구간을 닫아 더합니다. */
        void end( int64 nowNanoseconds )
        {
            ScopedLock<mutex> lock{ _mutex };
            SW_ASSERT( _activeCount > 0 ); // begin 없는 end
            if ( --_activeCount == 0 )
                _busyNanoseconds += nowNanoseconds - _busySinceNanoseconds;
        }

        /** @brief 지금까지 바빴던 나노초입니다 — 진행 중인 구간은 @p nowNanoseconds 까지 셉니다. */
        [[nodiscard]] int64 getBusyNanoseconds( int64 nowNanoseconds ) const
        {
            ScopedLock<mutex> lock{ _mutex };
            return _busyNanoseconds + ( _activeCount > 0 ? nowNanoseconds - _busySinceNanoseconds : 0 );
        }

        /** @brief 표시(그때의 `getBusyNanoseconds` 값 @p busyAtMarkNanoseconds) 뒤로 바빴던 초입니다. */
        [[nodiscard]] float64 getBusySecondsSince( int64 busyAtMarkNanoseconds, int64 nowNanoseconds ) const
        {
            return static_cast<float64>( getBusyNanoseconds( nowNanoseconds ) - busyAtMarkNanoseconds ) / static_cast<float64>( constant::time::kNanosecondsPerSecond );
        }

    private:
        mutable mutex               _mutex;
        uint32 _activeCount         SW_GUARDED_BY( _mutex ); ///< 진행 중인 일 수
        int64 _busySinceNanoseconds SW_GUARDED_BY( _mutex ); ///< 지금 바쁜 구간이 시작한 시각(진행 중일 때만 뜻이 있다)
        int64 _busyNanoseconds      SW_GUARDED_BY( _mutex ); ///< 닫힌 바쁜 구간의 합
    };

    /**
     * @class BusyScope
     * @brief 범위 동안 일 하나를 `BusyClock` 에 셉니다(지금 시각은 `MonotonicClock`). 끝을 잊는 길이 없게 begin · end 를 짝짓는다.
     */
    class BusyScope
    {
    public:
        explicit BusyScope( BusyClock& clock )
            : _clock{ clock }
        {
            _clock.begin( MonotonicClock::nowNanoseconds() );
        }
        ~BusyScope() { _clock.end( MonotonicClock::nowNanoseconds() ); }
        BusyScope( const BusyScope& )            = delete;
        BusyScope& operator=( const BusyScope& ) = delete;

    private:
        BusyClock& _clock;
    };
} // namespace sw
