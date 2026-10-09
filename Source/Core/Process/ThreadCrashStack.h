/**
 * @file ThreadCrashStack.h
 * @brief 스레드마다 크래시 보고가 쓸 스택 자리를 준비합니다(스택 오버플로 대비).
 */
#pragma once
#include "Core/Common/Macros.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) ThreadCrashStack — 스레드 시작 때 부르는 준비. 크래시 보고(`CrashHandler`, Diagnostics)보다 아래층이라
    //    작업 스레드를 띄우는 곳(로그 싱크 · 파일 · 태스크)이 보고기를 몰라도 부를 수 있다
    // ------------------------------------------------------------------------------
    /** @brief 스레드별 크래시 스택 준비입니다. */
    struct SW_API ThreadCrashStack
    {
        /**
         * @brief **이 스레드**에서 스택 오버플로가 나도 리포트를 쓸 수 있게 준비합니다. 엔진이 만드는 스레드는 시작할 때 부릅니다.
         * @details 스택 오버플로는 스택이 바닥난 채로 핸들러에 들어옵니다. 준비가 없으면 핸들러가 첫 호출에서 다시 넘쳐 **아무것도 남기지
         *          못합니다**(실측: 덤프 파일 0 바이트). Windows 는 `SetThreadStackGuarantee` 로 넘친 뒤에 쓸 자리를 남겨 두고, POSIX 는
         *          이 스레드 전용 대체 시그널 스택을 깝니다 — `sigaltstack` 은 스레드마다라, `CrashHandler::initialize` 를 부른 스레드만으로는 덮이지 않습니다.
         *          핸들러 설치 전에 불러도 됩니다(로거 작업 스레드가 그렇다). 두 번 불러도 됩니다.
         */
        static void initializeCurrentThread();

        /**
         * @brief 이 스레드의 준비를 걷어 냅니다(POSIX 는 대체 스택을 끄고 돌려줍니다, Windows 는 할 일이 없습니다).
         * @details `CrashHandler::shutdown` 이 부른 스레드의 것을 걷습니다. 다른 스레드의 것은 그 스레드가 끝날 때 돌려줍니다.
         */
        static void releaseCurrentThread();
    };
} // namespace sw
