/**
 * @file BehaviorTreeSchema.h
 * @brief 행동 트리 파일(`*.bt.xml`)의 합치기 규칙입니다. 노드 `id` 가 열쇠라 `_base` 트리에서 가지만 바꿉니다.
 * @details 실행기는 GameFramework 에 있지만 규칙은 여기 둔다 — 팩 쿠킹은 게임 모듈을 올리기 전에 돌아 GameFramework 의 정적 등록을 볼 수 없다(`DataCatalogInheritance` 의 까닭과 같다).
 */
#pragma once
#include "Core/Foundation/Macros.h"

#include "Engine/Serialization/Inherit/DataInheritance.h"

namespace sw
{
    /** @struct BehaviorTreeSchema @brief 행동 트리의 `DataInheritanceSchema` 를 만듭니다. */
    struct SW_API BehaviorTreeSchema
    {
        [[nodiscard]] static DataInheritanceSchema make();
    };
} // namespace sw
