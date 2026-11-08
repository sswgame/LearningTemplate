/**
 * @file RigBuiltinNodes.h
 * @brief 엔진 리그의 IK 노드(`TwoBoneIK`, `FABRIKChain`, `CCDChain`, `Aim`, `FootPlacement`, `StrideWarping`, `FullBodyIK`)입니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"

namespace sw
{
    class RigNodeRegistry;

    /** @brief IK 노드(`TwoBoneIK`, `FABRIKChain`, `CCDChain`, `Aim`, `FootPlacement`, `StrideWarping`, `FullBodyIK`)의 등록입니다(`RigNodeLibrary::registerEngineNodes` 가 부른다). */
    struct RigIKNodes
    {
        /** @brief 이 종류의 노드를 @p registry 에 등록합니다. */
        static void registerNodes( RigNodeRegistry& registry );
    };
} // namespace sw

/*
 * 엔진 리그의 제약 노드(`CopyTransform`, `Position`, `Rotation`, `ParentSwitch`, `Distance`, `LimitRotation`, `TwistDistribution`)입니다.
 */
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

/*
 * 엔진 리그의 2 차 움직임과 보정 노드(`SpringChain`, `PoseDriver`)입니다.
 */
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
