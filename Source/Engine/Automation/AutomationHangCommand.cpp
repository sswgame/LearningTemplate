#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

// 개발 명령 `automation.hang <seconds>` — 행 감시(`AutomationHangWatchdog`)를 통합에서 시험하는 교착 시나리오가 게임 스레드를 멈추는 데 쓴다.
// `profile.stall` 은 5 초가 상한이라(`FrameStall`) 시나리오 상한 + 유예(`wallTimeoutSeconds` 3 + 5 = 8 초)를 넘기지 못한다.
#if SW_DEV_COMMANDS_ENABLED
namespace sw
{
    namespace
    {
        struct AutomationHangCommandInternal
        {
            /**
             * @brief 한 번에 멈출 수 있는 가장 긴 시간(초)입니다. 행 감시가 먼저 프로세스를 끝내므로 정상 실행에서는 닿지 않는다 — 감시 없이 손으로 부른 실행이
             *        영영 멈추지 않게 하는 상한이고, 프로세스 시한(`AutomationTimeLimits::kProcessKillSeconds` 180)보다 짧다.
             */
            static constexpr int32 kMaxHangSeconds = 60;

            /** @brief `automation.hang <seconds>`: 호출한 게임 스레드를 그만큼 붙잡습니다(프레임 경계를 지나지 않는다). */
            static bool runHang( const vector<string>& listArgument, string& outReply )
            {
                int32 seconds = 0;
                if ( listArgument.size() != 1 || StringUtil::parseInt( listArgument[0], seconds ) == false )
                    return false;
                const bool bInRange = 1 <= seconds && seconds <= kMaxHangSeconds;
                if ( bInRange == false )
                {
                    outReply = "hang must be 1.." + to_string( kMaxHangSeconds ) + " s";
                    return true;
                }
                MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + static_cast<int64>( seconds ) * constant::kNanosecondsPerSecond );
                outReply = "the game thread was held for " + to_string( seconds ) + " s";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( AutomationHang, "automation.hang", "automation.hang <seconds>", "Block the calling game thread for that many seconds (hang watchdog checks)",
                    &AutomationHangCommandInternal::runHang );
} // namespace sw
#endif
