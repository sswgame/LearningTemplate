/**
 * @file TestWait.h
 * @brief 테스트의 조건 대기 — 다른 스레드 · OS(소켓 · 자식 프로세스 · IO 스레드)가 상태를 바꾸기를 기다립니다.
 * @details 통과는 조건이 서는 것이고, 시한은 멈춘 실행을 잡는 안전 그물이다(시나리오 `<WaitUntil>` 과 같은 모양). 고정 시간을 자고 "이제 됐겠지" 로
 *          단언하지 않는다 — 기계가 느리면 옳은 구현이 지고, 빠르면 잠만 낭비한다. 시간이 판정에 들어가는 대기는 시계 주입(`ManualClock`)이나
 *          사건(문 · 세대 번호)으로 바꾼다. 조건을 다시 보는 간격만 여기 한 곳에 둔다(테스트마다 손으로 짠 잠 · 시한 고리를 두지 않는다).
 *          GoogleTest 의 `EXPECT_EVENTUALLY` · Catch2 의 대기 도우미와 같은 자리.
 */
#pragma once
#include "Core/Concurrency/thread.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/MonotonicClock.h"

namespace sw::test
{
    /** @brief 조건 대기의 기본 시한(초)입니다. 멈춘 실행을 잡는 그물이라 넉넉하다 — 옳은 실행은 조건이 서는 대로 돌아온다. */
    inline constexpr float64 kDefaultWaitSeconds = 10.0;

    /**
     * @brief @p predicate 가 참이 될 때까지 기다립니다(최대 @p timeoutSeconds). 시한이 지나면 false 입니다 — 부르는 쪽이 단언한다.
     * @details 조건은 1 ms 마다 다시 본다. 조건을 내는 쪽이 사건(문 · future)을 주면 그것을 기다린다 — 이 함수는 사건이 없는 상태(카운터 · 연결 수)에 쓴다.
     */
    template <typename FPredicate>
    [[nodiscard]] bool waitUntil( FPredicate&& predicate, float64 timeoutSeconds = kDefaultWaitSeconds )
    {
        const Deadline deadline = Deadline::afterMilliseconds( static_cast<int64>( timeoutSeconds * static_cast<float64>( constant::time::kMillisecondsPerSecond ) ) );
        while ( predicate() == false )
        {
            if ( deadline.isExpired() )
                return false;
            ThreadUtil::sleepForMilliseconds( 1 );
        }
        return true;
    }
} // namespace sw::test
