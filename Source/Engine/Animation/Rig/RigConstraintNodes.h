/**
 * @file RigConstraintNodes.h
 * @brief 엔진 리그의 제약 노드(`CopyTransform`, `Position`, `Rotation`, `ParentSwitch`, `Distance`, `LimitRotation`, `TwistDistribution`)입니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"

namespace sw
{
    class RigNodeRegistry;

    /** @brief 제약 노드(`CopyTransform`, `Position`, `Rotation`, `ParentSwitch`, `Distance`, `LimitRotation`, `TwistDistribution`)의 등록입니다(`RigNodeLibrary::registerEngineNodes` 가 부른다). */
    struct RigConstraintNodes
    {
        /** @brief 이 종류의 노드를 @p registry 에 등록합니다. */
        static void registerNodes( RigNodeRegistry& registry );
    };
} // namespace sw
