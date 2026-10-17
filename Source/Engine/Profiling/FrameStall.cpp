#include "pch.h"

#include "Engine/Profiling/FrameStall.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#if SW_DEV_COMMANDS_ENABLED
namespace sw
{
    namespace
    {
        /** @brief 다음 `consume` 이 붙잡을 시간(밀리초)입니다. 0 이면 걸린 지연이 없습니다. 개발 명령이 쓰고 게임 스레드가 비웁니다. */
        atomic<int32> s_pendingStallMilliseconds{ 0 };

        struct FrameStallInternal
        {
            /** @brief 한 번에 걸 수 있는 가장 긴 지연(밀리초)입니다. 잘못 친 값이 시나리오 시한보다 오래 창을 멈추지 않게 합니다. */
            static constexpr int32 kMaxStallMilliseconds = 5000;

            /** @brief `profile.stall <ms>`: 다음 게임 스레드 프레임을 그만큼 붙잡습니다(프로파일러 스파이크 정지 확인). */
            static bool runStall( const vector<string>& listArgument, string& outReply )
            {
                int32 milliseconds = 0;
                if ( listArgument.size() != 1 || StringUtil::parseInt( listArgument[0], milliseconds ) == false )
                    return false;
                const bool bInRange = 1 <= milliseconds && milliseconds <= kMaxStallMilliseconds;
                if ( bInRange == false )
                {
                    outReply = "stall must be 1.." + to_string( kMaxStallMilliseconds ) + " ms";
                    return true;
                }
                FrameStall::request( milliseconds );
                outReply = "next game frame stalls " + to_string( milliseconds ) + " ms";
                return true;
            }
        };
    } // namespace

    void FrameStall::request( int32 milliseconds )
    {
        s_pendingStallMilliseconds.store( milliseconds, std::memory_order_relaxed );
    }

    void FrameStall::consume()
    {
        const int32 milliseconds = s_pendingStallMilliseconds.exchange( 0, std::memory_order_relaxed );
        if ( milliseconds <= 0 )
            return;
        MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + static_cast<int64>( milliseconds ) * constant::kNanosecondsPerMillisecond );
    }

    SW_DEV_COMMAND( ProfileStall, "profile.stall", "profile.stall <ms>", "Hold the next game thread frame for that many milliseconds (profiler spike checks)",
                    &FrameStallInternal::runStall );
} // namespace sw
#endif
