/**
 * @file WebCrashHandler.cpp
 * @brief 웹(wasm32)의 크래시 OS 훅 — 걸 것이 없다.
 * @details 브라우저 wasm 에는 시그널 · 예외 필터가 없다: 메모리 범위 밖 접근 · `abort` 는 wasm 트랩이 되어 JS 예외로 실행을 끝내고, 브라우저 콘솔이 스택을 남긴다.
 *          덤프도 없다(살아 있는 wasm 메모리를 디스크에 쓸 길이 없다 — 행 보고는 `CrashHandler::reportHang` 의 컨텍스트 글만).
 */
#include "pch.h"

#include "Core/Diagnostics/CrashHandlerProcessHook.h"

#if defined( SW_PLATFORM_WEB )
namespace sw
{
    void CrashHandlerProcessHook::install()
    {
    }

    void CrashHandlerProcessHook::uninstall()
    {
    }

    void CrashHandlerProcessHook::writeHangDump( CrashHandler& )
    {
    }
} // namespace sw
#endif
