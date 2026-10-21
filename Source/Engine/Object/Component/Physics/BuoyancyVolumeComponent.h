/**
 * @file BuoyancyVolumeComponent.h
 * @brief 물 부피 — 오브젝트 자리의 상자 안에 든 움직이는 바디를 고정 스텝마다 띄우고 끌림 · 흐름을 겁니다(3D, `IPhysicsBuoyancyScene`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Physics/PhysicsComponent.h"
#include "Engine/Physics/IPhysicsBuoyancyScene.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class BuoyancyVolumeComponent
     * @brief 물 부피입니다(언리얼 Water Body 의 부력 · 유니티 수면 부력 스크립트의 자리). 수면은 상자의 윗면이고(오브젝트의 +Y 쪽), 상자와 겹친 움직이는
     *        바디마다 잠긴 부피만큼 위로 밀고 잠긴 몫에 끌림과 흐름(`_flowVelocity`, 오브젝트 로컬)을 겁니다.
     * @details 고정 스텝 직전(`prePhysicsStep`)에 상자와 겹치는 바디를 찾아(`overlapShape`, 트리거 제외) 바디마다 `applyBuoyancy` 를 부른다 — 잠긴 부피와 부력 중심은
     *          바디 셰이프에서 백엔드가 정확히 구한다(Jolt). 바디가 뜨는 높이는 `_buoyancy`(유체 밀도 ÷ 바디 밀도)가 정한다. 상자는 바디를 만들지 않는다 — 다른 바디가
     *          부딪히지 않는 순수한 질의 부피다. 2D 물리에는 없다(Box2D 에 잠긴 넓이 계산이 없다).
     */
    REFLECT( Category = "Physics", DisplayName = "Buoyancy Volume", Tooltip = "Water box: dynamic bodies inside float, feel drag and drift with the flow", Meta = "Icon=Physics" )
    class SW_API BuoyancyVolumeComponent : public PhysicsComponent
    {
    public:
        REFLECT_BODY();

        BuoyancyVolumeComponent();
        ~BuoyancyVolumeComponent() override = default;

        /** @brief 지난 고정 스텝에 수면 아래로 잠긴 바디 수입니다(진단 · 테스트 · 탐침). */
        uint32 getSubmergedBodyCount() const { return _submergedBodyCount; }
        /** @brief 지금 자세의 수면(점 · 위쪽 법선, 월드)과 상자 중심 · 회전입니다. 오브젝트가 없으면 false 입니다. */
        [[nodiscard]] bool computeSurface( float3& outSurfacePoint, float3& outSurfaceNormal, float3& outCenter, quaternion& outRotation ) const;

        void setHalfExtents( const float3& halfExtents ) { _halfExtents = halfExtents; }
        void setBuoyancy( float32 buoyancy ) { _buoyancy = buoyancy; }
        void setFlowVelocity( const float3& flowVelocity ) { _flowVelocity = flowVelocity; }

    protected:
        void prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount ) override;

    private:
        PROPERTY( Category = "Water", DisplayName = "Half Extents", Min = 0.01, Tooltip = "Half size of the water box; the surface is its top face", Units = m )
        float3 _halfExtents;
        PROPERTY( Category = "Water", Min = 0.0, Tooltip = "Fluid density over body density: 1 neither floats nor sinks, larger floats higher" )
        float32 _buoyancy;
        PROPERTY( Category = "Water", DisplayName = "Linear Drag", Min = 0.0, Tooltip = "Drag on the submerged part (1/s)" )
        float32 _linearDrag;
        PROPERTY( Category = "Water", DisplayName = "Angular Drag", Min = 0.0, Tooltip = "Rotational drag on the submerged part (1/s)" )
        float32 _angularDrag;
        PROPERTY( Category = "Water", DisplayName = "Flow Velocity", Tooltip = "Current in the object's local space (m/s); floating bodies drift with it" )
        float3 _flowVelocity;
        PROPERTY( Category = "Water", DisplayName = "Layer Mask", Tooltip = "Physics layers (bit per layer index) that float" )
        uint32 _layerMask;

        vector<PhysicsBodyHandle> _listOverlapBody;    ///< 고정 스텝마다 다시 채우는 겹친 바디(할당을 되풀이하지 않는다)
        uint32                    _submergedBodyCount; ///< 지난 고정 스텝의 잠긴 바디 수
    };
} // namespace sw
