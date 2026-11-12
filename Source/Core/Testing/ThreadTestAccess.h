/**
 * @file ThreadTestAccess.h
 * @brief 테스트와 테스트 실행기가 스레드를 만드는 열쇠(`ThreadCreateKey`)를 받는 곳입니다.
 * @details 단위 테스트는 경합을 일부러 만들려고 스레드를 띄운다. 제품 코드의 스레드 주인 목록은 `Core/Concurrency/thread.h` 의 `ThreadCreateKey` 친구들이다 —
 *          이 헤더는 테스트(`/Tests/`)와 `Core/Testing` 만 include 한다. 배포 구성은 둘 다 짓지 않는다(`Source/Core/CMakeLists.txt`) — 배포 구성이 짓는 파일이
 *          include 하면 아래 `#error` 로 멈춘다. `Core/Testing` 은 Core 맨 위 티어라 다른 Core 폴더는 층 빌드가 막는다.
 */
#pragma once
#include "Core/Concurrency/thread.h"

#if defined( SW_SHIPPING )
    #error "ThreadTestAccess.h is test-only - product threads take ThreadCreateKey from their owner class (friend list in Core/Concurrency/thread.h)"
#endif

namespace sw
{
    /** @struct ThreadTestAccess @brief 테스트용 스레드 열쇠를 냅니다. */
    struct ThreadTestAccess
    {
        static ThreadCreateKey getKey() { return ThreadCreateKey{}; }
    };
} // namespace sw
