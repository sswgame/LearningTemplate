/**
 * @file Light2DComponent.h
 * @brief 2D 빛 — 점 · 스폿(`PointLight2DComponent`)과 전역(`GlobalLight2DComponent`)입니다. 빛 받는 스프라이트(`sprite2dlit.material`)만 비춥니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Object/Component/3D/LightComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class PointLight2DComponent
     * @brief 한 점에서 퍼지는 2D 빛입니다. 바깥 각을 줄이면 스폿(원뿔)입니다. 그림자를 켜면 2D 가림막(`ShadowCaster2DComponent`)에 가려집니다.
     * @details 유니티 Light 2D(Point · 안/바깥 반경 · 안/바깥 각 · Falloff · Normal Map Distance · Shadows), Godot PointLight2D 의 자리입니다.
     *          감쇠: 안 반경 안은 1, 바깥 반경에서 0, 그 사이는 ((바깥 − 거리) / (바깥 − 안))^지수(`computeAttenuation` — 셰이더 lighting2d.hlsli 와 같은 식).
     *          원뿔 방향은 컴포넌트의 로컬 +X 를 월드 회전으로 돌린 것입니다(2D 는 Z 축 회전).
     */
    REFLECT( Category = "Rendering 2D", DisplayName = "Point Light 2D", Tooltip = "2D point or spot light for lit sprites" )
    class SW_API PointLight2DComponent : public LightComponent
    {
    public:
        REFLECT_BODY();
        PointLight2DComponent();
        virtual ~PointLight2DComponent() override = default;

        /**
         * @brief 빛에서 @p distance 떨어진 곳의 감쇠(원뿔 제외)입니다. 셰이더 `swComputeLight2dAttenuation` 과 같은 식입니다.
         * @param exponent 1 이면 선형, 2 면 제곱으로 빨리 어두워집니다
         */
        static float32 computeAttenuation( float32 distance, float32 innerRadius, float32 outerRadius, float32 exponent );

        void setRadius( float32 innerRadius, float32 outerRadius );
        void setFalloffExponent( float32 exponent ) { _falloffExponent = exponent; }
        /** @brief 원뿔의 안 · 바깥 전체 각(라디안)입니다. 2π 면 원뿔이 없습니다. */
        void    setConeAngles( float32 innerAngle, float32 outerAngle );
        void    setCastShadows( bool bCastShadows ) { _bCastShadows = bCastShadows; }
        void    setNormalMapHeight( float32 height ) { _normalMapHeight = height; }
        float32 getOuterRadius() const { return _outerRadius; }

    protected:
        void writeGpuLightKindFields( GpuLight& outLight ) const override;

    private:
        PROPERTY( Category = "Light", DisplayName = "Inner Radius", Min = 0.0, Tooltip = "Full intensity inside this distance", Meta = "Units=m" )
        float32 _innerRadius;
        PROPERTY( Category = "Light", DisplayName = "Outer Radius", Min = 0.01, Tooltip = "Distance at which the light reaches zero", Meta = "Units=m" )
        float32 _outerRadius;
        PROPERTY( Category = "Light", DisplayName = "Falloff Exponent", Min = 0.01, Tooltip = "1 fades linearly, larger values fade faster near the edge" )
        float32 _falloffExponent;
        PROPERTY( Category = "Light", DisplayName = "Inner Angle", Min = 0.0, Max = 6.2831853, Tooltip = "Full intensity inside this cone angle", Meta = "Units=rad" )
        float32 _innerAngle;
        PROPERTY( Category = "Light", DisplayName = "Outer Angle", Min = 0.0, Max = 6.2831853, Tooltip = "Cone angle where the light reaches zero; a full turn is a point light",
                  Meta = "Units=rad" )
        float32 _outerAngle;
        PROPERTY( Category = "Light", DisplayName = "Normal Map Height", Min = 0.0, Tooltip = "How far in front of the sprite plane the light sits for normal maps",
                  Meta = "Units=m" )
        float32 _normalMapHeight;
        PROPERTY( Category = "Light", DisplayName = "Cast Shadows", Tooltip = "Shadow Caster 2D shapes block this light" )
        bool _bCastShadows;
    };
} // namespace sw

namespace sw
{
    /**
     * @class GlobalLight2DComponent
     * @brief 빛 받는 스프라이트 모두에 같은 밝기를 주는 바탕 빛입니다(유니티 Global Light 2D · Godot CanvasModulate 의 자리).
     * @details 2D 빛이 하나도 없으면 빛 받는 스프라이트는 빛 없이(그대로) 그려집니다 — 빛을 하나라도 두면 바탕이 0 이 되므로 어두운 장면은 이것으로 바탕을 줍니다.
     */
    REFLECT( Category = "Rendering 2D", DisplayName = "Global Light 2D", Tooltip = "Ambient light for every lit sprite" )
    class SW_API GlobalLight2DComponent : public LightComponent
    {
    public:
        REFLECT_BODY();
        GlobalLight2DComponent();
        virtual ~GlobalLight2DComponent() override = default;

    protected:
        void writeGpuLightKindFields( GpuLight& outLight ) const override;
    };
} // namespace sw
