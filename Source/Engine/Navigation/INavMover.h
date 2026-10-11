/**
 * @file INavMover.h
 * @brief 길을 따라 움직이는 것의 공통 인터페이스입니다. 목적지 지정, 멈추기, 상태, 속도를 제공합니다.
 * @details 내비메시 에이전트(`NavMeshAgentComponent`, 3D 내비메시와 군중)와 격자 행위자(`GameFramework/Base/Actor/Navigation/NavGridMover`, 격자 A*)가
 *          같이 구현한다. AI(행동 트리의 이동 노드와 감독)는 이 인터페이스만 보고, 어느 쪽으로 걷는지 모른다. 언리얼의 `UPathFollowingComponent` 에 해당한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

namespace sw
{
    /** @brief 이동 요청의 상태입니다. */
    enum class NavMoveStatus : uint8
    {
        Idle = 0, ///< 목적지가 없다.
        Moving,   ///< 경로를 구하거나 따라가는 중이다.
        Arrived,  ///< 목적지(멈춤 거리 안)에 닿았다
        Failed,   ///< 목적지로 가는 길이 없다(내비메시나 격자 밖이거나 막혔다).
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
        /** @brief 현재 위치(발)를 반환합니다. */
        virtual float3 getMovePosition() const = 0;

    protected:
        INavMover()                              = default;
        INavMover( const INavMover& )            = default;
        INavMover& operator=( const INavMover& ) = default;
    };
} // namespace sw
