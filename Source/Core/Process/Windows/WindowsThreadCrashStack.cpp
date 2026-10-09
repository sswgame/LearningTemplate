#include "pch.h"

#include "Core/Process/ThreadCrashStack.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"

namespace sw
{
    namespace
    {
        struct WindowsThreadCrashStackInternal
        {
            /**
             * @brief 넘친 뒤 핸들러가 쓸 스택 자리입니다(`SetThreadStackGuarantee`).
             * @details 스택 오버플로는 스택이 바닥난 채로 필터에 들어옵니다. 자리가 없으면 필터의 첫 호출에서 다시 넘쳐 **아무것도 남지 않습니다**
             *          (실측: 덤프 0 바이트). 미니덤프 · 심볼 변환 · 8 KB 문자열 버퍼가 차례로 들어가는 크기입니다 — 실측으로 64 KB 에서 덤프가 온전합니다.
             */
            static constexpr ULONG kStackGuaranteeBytes = 64 * 1024;
        };
    } // namespace
} // namespace sw

namespace sw
{
    void ThreadCrashStack::initializeCurrentThread()
    {
        ULONG guaranteeBytes = WindowsThreadCrashStackInternal::kStackGuaranteeBytes;
        SetThreadStackGuarantee( &guaranteeBytes );
    }

    void ThreadCrashStack::releaseCurrentThread() {}
} // namespace sw

#endif
