/**
 * @file RigNodeLibrary.h
 * @brief 엔진이 기본으로 등록하는 리그 노드 종류들입니다. 종류마다 .cpp 하나가 자기 노드를 등록합니다.
 */
#pragma once
#include "Core/Common/Macros.h"

namespace sw
{
    class RigNodeRegistry;

    /** @brief 엔진 노드 등록 함수 모음입니다(`RigNodeRegistry::getInstance` 가 처음 한 번 호출합니다). */
    struct RigNodeLibrary
    {
        /** @brief 아래 모두를 등록합니다. 늘 true 입니다(정적 초기화에 쓰려고 값을 돌려줍니다). */
        static bool registerEngineNodes( RigNodeRegistry& registry );
        /** @brief IK 노드(`TwoBoneIK`, `FabrikChain`, `CcdChain`, `Aim`, `FootPlacement`, `StrideWarping`, `FullBodyIK`)를 등록합니다. */
        static void registerIKNodes( RigNodeRegistry& registry );
        /** @brief 제약 노드(`CopyTransform`, `Position`, `Rotation`, `ParentSwitch`, `Distance`, `LimitRotation`, `TwistDistribution`)를 등록합니다. */
        static void registerConstraintNodes( RigNodeRegistry& registry );
        /** @brief 2 차 움직임과 보정 노드(`SpringChain`, `PoseDriver`)를 등록합니다. */
        static void registerSecondaryNodes( RigNodeRegistry& registry );
    };
} // namespace sw
