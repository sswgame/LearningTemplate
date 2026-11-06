/**
 * @file INavMeshQuery.h
 * @brief 내비메시의 질의 쪽 인터페이스입니다. 가장 가까운 점, 경로, 시선을 읽기만 합니다.
 * @details 모두 const 이고 여러 스레드가 같이 호출해도 된다(교체와 겹치면 안 된다 — `INavMesh.h` 머리말). 유니티 `NavMesh.CalculatePath` · `NavMesh.Raycast`,
 *          물리의 `IPhysicsQueryScene` 과 같은 자리다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Navigation/NavigationTypes.h"

namespace sw
{
    /** @brief 내비메시를 읽기만 하는 질의입니다. */
    class INavMeshQuery
    {
    public:
        virtual ~INavMeshQuery() = default;

        /** @brief @p position 에서 반 크기 @p searchExtent 상자 안(가로 XZ 기준)의 가장 가까운 내비메시 위 점입니다. 없으면 false 입니다 — 폴리곤의 경계 상자만 걸치고 점은 상자 밖이면 false 다. */
        [[nodiscard]] virtual bool findNearestPoint( const float3& position, const float3& searchExtent, const NavQueryFilter& filter, NavLocation& outLocation ) const = 0;
        /**
         * @brief @p start 에서 @p end 까지의 경로를 구해 다듬습니다(줄 당기기, 모퉁이만 남깁니다).
         * @details 두 점은 반 크기 @p searchExtent 안의 가장 가까운 폴리곤으로 붙는다. 끝에 닿지 못하면 가장 가까운 곳까지 가는 `Partial` 경로를 반환한다.
         */
        virtual NavPathStatus findPath( const float3& start, const float3& end, const float3& searchExtent, const NavQueryFilter& filter, NavPath& outPath ) const = 0;
        /** @brief @p start 에서 @p end 쪽으로 걸을 면을 따라 곧게 갑니다(시선). 경계에 막히면 `_bHit` 입니다. 시작이 내비메시 밖이면 false 입니다. */
        [[nodiscard]] virtual bool raycast( const float3& start, const float3& end, const float3& searchExtent, const NavQueryFilter& filter, NavRaycastHit& outHit ) const = 0;
    };
} // namespace sw
