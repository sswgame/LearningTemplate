/**
 * @file RigNodeLibrary.h
 * @brief 엔진이 기본으로 등록하는 리그 노드 종류들입니다. 종류마다 헤더 · cpp 한 쌍(`RigIKNodes` · `RigConstraintNodes` · `RigSecondaryNodes`)이 자기 노드를 등록합니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"

namespace sw
{
    class RigNodeRegistry;

    /** @brief 엔진 노드 등록 함수 모음입니다(`RigNodeRegistry::getInstance` 가 처음 한 번 호출합니다). */
    struct RigNodeLibrary
    {
        /** @brief 엔진 노드 종류를 모두 등록합니다. 늘 true 입니다(정적 초기화에 쓰려고 값을 돌려줍니다). */
        [[nodiscard]] static bool registerEngineNodes( RigNodeRegistry& registry );
    };
} // namespace sw
