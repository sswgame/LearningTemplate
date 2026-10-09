/**
 * @file CameraCollisionProbe.h
 * @brief 카메라 암(피벗 → 카메라)을 장면에 쓸어 보는 질의의 창구 — 스프링 암 · 가림 피하기가 이것만 압니다.
 * @details 물리 백엔드가 바뀌어도(지금 `PhysicsWorld` 의 AABB 바디, 다음은 Jolt 질의) 카메라 쪽은 이 인터페이스 하나만 봅니다. 구현은 둘 —
 *          상자 목록(시험 · 게임이 아는 막는 상자)과 매니저의 `PhysicsWorld` 바디입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Physics/Collision/AABB.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;
    class PhysicsWorld;

    /**
     * @class ICameraCollisionProbe
     * @brief 구 하나를 @p from 에서 @p to 로 쓸어 처음 닿는 거리를 답합니다.
     */
    class SW_GF_API ICameraCollisionProbe
    {
    public:
        ICameraCollisionProbe()                                          = default;
        ICameraCollisionProbe( const ICameraCollisionProbe& )            = default;
        ICameraCollisionProbe& operator=( const ICameraCollisionProbe& ) = default;
        virtual ~ICameraCollisionProbe()                                 = default;

        /**
         * @brief 반지름 @p radius 의 구를 @p from 에서 @p to 로 쓸 때 처음 닿는 곳까지의 거리입니다.
         * @return 닿으면 true 이고 @p outDistance 는 @p from 에서 닿은 중심까지의 거리입니다. 안 닿으면 false 입니다.
         */
        virtual bool sweepSphere( const float3& from, const float3& to, float32 radius, float32& outDistance ) const = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class CameraBoxCollisionProbe
     * @brief 막는 상자 목록에 대한 질의입니다. 게임이 이미 아는 벽 · 엄폐물 상자(슈터의 차단 상자)와 시험이 씁니다.
     */
    class SW_GF_API CameraBoxCollisionProbe final : public ICameraCollisionProbe
    {
    public:
        CameraBoxCollisionProbe();

        void addBox( const AABB& box ) { _listBox.push_back( box ); }
        void clear() { _listBox.clear(); }
        bool isEmpty() const { return _listBox.empty(); }

        bool sweepSphere( const float3& from, const float3& to, float32 radius, float32& outDistance ) const override;

    private:
        vector<AABB> _listBox;
    };
} // namespace sw

namespace sw
{
    /**
     * @class PhysicsWorldCameraProbe
     * @brief 씬 매니저의 `PhysicsWorld` 바디(콜라이더)에 대한 질의입니다. 대상 자신의 바디는 건너뜁니다(피벗이 그 안에 있다).
     */
    class SW_GF_API PhysicsWorldCameraProbe final : public ICameraCollisionProbe
    {
    public:
        PhysicsWorldCameraProbe( const PhysicsWorld& world, uint8 layer, uint64 ignoredObjectId );

        bool sweepSphere( const float3& from, const float3& to, float32 radius, float32& outDistance ) const override;

    private:
        const PhysicsWorld* _pWorld;
        uint64              _ignoredObjectId;
        uint8               _layer;
    };
} // namespace sw

namespace sw
{
    /**
     * @class SceneCameraProbe
     * @brief 씬의 강체 물리(Jolt 3D 씬 — 레이어 `Default` · `Static`, 대상의 바디는 모두 건너뜀)와 겹침 월드(`PhysicsWorld` 콜라이더) 중 가까운 것입니다.
     *        카메라 디렉터의 기본 암 충돌 질의입니다 — 강체 벽 · 바닥(RigidBody)과 2D 콜라이더만 있는 씬 모두 막습니다.
     */
    class SW_GF_API SceneCameraProbe final : public ICameraCollisionProbe
    {
    public:
        SceneCameraProbe( const GameObjectManager& manager, uint64 ignoredObjectId );

        bool sweepSphere( const float3& from, const float3& to, float32 radius, float32& outDistance ) const override;

    private:
        const GameObjectManager& _manager;
        uint64                   _ignoredObjectId;
    };
} // namespace sw
