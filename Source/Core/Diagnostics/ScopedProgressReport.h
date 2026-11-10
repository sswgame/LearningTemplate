/**
 * @file Core/Diagnostics/ScopedProgressReport.h
 * @brief 오래 막히는 호출 하나가 사는 동안 옆 스레드가 일정 간격마다 "아직 하는 중" 한 줄을 로그에 남깁니다.
 * @details 백엔드마다 실행의 정지 감시(`AutomationPerBackend`)는 자식 로그가 자라는지로 일하는 자식과 멈춘 자식을 가른다. 캐시가 빈 실행의 fxc 큰 계산 셰이더
 *          (mesh_morph, 부하 걸린 기계에서 3 분) · 드라이버 프로그램 링크처럼 **호출 하나** 가 정지 시한보다 길면, 끝난 뒤에야 찍히는 한 줄을 기다리다 일하는 자식을 죽였다.
 *          이 범위가 그 호출을 감싸 간격마다 한 줄을 남긴다. 진짜 교착은 이 범위 밖에서 일어나므로 감시는 여전히 잡는다.
 *          자식의 CPU 시간으로 재지 않는 까닭: 게임 · 렌더 스레드가 멈춘 자식도 오디오 스레드가 CPU 를 쓴다 — 교착을 일하는 것으로 본다.
 *          언리얼의 셰이더 컴파일 진행 줄("Compiling shaders (N)")과 같은 자리다.
 */
#pragma once
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/ThreadSafety.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include <condition_variable>

namespace sw
{
    /**
     * @class ScopedProgressReport
     * @brief 범위가 사는 동안 `kIntervalSeconds` 마다 `ProgressReport.StillWorking` 한 줄(무엇 · 지난 초)을 남깁니다.
     * @details 범위마다 스레드 하나를 띄우므로 느린 길(캐시 미스 컴파일 · 프로그램 링크)에만 씌운다. 간격 안에 끝나면 아무것도 찍지 않는다.
     */
    class SW_API ScopedProgressReport
    {
    public:
        /** @brief 줄 간격(초)입니다 — 자동화 정지 시한(`AutomationTimeLimits::kProcessStallSeconds`)보다 짧아야 한다(그쪽 static_assert). */
        static constexpr uint32 kIntervalSeconds = 30;

        /** @brief @p what(로그에 그대로 찍는 일의 이름)을 감싸 보고 스레드를 띄웁니다. */
        explicit ScopedProgressReport( string what );
        /** @brief 보고 스레드를 깨워 끝내고 합류합니다. */
        ~ScopedProgressReport();

        ScopedProgressReport( const ScopedProgressReport& )            = delete;
        ScopedProgressReport& operator=( const ScopedProgressReport& ) = delete;

    private:
        void run();

    private:
        string                      _what;
        int64                       _startNanos{ 0 };
        mutex                       _mutex;
        std::condition_variable_any _cv;
        bool _bDone                 SW_GUARDED_BY( _mutex ){ false }; ///< NOLINT(sw-bool-flag): 조건 변수가 잠금 아래 읽는 값 하나다 — 비트필드는 잠금 계약 주석을 달 수 없다
        thread                      _thread;                          ///< 마지막에 둔다 — 다른 칸이 다 선 뒤에 시작하고, 먼저 합류한다
    };
} // namespace sw
