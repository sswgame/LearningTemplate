/**
 * @file IPhysicsQueryScene.h
 * @brief 물리 씬의 질의 쪽 인터페이스입니다(`IPhysicsQueryScene3D`, `IPhysicsQueryScene2D`). 레이 캐스트, 셰이프 캐스트, 겹침을 읽기만 합니다.
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
        /** @brief 셰이프와 겹치는 바디들을 @p outListBody 에 붙이고 그 수를 돌려줍니다(같은 바디는 한 번). */
        virtual uint32 overlapShape( const ShapeDesc& shape, const Vector& position, const Rotation& rotation, const PhysicsQueryFilter& filter,
                                     vector<PhysicsBodyHandle>& outListBody ) const = 0;
    };

    using IPhysicsQueryScene3D = IPhysicsQueryScene<PhysicsDimension3D>;
    using IPhysicsQueryScene2D = IPhysicsQueryScene<PhysicsDimension2D>;
} // namespace sw
