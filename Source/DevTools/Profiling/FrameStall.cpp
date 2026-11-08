#include "pch.h"

#include "DevTools/Profiling/FrameStall.h"

#include "Core/Concurrency/atomic.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "DevTools/DevToolsHost.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineServices.h"

namespace sw
{
    namespace
    {
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
                    outReply = formatToString( "stall must be 1..%# ms", kMaxStallMilliseconds );
                    return true;
                }
                DevToolsHost* const pHost = engine::findSubsystem<DevToolsHost>();
                if ( pHost == nullptr )
                {
                    outReply = "no dev tools host in this process";
                    return true;
                }
                pHost->getFrameStall().request( milliseconds );
                outReply = formatToString( "next game frame stalls %# ms", milliseconds );
                return true;
            }
        };
    } // namespace

    FrameStall::FrameStall()
        : _pendingStallMilliseconds{ 0 }
    {
    }

    void FrameStall::request( int32 milliseconds )
    {
        _pendingStallMilliseconds.store( milliseconds, std::memory_order_relaxed );
    }

    void FrameStall::consume()
    {
        const int32 milliseconds = _pendingStallMilliseconds.exchange( 0, std::memory_order_relaxed );
        if ( milliseconds <= 0 )
            return;
        MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + static_cast<int64>( milliseconds ) * constant::time::kNanosecondsPerMillisecond );
    }

    SW_DEV_COMMAND( ProfileStall, "profile.stall", "profile.stall <ms>", "Hold the next game thread frame for that many milliseconds (profiler spike checks)",
                    &FrameStallInternal::runStall );
} // namespace sw
