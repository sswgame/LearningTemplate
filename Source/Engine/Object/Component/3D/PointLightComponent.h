/**
 * @file PointLightComponent.h
 * @brief 점광 하나입니다. 위치 · 색 · 세기 · 반경을 선언으로 다룹니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class PointLightComponent
     * @brief 한 점에서 사방으로 퍼지는 빛입니다. 위치는 이 컴포넌트의 월드 트랜스폼에서 나옵니다.
     * @details 방향광과 달리 **여러 개가 동시에 화면에 영향을 줍니다.** 그래서 프레임마다 등록부에서
     *          모아 구조 버퍼 하나로 올리고, 포워드 · 디퍼드가 같은 버퍼를 같은 루프로 읽습니다
     *          (`GpuLightBuffer` · `lighting.hlsli`).
     * @note **그림자를 드리우지 않습니다.** 점광 그림자는 큐브맵이 필요한데 이 엔진에는 큐브맵 자원이
     *       없습니다. 없는 것을 있는 척하면 "왜 저 빛만 그림자가 없지" 를 나중에 렌더러 버그로 오인합니다.
     *       그림자를 드리우는 빛은 `DirectionalLightComponent` 하나뿐입니다(그림자 맵도 하나입니다).
     */
    REFLECT( Category = "Rendering 3D", DisplayName = "Point Light", Tooltip = "Omnidirectional light with a finite radius" )
    class SW_API PointLightComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 기본 점광 값으로 만듭니다. */
        PointLightComponent();
        /** @brief 기본 소멸자입니다. */
        virtual ~PointLightComponent() override = default;

        /** @brief 빛 색입니다. */
        const float3& getColor() const { return _color; }
        /** @brief 빛 색을 설정합니다. */
        void setColor( const float3& color );

        /** @brief 빛 세기입니다. */
        float32 getIntensity() const { return _intensity; }
        /** @brief 빛 세기를 설정합니다. */
        void setIntensity( float32 intensity );

        /** @brief 빛이 닿는 반경입니다. 이 밖은 0 이 되어 계산에서 빠집니다. */
        float32 getRadius() const { return _radius; }
        /** @brief 반경을 설정합니다. */
        void setRadius( float32 radius );

        /** @brief 이 빛의 월드 위치입니다. */
        float3 getLightPosition() const;

        /** @brief 씬에 붙을 때 빛 등록부에 자기를 등록합니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 씬에서 떨어질 때 등록을 해제합니다. */
        void onUnregister( GameObjectManager& manager ) override;

    private:
        PROPERTY( Category = "Light", DisplayName = "Color", Color, Tooltip = "Point light color" )
        float3 _color;
        PROPERTY( Category = "Light", DisplayName = "Intensity", Min = 0.0, Tooltip = "Point light intensity" )
        float32 _intensity;
        PROPERTY( Category = "Light", DisplayName = "Radius", Min = 0.0, Tooltip = "Distance at which the light reaches zero", Meta = "Units=m" )
        float32 _radius;
    };
} // namespace sw
