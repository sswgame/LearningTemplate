/**
 * @file PointLightComponent.h
 * @brief 점광 하나입니다. 위치 · 색 · 세기 · 반경을 선언으로 다룹니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Object/Component/3D/LightComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class ValidationContext;

    /**
     * @class PointLightComponent
     * @brief 한 점에서 사방으로 퍼지는 빛입니다. 위치는 이 컴포넌트의 월드 트랜스폼에서 나옵니다.
     * @details 방향광과 달리 **여러 개가 동시에 화면에 영향을 줍니다.** 그래서 프레임마다 레지스트리에서
     *          모아 구조 버퍼 하나로 올리고, 포워드 · 디퍼드가 같은 버퍼를 같은 루프로 읽습니다
     *          (`GPULightBuffer` · `lighting.hlsli`).
     * @note 그림자는 켤 때만 드리웁니다(`_bCastShadow`, 기본 끔 — 언리얼 · 유니티의 점광 기본값과 같습니다). 그림자 아틀라스의 칸 여섯에
     *       면(+X −X +Y −Y +Z −Z)마다 90° 원근으로 그립니다(큐브맵 대신 2D 아틀라스 — 유니티 URP · Godot 과 같은 방식, `ShadowSetupUtil`).
     *       칸이 모자라면 앞의 빛부터 받고 나머지는 그림자 없이 비춥니다.
     */
    REFLECT( Category = "Rendering 3D", DisplayName = "Point Light", Tooltip = "Omnidirectional light with a finite radius" )
    class SW_API PointLightComponent : public LightComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 기본 점광 값으로 만듭니다. */
        PointLightComponent();
        /** @brief 기본 소멸자입니다. */
        virtual ~PointLightComponent() override = default;

        /** @brief 빛이 닿는 반경입니다. 이 밖은 0 이 되어 계산에서 빠집니다. */
        float32 getRadius() const { return _radius; }
        /** @brief 반경을 설정합니다. */
        void setRadius( float32 radius );

        /** @brief 그림자를 드리우면 true 입니다. */
        bool castsShadow() const { return _bCastShadow == SW_TRUE; }
        /** @brief 그림자 사용 여부를 설정합니다. */
        void setCastShadow( bool bCastShadow );

    protected:
        /** @brief 월드 위치와 반경(`_positionRadius`)을 씁니다. */
        void writeGPULightKindFields( GPULight& outLight ) const override;

    private:
        PROPERTY( Category = "Light", DisplayName = "Radius", Min = 0.0, Tooltip = "Distance at which the light reaches zero", Units = m, Validate = validateRadius )
        float32 _radius;
        PROPERTY( Category = "Shadow", DisplayName = "Cast Shadow", Tooltip = "Render this light into six cells of the shadow atlas" )
        uint8                  _bCastShadow        : 1;
        [[maybe_unused]] uint8 _reservedPointLight : 7;

        /** @brief 반경이 0 이면 빛이 아무것도 비추지 않는다는 경고를 적습니다(맵 검사). */
        void validateRadius( ValidationContext& context ) const;
    };
} // namespace sw
