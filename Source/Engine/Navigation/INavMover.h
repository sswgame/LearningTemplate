/**
 * @file INavMover.h
 * @brief 길을 따라 움직이는 것의 공통 창구 — 목적지 걸기 · 멈추기 · 상태 · 속도입니다.
 * @details 내비메시 에이전트(`NavMeshAgentComponent`, 3D 내비메시 + 군중)와 격자 행위자(`GameFramework/Navigation/NavGridMover`, 격자 A*)가
 *          같이 구현합니다. AI(행동 트리의 이동 노드 · 감독)는 이것만 보고 어느 쪽으로 걷는지 모릅니다 — 언리얼 `UPathFollowingComponent` 의 자리.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

namespace sw
{
    /** @brief 이동 요청의 상태입니다. */
    enum class NavMoveStatus : uint8
    {
        Idle = 0, ///< 목적지 없음
        Moving,   ///< 경로를 구하거나 따라가는 중
        Arrived,  ///< 목적지(멈춤 거리 안)에 닿았다
        Failed,   ///< 목적지로 가는 길이 없다(내비메시 · 격자 밖, 막힘)
    };
} // namespace sw

namespace sw
{
    /** @class INavMover @brief 파일 머리말 참고. */
    class INavMover
    {
    public:
        virtual ~INavMover() = default;

        /** @brief @p destination 으로 갑니다. 받아들이지 못하면(길 없음) false 이고 상태가 `Failed` 입니다. 요청이 늦게 풀리는 구현은 true 를 돌려주고 나중에 실패를 알립니다. */
        [[nodiscard]] virtual bool moveTo( const float3& destination ) = 0;
        /** @brief 목적지를 지우고 멈춥니다. */
        virtual void stopMoving() = 0;
        /** @brief 지금 상태입니다. */
        virtual NavMoveStatus getMoveStatus() const = 0;
        /** @brief 지금 속도(월드, m/s)입니다. */
        virtual float3 getMoveVelocity() const = 0;
        /** @brief 지금 자리(발)입니다. */
        virtual float3 getMovePosition() const = 0;

    protected:
        INavMover()                              = default;
        INavMover( const INavMover& )            = default;
        INavMover& operator=( const INavMover& ) = default;
    };
} // namespace sw
