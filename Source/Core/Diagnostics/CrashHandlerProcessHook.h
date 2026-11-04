/**
 * @file CrashHandlerProcessHook.h
 * @brief 크래시 처리기의 OS 훅(예외 필터 · 시그널 처리기)입니다 — 플랫폼 파일(`Windows/WindowsCrashHandler.cpp` · `Posix/PosixCrashHandler.cpp`)이 구현합니다.
 * @details 크래시 모듈 안쪽 전용입니다. OS 는 훅을 프로세스에 하나만 두므로 이 상태(이전 필터 · 보고 스레드)만 프로세스 전역이고, 보고할 처리기는
 *          `CrashHandler::_s_pInstalled` 로 찾는다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    class CrashHandler;

    /** @struct CrashHandlerProcessHook @brief OS 훅입니다. 처음 처리기가 설치될 때 걸고 마지막이 풀릴 때 뗍니다. */
    struct CrashHandlerProcessHook
    {
        /** @brief 훅을 겁니다(심볼 · 보고 스레드 · 덤프 함수를 미리 준비한다 — 크래시 순간에 로드 · 할당하지 않게). */
        static void install();
        /** @brief 훅을 떼고 이전 처리기를 되돌립니다. */
        static void uninstall();
        /** @brief OS 진입점이 보고를 맡길 가장 안쪽 처리기입니다(`CrashHandler::_s_pInstalled`). 없으면 nullptr — 할당 · 잠금 없이 읽는다. */
        static CrashHandler* findInstalled();
        /** @brief 행 보고의 플랫폼 몫입니다(Windows 는 스냅샷 미니덤프 — 모든 스레드의 스택, POSIX 는 없음). */
        static void writeHangDump( CrashHandler& handler );
    };
} // namespace sw
