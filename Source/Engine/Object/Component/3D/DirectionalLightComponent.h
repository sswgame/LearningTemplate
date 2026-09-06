/**
 * @file DirectionalLightComponent.h
 * @brief 씬의 주광(directional key light) — 방향·색·그림자 볼륨을 선언으로 다룹니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    namespace generated
    {
        struct sw_DirectionalLightComponent_Registrar;
    } // namespace generated

    /**
     * @class DirectionalLightComponent
     * @brief 방향광 하나. 빛 방향은 이 컴포넌트의 월드 트랜스폼에서 나옵니다.
     * @details 예전에는 주광의 방향·색·세기와 그림자 직교 볼륨이 전부 FrameRendererConstants.cpp
     *          안의 `constexpr` 상수였다. 그래서 씬이 커져도 그림자 볼륨은 2 유닛짜리 그대로였고
     *          (큐브 5000 개 격자는 142 유닛이다), 빛을 돌려보려면 엔진을 다시 빌드해야 했다.
     *          카메라가 CameraComponent 로 선언되듯 빛도 컴포넌트로 선언한다.
     * @note 방향은 이 컴포넌트의 전방 벡터다 — 회전을 주면 빛이 돈다. 언리얼의
     *       `UDirectionalLightComponent` 와 같은 규약이다.
     */
    REFLECT( Category = "Rendering 3D", DisplayName = "Directional Light", Tooltip = "Scene key light and shadow volume" )
    class SW_API DirectionalLightComponent : public SceneComponent
    {
        friend struct ::sw::generated::sw_DirectionalLightComponent_Registrar;

    public:
        REFLECT_BODY();

        /** @brief 기본 주광 값으로 만듭니다. */
        DirectionalLightComponent();
        /** @brief 기본 소멸. */
        virtual ~DirectionalLightComponent() override = default;

        /** @brief 라이트를 이동합니다. */
        DirectionalLightComponent( DirectionalLightComponent&& ) noexcept = default;
        /** @brief 이동 대입입니다. */
        DirectionalLightComponent& operator=( DirectionalLightComponent&& ) noexcept = default;

        /** @brief 빛이 나아가는 방향(정규화). 컴포넌트 전방 벡터입니다. */
        float3 getLightDirection() const;

        /** @brief 빛 색입니다. */
        const float3& getColor() const { return _color; }
        /** @brief 빛 색을 설정합니다. */
        void setColor( const float3& color );

        /** @brief 빛 세기입니다. */
        float32 getIntensity() const { return _intensity; }
        /** @brief 빛 세기를 설정합니다. */
        void setIntensity( float32 intensity );

        /** @brief 환경광 세기입니다. */
        float32 getAmbient() const { return _ambient; }
        /** @brief 환경광 세기를 설정합니다. */
        void setAmbient( float32 ambient );

        /** @brief 그림자 직교 볼륨의 한 변 절반입니다. 씬을 덮을 만큼 커야 합니다. */
        float32 getShadowExtent() const { return _shadowExtent; }
        /** @brief 그림자 직교 볼륨 크기를 설정합니다. */
        void setShadowExtent( float32 extent );

        /** @brief 그림자 카메라를 원점에서 얼마나 뒤로 뺄지입니다. */
        float32 getShadowDistance() const { return _shadowDistance; }
        /** @brief 그림자 카메라 거리를 설정합니다. */
        void setShadowDistance( float32 distance );

        /** @brief 그림자를 드리우면 true. */
        bool castsShadow() const { return _bCastShadow == SW_TRUE; }
        /** @brief 그림자 사용 여부를 설정합니다. */
        void setCastShadow( bool bCastShadow );

        /** @brief 이 라이트의 그림자 view-projection 행렬을 만듭니다. */
        float4x4 buildShadowViewProj() const;

    private:
        PROPERTY( Category = "Light", DisplayName = "Color", Color, Tooltip = "Key light color" )
        float3 _color;
        PROPERTY( Category = "Light", DisplayName = "Intensity", Min = 0.0, Tooltip = "Key light intensity" )
        float32 _intensity;
        PROPERTY( Category = "Light", DisplayName = "Ambient", Min = 0.0, Tooltip = "Ambient term" )
        float32 _ambient;
        PROPERTY( Category = "Shadow", DisplayName = "Shadow Extent", Min = 0.0, Tooltip = "Half size of the shadow ortho volume", Meta = "Units=m" )
        float32 _shadowExtent;
        PROPERTY( Category = "Shadow", DisplayName = "Shadow Distance", Min = 0.0, Tooltip = "Shadow camera pullback along the light direction", Meta = "Units=m" )
        float32 _shadowDistance;
        PROPERTY( Category = "Shadow", DisplayName = "Cast Shadow", Tooltip = "Render this light into the shadow map" )
        uint8                  _bCastShadow   : 1;
        [[maybe_unused]] uint8 _reservedLight : 7;
    };
} // namespace sw
