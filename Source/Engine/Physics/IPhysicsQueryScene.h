/**
 * @file IPhysicsQueryScene.h
 * @brief 물리 씬의 질의 쪽 인터페이스입니다(`IPhysicsQueryScene3D`, `IPhysicsQueryScene2D`). 레이 캐스트, 셰이프 캐스트(가장 가까운 것 · 모두), 겹침을 읽기만 합니다.
 * @details 맞힘 판정, 소리 가림, 발 IK, 탈것 땅 광선처럼 씬을 바꾸지 않는 코드는 이 인터페이스만 받는다.
 *          유니티의 `Physics.Raycast` · `Physics2D.Raycast`, 언리얼의 `UWorld::LineTraceSingleByChannel` 에 해당한다.
 *          핸들 · 스레드 · 단위 규칙은 `IPhysicsScene.h` 머리말과 같다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Physics/Collision/PhysicsQuery.h"
#include "Engine/Physics/PhysicsDesc.h"
#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    /** @brief 씬을 읽기만 하는 질의입니다. */
    template <typename TDimension>
    class IPhysicsQueryScene
    {
    public:
        using Vector    = typename TDimension::Vector;
        using Rotation  = typename TDimension::Rotation;
        using ShapeDesc = typename TDimension::ShapeDesc;
        using CastHit   = PhysicsCastHit<TDimension>;

        virtual ~IPhysicsQueryScene() = default;

        /** @brief @p origin 에서 @p direction(정규화)으로 @p maxDistance 까지 처음 닿는 바디입니다. */
        virtual bool raycast( const Vector& origin, const Vector& direction, float32 maxDistance, const PhysicsQueryFilter& filter, CastHit& outHit ) const = 0;
        /** @brief 셰이프를 @p position, @p rotation 에서 @p direction 으로 @p maxDistance 까지 쓸어 처음 닿는 바디를 찾습니다. */
        virtual bool shapeCast( const ShapeDesc& shape, const Vector& position, const Rotation& rotation, const Vector& direction, float32 maxDistance,
                                const PhysicsQueryFilter& filter, CastHit& outHit ) const = 0;
        /**
         * @brief `raycast` 와 같은 광선이 닿는 바디를 **모두** @p outListHit 끝에 붙이고 그 수를 돌려줍니다.
         * @details 바디마다 가장 가까운 닿음 하나이고(컴파운드의 서브 셰이프가 여럿 닿아도 하나), 가까운 순서다(`PhysicsQueryUtil::keepNearestHitPerBody`).
         *          관통 사격, 시야를 가린 것 모두 찾기에 쓴다. 언리얼 `LineTraceMultiByChannel`(막는 닿음에서 멈추지 않는 형태), 유니티 `RaycastAll` 에 해당한다.
         *          재질 이름은 `raycast` 처럼 채운다.
         */
        virtual uint32 raycastAll( const Vector& origin, const Vector& direction, float32 maxDistance, const PhysicsQueryFilter& filter,
                                   vector<CastHit>& outListHit ) const = 0;
        /**
         * @brief `shapeCast` 와 같은 스윕이 닿는 바디를 **모두** @p outListHit 끝에 붙이고 그 수를 돌려줍니다(바디마다 가장 가까운 하나, 가까운 순서).
         * @details 출발점에서 이미 겹친 바디는 `_fraction = 0` 으로 들어온다(언리얼 `SweepMulti` 의 초기 겹침과 같다). 재질 이름은 비어 있다.
         */
        virtual uint32 shapeCastAll( const ShapeDesc& shape, const Vector& position, const Rotation& rotation, const Vector& direction, float32 maxDistance,
                                     const PhysicsQueryFilter& filter, vector<CastHit>& outListHit ) const = 0;
        /** @brief 셰이프와 겹치는 바디들을 @p outListBody 에 붙이고 그 수를 돌려줍니다(같은 바디는 한 번). */
        virtual uint32 overlapShape( const ShapeDesc& shape, const Vector& position, const Rotation& rotation, const PhysicsQueryFilter& filter,
                                     vector<PhysicsBodyHandle>& outListBody ) const = 0;
    };

    using IPhysicsQueryScene3D = IPhysicsQueryScene<PhysicsDimension3D>;
    using IPhysicsQueryScene2D = IPhysicsQueryScene<PhysicsDimension2D>;
} // namespace sw
