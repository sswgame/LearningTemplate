#include "pch.h"

#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Profiling/MemoryBudgetMonitor.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 메모리 태그 개발 명령입니다(배포본에는 등록되지 않는다 — `SW_DEV_COMMAND`). 언리얼 `stat llm` · `memreport` 콘솔 명령 자리.
         * @details 예전에는 테스트용 변수 둘(`-gv_memoryReport` · `-gv_memoryTracking`)을 감시가 프레임마다 읽었다. 묻는 쪽이 부르는 명령으로 바꿔 런타임 프레임에서 개발 분기를 뺐다.
         *          시작부터는 `-gv_devConsoleExec=memory.tracking on`(시작 씬이 열린 뒤 — 기동 중 할당까지 세려면 Debug 구성을 쓴다).
         */
        struct MemoryCommandsInternal
        {
            /** @brief `memory.report`: 태그 표(살아 있는 · 최고치 · 예산)를 로그로 한 번 남깁니다. */
            static bool runReport( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                const MemoryProfiler* pProfiler = MemoryProfiler::getActive();
                if ( pProfiler == nullptr )
                {
                    outReply = "memory: no memory profiler in this build";
                    return true;
                }
                MemoryBudgetMonitor::logMemoryReport( *pProfiler, "memory.report" );
                outReply = pProfiler->isTrackingEnabled() ? "memory: tag table logged" : "memory: tag table logged - tracking is off, the counts did not move (memory.tracking on)";
                return true;
            }

            /** @brief `memory.tracking [on|off]`: 메모리 태그 추적을 켜고 끕니다(인자가 없으면 지금 상태를 답한다). 켜거나 끄면 최고치를 다시 잰다 — 꺼진 동안의 할당은 세지 않았다. */
            static bool runTracking( const vector<string>& listArgument, string& outReply )
            {
                MemoryProfiler* pProfiler = MemoryProfiler::getActive();
                if ( pProfiler == nullptr )
                {
                    outReply = "memory: no memory profiler in this build";
                    return true;
                }
                if ( listArgument.size() > 1 )
                    return false;
                if ( listArgument.size() == 1 )
                {
                    bool bOn = false;
                    if ( StringUtil::tryParseBool( listArgument[0], bOn ) == false )
                        return false;
                    pProfiler->setTrackingEnabled( bOn );
                    pProfiler->resetPeaks();
                }
                outReply = pProfiler->isTrackingEnabled() ? "memory tracking: on" : "memory tracking: off";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( MemoryReport, "memory.report", "memory.report", "Log the memory tag table once (live, peak, budget)", &MemoryCommandsInternal::runReport );
    SW_DEV_COMMAND( MemoryTracking, "memory.tracking", "memory.tracking [on|off]", "Show or switch memory tag tracking (peaks restart when switched)", &MemoryCommandsInternal::runTracking );
} // namespace sw
