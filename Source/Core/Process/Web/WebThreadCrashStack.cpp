/**
 * @file WebThreadCrashStack.cpp
 * @brief 웹(wasm32)은 시그널이 없어 대체 시그널 스택을 깔 것이 없다 — 스택 오버플로는 wasm 트랩(JS 예외)이다.
 */
#include "pch.h"

#include "Core/Process/ThreadCrashStack.h"

#if defined( SW_PLATFORM_WEB )
namespace sw
{
    void ThreadCrashStack::initializeCurrentThread()
    {
    }

    void ThreadCrashStack::releaseCurrentThread()
    {
    }
} // namespace sw
#endif
