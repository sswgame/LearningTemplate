/**
 * @file NavGridMover.h
 * @brief 격자 행위자(`NavAgent` + `NavGrid` + `GridPathfinder`)를 공통 이동 창구(`INavMover`)로 보입니다 — 내비메시 에이전트와 같은 자리에 꽂힌다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Navigation/INavMover.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GridPathfinder;
    class NavAgent;
    class NavGrid;

    /**
     * @class NavGridMover
     * @brief AI(행동 트리의 이동 · 감독)가 격자 게임과 내비메시 게임에서 같은 코드로 걷게 하는 묶음입니다. 격자 · 길찾기 · 행위자는 빌려 씁니다(이것보다 오래 산다).
     * @details 상태는 행위자의 것을 옮깁니다 — 경로 · 흐름장을 따르는 중이면 `Moving`, 도착이면 `Arrived`, 끼였거나 경로를 못 구했으면 `Failed`.
     */
    class SW_GF_API NavGridMover final : public INavMover
    {
    public:
        NavGridMover( NavAgent& agent, const NavGrid& grid, GridPathfinder& pathfinder );

        [[nodiscard]] bool moveTo( const float3& destination ) override;
        void               stopMoving() override;
        NavMoveStatus      getMoveStatus() const override;
        float3             getMoveVelocity() const override;
        float3             getMovePosition() const override;

    private:
        NavAgent&       _agent;
        const NavGrid&  _grid;
        GridPathfinder& _pathfinder;
        bool            _bFailed;
    };
} // namespace sw
