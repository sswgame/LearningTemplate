/**
 * @file RigNodeLibrary.h
 * @brief 엔진이 기본으로 등록하는 리그 노드 종류들입니다. 종류마다 .cpp 하나가 자기 노드를 등록합니다.
 */
#pragma once
#include "Core/Common/Macros.h"

namespace sw
{
    class RigNodeRegistry;

    /** @brief 엔진 노드 등록 함수 묶음입니다(`RigNodeRegistry::getInstance` 가 처음 한 번 부릅니다). */
    struct RigNodeLibrary
    {
        /** @brief 아래 모두를 등록합니다. 늘 true 입니다(정적 초기화에 쓰려고 값을 돌려줍니다). */
        static bool registerEngineNodes( RigNodeRegistry& registry );
        /** @brief IK — `TwoBoneIK` · `FabrikChain` · `CcdChain` · `Aim` · `FootPlacement`. */
        static void registerIKNodes( RigNodeRegistry& registry );
        /** @brief 제약 — `CopyTransform` · `Position` · `Rotation` · `ParentSwitch` · `Distance` · `LimitRotation` · `TwistDistribution`. */
        static void registerConstraintNodes( RigNodeRegistry& registry );
        /** @brief 2 차 움직임 · 보정 — `SpringChain` · `PoseDriver`. */
        static void registerSecondaryNodes( RigNodeRegistry& registry );
    };
} // namespace sw
