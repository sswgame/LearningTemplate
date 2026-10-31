/**
 * @file RigSecondaryNodes.h
 * @brief 엔진 리그의 2 차 움직임과 보정 노드(`SpringChain`, `PoseDriver`)입니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"

namespace sw
{
    class RigNodeRegistry;

    /** @brief 2 차 움직임과 보정 노드(`SpringChain`, `PoseDriver`)의 등록입니다(`RigNodeLibrary::registerEngineNodes` 가 부른다). */
    struct RigSecondaryNodes
    {
        /** @brief 이 종류의 노드를 @p registry 에 등록합니다. */
        static void registerNodes( RigNodeRegistry& registry );
    };
} // namespace sw
