/**
 * @file CameraCollisionProbe.h
 * @brief 카메라 암(피벗에서 카메라까지)을 장면에 쓸어 보는 질의의 인터페이스를 정의합니다.
 * @details 스프링 암과 가림 피하기는 이 인터페이스만 안다.
 *          물리 백엔드가 바뀌어도 카메라 쪽은 이 인터페이스 하나만 본다.
 *          구현은 상자 목록(테스트와 게임이 아는 막는 상자)과 씬의 물리(3D Jolt · 2D Box2D, 2D 콜라이더 포함), 두 가지이다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/AABB.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class ICameraCollisionProbe
     * @brief 구 하나를 @p from 에서 @p to 로 쓸어 처음 닿는 거리를 구합니다.
     */
    class ICameraCollisionProbe
    {
    public:
        ICameraCollisionProbe()                                          = default;
        ICameraCollisionProbe( const ICameraCollisionProbe& )            = default;
        ICameraCollisionProbe& operator=( const ICameraCollisionProbe& ) = default;
        virtual ~ICameraCollisionProbe()                                 = default;

        /**
         * @brief 반지름 @p radius 의 구를 @p from 에서 @p to 로 쓸 때 처음 닿는 곳까지의 거리를 구합니다.
         * @return 닿으면 true 이고 @p outDistance 는 @p from 에서 닿은 중심까지의 거리이다. 안 닿으면 false 이다.
         */
        virtual bool sweepSphere( const float3& from, const float3& to, float32 radius, float32& outDistance ) const = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class CameraBoxCollisionProbe
     * @brief 막는 상자 목록에 대한 질의입니다. 게임이 이미 아는 벽과 엄폐물 상자(슈터의 차단 상자), 그리고 테스트가 씁니다.
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
     * @class SceneCameraProbe
     * @brief 씬의 3D 물리(구 쓸기)와 2D 물리(XY 평면의 원 쓸기, 2D 콜라이더 포함) 중 가까운 쪽을 고르는 질의입니다.
     * @details 레이어 `Default` 와 `Static` 만 보고, 대상의 바디는 모두 건너뛴다(피벗이 대상 자신의 바디 안에 있다).
     *          카메라 디렉터의 기본 암 충돌 질의이다. 강체 벽과 바닥(RigidBody)이 있는 씬도, 2D 콜라이더만 있는 씬도 모두 막는다.
     */
    class SW_GF_API SceneCameraProbe final : public ICameraCollisionProbe
    {
    public:
        SceneCameraProbe( const GameObjectManager& manager, uint64 ignoredObjectID );

        bool sweepSphere( const float3& from, const float3& to, float32 radius, float32& outDistance ) const override;

    private:
        const GameObjectManager& _manager;
        uint64                   _ignoredObjectID;
    };
} // namespace sw
