/**
 * @file NavGridObstacle.h
 * @brief 씬의 정적 바디 모양으로 내비 격자의 칸을 막습니다 — 걷기를 막는 것과 길 찾기를 막는 것이 같은 바디다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class NavGrid;
    class RigidBodyComponent;
    struct WorldOrigin;

    /** @struct NavGridObstacle @brief 파일 머리말 참고. */
    struct SW_GF_API NavGridObstacle
    {
        /**
         * @brief 바디 @p body 의 셰이프 경계(절대 자리의 축 상자)에서 @p clearance 안에 가운데가 드는 칸을 막고 그 수를 반환합니다. 격자 밖의 칸은 세지 않습니다.
         * @details 셰이프는 내비메시 베이크와 같은 길(`NavMeshGeometry::addPhysicsShape`)로 옮긴다. 돌아간 바디는 조금 크게 막힌다.
         */
        static uint32 blockBody( NavGrid& inoutGrid, const WorldOrigin& worldOrigin, const RigidBodyComponent& body, float32 clearance );
    };
} // namespace sw
