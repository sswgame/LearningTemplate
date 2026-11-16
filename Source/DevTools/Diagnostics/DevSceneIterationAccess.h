/**
 * @file DevSceneIterationAccess.h
 * @brief 개발 도구 층이 씬 전체 순회 열쇠(`SceneIterationKey`)를 받는 공용 공급자입니다(감시 값 · 서버 자세 겹침 · 상태 해시 단계 · HUD).
 * @details 게임 · 키트 · 런타임 타깃은 DevTools 를 include 할 수 없어(`Config/Rules/Layers/TargetLayers.toml`) 컴포넌트 틱이 이 공급자에 닿지 못한다.
 */
#pragma once
#include "Engine/Foundation/SceneIterationKey.h"

namespace sw
{
    /** @struct DevSceneIterationAccess @brief 개발 도구의 순회 열쇠를 냅니다. */
    struct DevSceneIterationAccess
    {
        static SceneIterationKey getKey() { return SceneIterationKey{}; }
    };
} // namespace sw
