/**
 * @file BuoyancyComponent.h
 * @brief 같은 오브젝트의 강체(`RigidBodyComponent`)를 물(`WaterBodyComponent`) 위에 띄웁니다. 언리얼 Water 플러그인의 BuoyancyComponent(폰툰)에 해당합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Physics/PhysicsComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 부력을 받는 구 하나입니다. 바디 기준 위치와 반지름으로 정합니다. */
    REFLECT()
    struct SW_API BuoyancyPontoon
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Center relative to the body origin", Units = m )
        float3 _localPosition{};
        PROPERTY( Min = 0.01, Tooltip = "Sphere radius", Units = m )
        float32 _radius{ 0.5f };
    };
} // namespace sw

namespace sw
{
    /** @brief 폰툰 하나의 부력 계산입니다. 컴포넌트와 테스트가 같은 식을 씁니다. */
    struct SW_API BuoyancyUtil
    {
        /**
         * @brief 반지름 @p radius 인 구가 잠긴 부피의 비율(0..1)입니다. 수면이 구 중심보다 @p surfaceAboveCenter 만큼 위에 있습니다(아래면 음수).
         * @details 잠긴 높이 h = clamp( surfaceAboveCenter + r, 0, 2r ) 의 구 캡 부피 πh²(3r − h) / 3 을 구 부피로 나눈 h²(3r − h) / (4r³) 이다.
         */
        static float32 computeSubmergedFraction( float32 surfaceAboveCenter, float32 radius );
        /** @brief 잠긴 비율 @p submergedFraction 인 구가 받는 부력의 크기(N)입니다. 물 밀도 × 중력 크기 × 잠긴 부피입니다. */
        static float32 computeBuoyantForce( float32 submergedFraction, float32 radius, float32 waterDensity, float32 gravityMagnitude );
    };
} // namespace sw

namespace sw
{
    /**
     * @class BuoyancyComponent
     * @brief 폰툰(구)마다 그 자리의 수면 높이(`WaterBodyComponent::computeSurfaceHeight`, 파도 포함)로 부력과 물 저항을 계산해 같은 오브젝트의 동적 강체에 줍니다.
     * @details 물리 고정 스텝마다(`prePhysicsStep`) 폰툰 자리를 바디의 지금 자세로 옮기고, 그 (x, z) 를 덮는 물을 찾아(`findWaterAt`) 잠긴 비율을 구한다.
     *          부력은 중력 반대 방향, 저항은 폰툰 자리의 속도 반대 방향이며 둘 다 폰툰 자리에 충격량으로 준다(기울면 바로 서는 회전이 생긴다).
     *          파도 시간은 물 컴포넌트의 것(`getWaveTime`)이라 화면의 파도와 부력이 같은 수면을 본다. 중력은 그 씬의 물리 설정 표에서 온다.
     *          강체가 없거나 동적이 아니면 아무것도 하지 않는다. 단계는 `Joint` — 바디가 만들어진 뒤에 돈다.
     */
    REFLECT( Category = "Environment", DisplayName = "Buoyancy", Tooltip = "Floats the rigid body on the same object on water bodies" )
    class SW_API BuoyancyComponent : public PhysicsComponent
    {
    public:
        REFLECT_BODY();

        BuoyancyComponent();
        ~BuoyancyComponent() override = default;

        const vector<BuoyancyPontoon>& getPontoons() const { return _listPontoon; }
        void                           setPontoons( const vector<BuoyancyPontoon>& listPontoon ) { _listPontoon = listPontoon; }
        float32                        getWaterDensity() const { return _waterDensity; }
        void                           setWaterDensity( float32 density ) { _waterDensity = density; }
        float32                        getLinearDrag() const { return _linearDrag; }
        void                           setLinearDrag( float32 drag ) { _linearDrag = drag; }
        /** @brief 마지막 스텝에서 물에 조금이라도 잠긴 폰툰 수입니다. */
        uint32 getSubmergedPontoonCount() const { return _submergedPontoonCount; }
        /** @brief 마지막 스텝에서 폰툰 하나라도 물에 잠겼으면 true 입니다. */
        bool isInWater() const { return _submergedPontoonCount > 0; }

    protected:
        void prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount ) override;
        void releasePhysics( ScenePhysics& physics ) override;

    private:
        PROPERTY( Category = "Buoyancy", DisplayName = "Pontoons", Tooltip = "Spheres that sample the water surface (body local)" )
        vector<BuoyancyPontoon> _listPontoon;
        PROPERTY( Category = "Buoyancy", DisplayName = "Water Density", Min = 0.0, Tooltip = "Fresh water is 1000", Meta = "Units=kg/m3" )
        float32 _waterDensity;
        PROPERTY( Category = "Buoyancy", DisplayName = "Linear Drag", Min = 0.0, Tooltip = "Drag force per m/s of a fully submerged pontoon", Meta = "Units=N*s/m" )
        float32 _linearDrag;

        uint32 _submergedPontoonCount;
    };
} // namespace sw
