/**
 * @file SpotLightComponent.h
 * @brief 스포트라이트 하나입니다. 위치 · 방향 · 원뿔각 · 반경을 선언으로 다룹니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Object/Component/3D/LightComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class SpotLightComponent
     * @brief 한 점에서 **원뿔 안으로만** 퍼지는 빛입니다. 위치와 방향이 모두 월드 트랜스폼에서 나옵니다.
     * @details 점광에 원뿔을 곱한 것입니다. 거리 감쇠는 `PointLightComponent` 와 **같은 식**을 쓰고
     *          (`lighting.hlsli` 의 한 분기), 거기에 안쪽/바깥쪽 각 사이의 부드러운 감쇠를 곱합니다.
     *          언리얼 `USpotLightComponent` 의 `InnerConeAngle` / `OuterConeAngle` 과 같은 규약입니다.
     * @note 방향은 기본 방향(**아래**)을 이 컴포넌트의 월드 회전으로 돌린 것입니다(`LightComponent` 의 방향 규약). 기본이 아래인
     *       까닭: 전방 +Z 를 쓰면 카메라 쪽으로 쏴 화면이 통째로 하얘져 "스폿인지" 를 알 수 없습니다.
     * @note 점광과 같은 이유로 **그림자를 드리우지 않습니다.** 그림자 맵이 방향광 하나에 묶여 있습니다.
     */
    REFLECT( Category = "Rendering 3D", DisplayName = "Spot Light", Tooltip = "Cone-shaped light with inner/outer falloff" )
    class SW_API SpotLightComponent : public LightComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 기본 스포트라이트 값으로 만듭니다. */
        SpotLightComponent();
        /** @brief 기본 소멸자입니다. */
        virtual ~SpotLightComponent() override = default;

        /** @brief 빛이 닿는 반경입니다. 이 밖은 0 이 되어 계산에서 빠집니다. */
        float32 getRadius() const { return _radius; }
        /** @brief 반경을 설정합니다. */
        void setRadius( float32 radius );

        /** @brief 안쪽 원뿔 반각(라디안)입니다. 이 안은 감쇠가 없습니다. */
        float32 getInnerConeAngle() const { return _innerConeAngle; }
        /** @brief 안쪽 원뿔 반각을 설정합니다. 바깥 각을 넘으면 바깥 각으로 잘립니다. */
        void setInnerConeAngle( float32 radians );

        /** @brief 바깥 원뿔 반각(라디안)입니다. 이 밖은 완전히 어둡습니다. */
        float32 getOuterConeAngle() const { return _outerConeAngle; }
        /** @brief 바깥 원뿔 반각을 설정합니다. */
        void setOuterConeAngle( float32 radians );

        /** @brief 빛이 나아가는 방향(정규화)입니다. 기본 방향(아래)을 월드 회전으로 돌린 것입니다. */
        float3 getLightDirection() const;

    private:
        PROPERTY( Category = "Light", DisplayName = "Radius", Min = 0.0, Tooltip = "Distance at which the light reaches zero", Meta = "Units=m" )
        float32 _radius;
        PROPERTY( Category = "Cone", DisplayName = "Inner Cone Angle", Min = 0.0, Tooltip = "Half angle with no falloff", Meta = "Units=rad" )
        float32 _innerConeAngle;
        PROPERTY( Category = "Cone", DisplayName = "Outer Cone Angle", Min = 0.0, Tooltip = "Half angle where the light reaches zero", Meta = "Units=rad" )
        float32 _outerConeAngle;
    };
} // namespace sw
