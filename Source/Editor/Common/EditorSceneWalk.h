/**
 * @file EditorSceneWalk.h
 * @brief 에디터가 씬 전체 순회 열쇠(`SceneWalkKey`)를 받는 공용 공급자입니다(계층 · 고르기 · 검사 · 저장 명령, 에디터 확장).
 * @details 게임 · 키트 · 런타임 타깃은 Editor 를 include 할 수 없어(`Config/Rules/Layers/TargetLayers.toml`) 컴포넌트 틱이 이 공급자에 닿지 못한다.
 */
#pragma once
#include "Engine/Foundation/SceneWalkKey.h"

namespace sw::editor
{
    /** @struct EditorSceneWalk @brief 에디터의 순회 열쇠를 냅니다. */
    struct EditorSceneWalk
    {
        static SceneWalkKey getKey() { return SceneWalkKey{}; }
    };
} // namespace sw::editor
