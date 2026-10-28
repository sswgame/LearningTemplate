/**
 * @file ServiceStoreUtil.h
 * @brief 서비스 저장소를 쓰는 쪽의 공통 도우미입니다. 종료할 때 요청한 저장 작업을 시간 제한 안에서 회수하는 대기를 제공합니다.
 */
#pragma once
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/MonotonicClock.h"

#include "GameFramework/Base/Online/Store/ServiceStore.h"

namespace sw
{
    /** @brief 서비스 저장소 사용 쪽 도우미입니다. */
    struct ServiceStoreUtil
    {
        /** @brief 종료 대기의 시간 제한(ms)입니다. 넘기면 남은 작업은 호출부가 실패로 정리한다. */
        static constexpr int64 kShutdownWaitMilliseconds = 5000;
        /** @brief 회수한 완료가 없을 때 다음 회수까지 쉬는 시간(1 ms)입니다. */
        static constexpr int64 kIdlePollSleepNanoseconds = constant::kNanosecondsPerMillisecond;

        /**
         * @brief @p isIdle() 이 true 가 되거나 시간 제한이 지날 때까지 @p pStore 의 완료를 거둡니다. @p pStore 가 null 이면 바로 돌아갑니다.
         * @details 걸음마다 @p onPoll() 을 먼저 부른 뒤 `pollCompletions` 를 부르고, 회수한 것이 없으면 1 ms 쉰다.
         *          @p onPoll 은 저장소 완료 말고도 거둘 것이 있는 서비스(영수증 검증 등)가 넘긴다. 남은 작업의 정리와 오류 로그는 호출부가 한다.
         * @code
         *     ServiceStoreUtil::drainUntilIdle( _pStore, [this]() { return _listPendingCall.empty(); }, [this]() { pollReceipts(); } );
         * @endcode
         */
        template <typename TIsIdle, typename TPoll>
        static void drainUntilIdle( IServiceStore* pStore, TIsIdle&& isIdle, TPoll&& onPoll )
        {
            if ( pStore == nullptr )
                return;
            const Deadline deadline = Deadline::afterMilliseconds( kShutdownWaitMilliseconds );
            while ( isIdle() == false && deadline.isExpired() == false )
            {
                onPoll();
                if ( pStore->pollCompletions() == 0 )
                    MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + kIdlePollSleepNanoseconds );
            }
        }

        /** @brief 저장소 완료만 회수하는 `drainUntilIdle` 입니다. */
        template <typename TIsIdle>
        static void drainUntilIdle( IServiceStore* pStore, TIsIdle&& isIdle )
        {
            drainUntilIdle( pStore, isIdle, []() {} );
        }
    };
} // namespace sw
