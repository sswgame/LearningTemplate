/**
 * @file SceneIterationTestAccess.h
 * @brief 테스트가 씬 전체 순회 열쇠(`SceneIterationKey`)를 받는 곳입니다. 테스트(`/Tests/`)만 include 한다.
 * @details 배포 구성은 테스트를 짓지 않는다 — 배포 구성이 짓는 파일이 이 헤더를 include 하면 아래 `#error` 로 멈춘다(제품 코드가 테스트 열쇠를 꺼내지 못하게).
 */
#pragma once
#include "Engine/Foundation/SceneIterationKey.h"

#if defined( SW_SHIPPING )
    #error "SceneIterationTestAccess.h is test-only - product code gets SceneIterationKey from its framework (watch, dev command, HUD, save) instead"
#endif

namespace sw
{
    /** @struct SceneIterationTestAccess @brief 테스트용 순회 열쇠를 냅니다. */
    struct SceneIterationTestAccess
    {
        static SceneIterationKey getKey() { return SceneIterationKey{}; }
    };
} // namespace sw
