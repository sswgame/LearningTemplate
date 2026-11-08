/**
 * @file CrashInjection.h
 * @brief 크래시 보고 경로를 일부러 태워 보는 개발 기능입니다 — 죽는 방식마다 하나(`CrashInjectionKind`). 개발 도구(`DevTools`)에 있다 — 배포 호스트는 DevTools 를 링크하지 않는다.
 * @details 예전에는 런타임 보고기(`CrashHandler`) 안에 `crashForTest` 와 죽는 도우미가 있어 배포 바이너리에 실려 나갔다. 보고기는 보고만 하고,
 *          일부러 죽는 일은 개발 쪽이 맡는다(언리얼 `debug crash` · `debug stackoverflow` 콘솔 명령 자리). 부르는 곳: `-gv_crashTest`(`CrashInjectionSubsystem` — 렌더러가 선 뒤, `DevTools/Client`) · 크래시 보고를 끝까지 태우는 테스트.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    /**
     * @brief 개발 중 크래시 리포트 경로를 일부러 태워 보는 방법입니다(`CrashInjector::crash`).
     * @details 언리얼의 `debug crash` · `debug stackoverflow` · `debug gpf` 콘솔 명령과 같은 역할입니다. 리포트는 크래시가 나야만
     *          만들어지므로 죽는 방식마다 한 번씩 태워 보지 않으면 배포 뒤에야 "그 방식으로 죽으면 아무것도 안 남는다" 를 압니다.
     */
    enum class CrashInjectionKind : uint8
    {
        None = 0,
        AccessViolation,     ///< 널 포인터 쓰기
        StackOverflow,       ///< 이 스레드에서 끝없는 재귀
        WorkerStackOverflow, ///< 새 스레드에서 끝없는 재귀(스레드별 준비 `ThreadCrashStack::initializeCurrentThread` 를 태운다)
        Abort,               ///< std::abort() — 엔진의 "로그 + abort" 치명 경로
        PureVirtualCall,     ///< 생성 중인 객체의 순수 가상 함수 호출
        StderrHeld,          ///< 다른 스레드가 stderr 를 쥔 채 놓지 않는 동안 접근 위반 — 보고가 끝날 수 없다(시한이 끝내야 한다)
        AssertFailure,       ///< `SW_ASSERT` 실패(Debug 에서만 멈춘다 — 그 밖의 빌드는 아무것도 하지 않고 돌아온다)
        Count
    };
} // namespace sw

namespace sw
{
    /** @struct CrashInjector @brief 일부러 죽는 방법들입니다. */
    struct SW_DEVTOOLS_API CrashInjector
    {
        /** @brief 일부러 죽습니다(`CrashInjectionKind` 설명). 돌아오지 않습니다. `None` · 모르는 값이면 아무것도 하지 않습니다. 설치된 처리기가 보고합니다. */
        static void crash( CrashInjectionKind kind );
    };
} // namespace sw
