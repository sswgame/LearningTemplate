#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Object/GameObject/Tick/FixedStepContext.h"
#include "Engine/Physics/Collision/PhysicsQuery.h"
#include "Engine/Physics/Component/PhysicsComponent.h"
#include "Engine/Physics/IPhysicsBuoyancyScene.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class BuoyancyVolumeComponent
     * @brief 물 부피입니다(언리얼 Water Body 의 부력 · 유니티 수면 부력 스크립트의 자리). 수면은 상자의 윗면이고(오브젝트의 +Y 쪽), 상자와 겹친 움직이는
     *        바디마다 잠긴 부피만큼 위로 밀고 잠긴 몫에 끌림과 흐름(`_flowVelocity`, 오브젝트 로컬)을 겁니다.
     * @details 고정 스텝 직전(`onPrePhysicsStep`)에 상자와 겹치는 바디를 찾아(`overlapShape`, 트리거 제외) 바디마다 `applyBuoyancy` 를 부른다 — 잠긴 부피와 부력 중심은
     *          바디 셰이프에서 백엔드가 정확히 구한다(Jolt). 바디가 뜨는 높이는 `_buoyancy`(유체 밀도 ÷ 바디 밀도)가 정한다. 상자는 바디를 만들지 않는다 — 다른 바디가
     *          부딪히지 않는 순수한 질의 부피다. 2D 물리에는 없다(Box2D 에 잠긴 넓이 계산이 없다).
     */
    REFLECT( Category = "Physics", DisplayName = "Buoyancy Volume", Tooltip = "Water box: dynamic bodies inside float, feel drag and drift with the flow", Meta = "Icon=Physics" )
    class BuoyancyVolumeComponent : public PhysicsComponent
    {
    public:
        REFLECT_BODY();

        BuoyancyVolumeComponent();
        ~BuoyancyVolumeComponent() override = default;

        /** @brief 지난 고정 스텝에 수면 아래로 잠긴 바디 수입니다(진단 · 테스트 · 감시 값). */
        uint32 getSubmergedBodyCount() const { return _submergedBodyCount; }
        /** @brief 지금 자세의 수면(점 · 위쪽 법선, 월드)과 상자 중심 · 회전입니다. 오브젝트가 없으면 false 입니다. */
        [[nodiscard]] bool computeSurface( float3& outSurfacePoint, float3& outSurfaceNormal, float3& outCenter, quaternion& outRotation ) const;

        void setHalfExtents( const float3& halfExtents ) { _halfExtents = halfExtents; }
        void setBuoyancy( float32 buoyancy ) { _buoyancy = buoyancy; }
        void setFlowVelocity( const float3& flowVelocity ) { _flowVelocity = flowVelocity; }

    protected:
        /** @brief 쓰는 고정 스텝 그룹마다 메서드를 등록합니다(`registerFixedStep`). */
        void onRegister( GameObjectManager& manager ) override;
        void onPrePhysicsStep( const FixedStepContext& context );

    private:
        PROPERTY( Category = "Water", DisplayName = "Half Extents", Min = 0.01, Tooltip = "Half size of the water box; the surface is its top face", Units = m, Meta = "Shape=Box" )
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

        SW_TRANSIENT vector<PhysicsBodyHandle> _listOverlapBody;    ///< 고정 스텝마다 다시 채우는 겹친 바디(할당을 되풀이하지 않는다)
        SW_TRANSIENT uint32                    _submergedBodyCount; ///< 지난 고정 스텝의 잠긴 바디 수
    };

    BuoyancyVolumeComponent::BuoyancyVolumeComponent()
        : PhysicsComponent{ PhysicsComponentPhase::Joint } // 바디가 모두 선 뒤에 띄운다
        , _halfExtents{ 5.0f, 1.0f, 5.0f }
        , _buoyancy{ 1.5f }
        , _linearDrag{ 0.5f }
        , _angularDrag{ 0.05f }
        , _flowVelocity{ 0.0f, 0.0f, 0.0f }
        , _layerMask{ MathUtil::kMaxUInt32 }
        , _listOverlapBody{}
        , _submergedBodyCount{ 0 }
    {
    }

    bool BuoyancyVolumeComponent::computeSurface( float3& outSurfacePoint, float3& outSurfaceNormal, float3& outCenter, quaternion& outRotation ) const
    {
        float3 scale;
        if ( PhysicsComponent::readWorldPose( *this, outCenter, outRotation, scale ) == false )
            return false; // 배율 0 — 수면을 정할 수 없다
        const float4x4 world = getWorldMatrix();
        const float3   up    = float3::transformVector( float3{ 0.0f, 1.0f, 0.0f }, world ).normalize();
        if ( up.getLength() <= 0.0f )
            return false; // 배율 0 — 수면을 정할 수 없다
        outSurfaceNormal = up;
        outSurfacePoint  = outCenter + up * _halfExtents._y;
        return true;
    }

    void BuoyancyVolumeComponent::onRegister( GameObjectManager& manager )
    {
        PhysicsComponent::onRegister( manager );
        registerFixedStep<&BuoyancyVolumeComponent::onPrePhysicsStep>( manager, FixedStepGroup::PreStep );
    }

    void BuoyancyVolumeComponent::onPrePhysicsStep( const FixedStepContext& context )
    {
        ScenePhysics& physics         = context.getPhysics();
        const float32 fixedDeltaTime  = context.getFixedDeltaTime().count();
        _submergedBodyCount           = 0;
        IPhysicsScene3D* const pScene = physics.getScene3D();
        if ( isSimulated() == false || pScene == nullptr )
            return;
        float3     surfacePoint;
        float3     surfaceNormal;
        float3     center;
        quaternion rotation;
        if ( computeSurface( surfacePoint, surfaceNormal, center, rotation ) == false )
            return;

        PhysicsShapeDesc3D box;
        box._halfExtents = _halfExtents;
        PhysicsQueryFilter filter;
        filter._layerMask        = _layerMask;
        filter._bIncludeTriggers = SW_FALSE;
        _listOverlapBody.clear();
        (void)pScene->overlapShape( box, center, rotation, filter, _listOverlapBody ); // 수는 목록이 말한다

        PhysicsBuoyancyDesc desc;
        desc._surfacePoint  = surfacePoint;
        desc._surfaceNormal = surfaceNormal;
        desc._buoyancy      = _buoyancy;
        desc._linearDrag    = _linearDrag;
        desc._angularDrag   = _angularDrag;
        desc._fluidVelocity = float3::transformVector( _flowVelocity, getWorldMatrix() );
        for ( const PhysicsBodyHandle body : _listOverlapBody )
        {
            if ( pScene->applyBuoyancy( body, desc, fixedDeltaTime ) )
                ++_submergedBodyCount;
        }
    }
} // namespace sw

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( buoyancyVolumeSubmergedBodies, "BuoyancyVolume.SubmergedBodies", "Bodies the first water box floated in the last fixed step", BuoyancyVolumeComponent,
                                &BuoyancyVolumeComponent::getSubmergedBodyCount );
} // namespace sw

#include "BuoyancyVolumeComponent.cpp.gen.inl"
