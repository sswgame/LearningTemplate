/**
 * @file LightComponent.h
 * @brief 빛 종류들의 공통 기반입니다. 색 · 세기 · 위치 · 방향 규약 · 등록부 등록을 한 곳에 둡니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct GpuLight;

    /**
     * @class LightComponent
     * @brief 빛 하나의 공통 부분입니다. 종류(방향광 · 점광 · 스포트)는 파생이 정하고, 등록부와 수집이 그 종류로 나눕니다.
     * @details 언리얼 `ULightComponent` 의 자리입니다. 색 · 세기의 세터 · 등록과 해제 · 위치 · 방향 함수는 여기 하나입니다. 새 종류는 파생
     *          하나(GPU 원소의 자기 칸은 `writeGpuLightKindFields` 재정의) + 셰이더 분기 하나입니다. 수집(`collectSceneLights`)은 종류를 모릅니다.
     *
     *          **방향 규약.** 파생의 기본 방향은 **로컬** 방향이고, 월드 회전(부모 포함)이 그것을 돌립니다. 회전이 없는 루트 빛은 기본
     *          방향을 그대로 씁니다. 주의: "로컬 회전이 0 이면 기본 방향, 아니면 전방(+Z)" 처럼 갈라 두면 부모의 회전을 무시하고, 회전이
     *          문턱을 넘는 순간 방향이 기본 방향에서 +Z 로 튄다.
     *
     *          `REFLECT( Abstract )` 라 만들 수 없고 컴포넌트 팩토리도 없습니다. 색 · 세기 PROPERTY(`_color` · `_intensity`)는 저장된 씬 ·
     *          바이너리 · 기본값이 이름으로 묶입니다.
     */
    REFLECT( Abstract, Category = "Rendering 3D", DisplayName = "Light", Tooltip = "Common part of every light kind" )
    class SW_API LightComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 기본 소멸자입니다. */
        virtual ~LightComponent() override = default;

        /** @brief 빛 종류입니다(`shaderslot::kLightType*`). 등록부와 수집이 이것으로 나눕니다. */
        uint32 getLightType() const { return _lightType; }

        /** @brief 빛 색입니다. */
        const float3& getColor() const { return _color; }
        /** @brief 빛 색을 설정합니다. */
        void setColor( const float3& color );

        /** @brief 빛 세기입니다. */
        float32 getIntensity() const { return _intensity; }
        /** @brief 빛 세기를 설정합니다. 음수는 0 으로 자릅니다. */
        void setIntensity( float32 intensity );

        /** @brief 이 빛의 월드 위치입니다. */
        float3 getLightPosition() const;

        /**
         * @brief 이 빛 하나를 GPU 원소로 씁니다. 공통 칸(색 · 세기 · 종류)은 여기서, 종류마다 다른 칸은 파생의 `writeGpuLightKindFields` 가 채웁니다.
         * @details 그림자 플래그(`_params.x`)는 쓰지 않습니다 — 그림자 맵의 빛은 씬이 하나 고르므로(`Scene::findShadowCastingDirectionalLight`)
         *          모으는 쪽이 켭니다. @p outLight 의 다른 칸은 모두 덮어씁니다.
         */
        void writeGpuLight( GpuLight& outLight ) const;

        /** @brief 씬에 붙을 때 빛 등록부의 자기 종류 칸에 자기를 등록합니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 씬에서 떨어질 때 등록을 해제합니다. */
        void onUnregister( GameObjectManager& manager ) override;

    protected:
        /**
         * @brief 종류와 그 종류의 기본 색 · 세기로 만듭니다. 파생만 부릅니다.
         * @param lightType        `shaderslot::kLightType*`
         * @param defaultColor     기본 색(파생의 `XxxInternal` 상수)
         * @param defaultIntensity 기본 세기
         */
        LightComponent( uint32 lightType, const float3& defaultColor, float32 defaultIntensity );

        /**
         * @brief 로컬 기본 방향을 월드 회전(부모 포함)으로 돌린 빛 방향(정규화)입니다.
         * @details 스케일이 0 이라 방향이 사라지면 기본 방향을 그대로 돌려줍니다. 회전에 대해 연속이고 부모를 따릅니다.
         */
        float3 computeLightDirection( const float3& defaultLocalDirection ) const;

        /**
         * @brief 종류마다 다른 GPU 칸(위치 · 반경 · 방향 · 원뿔)을 씁니다. `writeGpuLight` 가 공통 칸을 채운 뒤 부릅니다.
         * @details 방향은 `_directionType` 의 xyz 만 씁니다 — w 는 종류이고 이미 채워져 있습니다.
         */
        virtual void writeGpuLightKindFields( GpuLight& outLight ) const = 0;

    private:
        PROPERTY( Category = "Light", DisplayName = "Color", Meta = "Color", Tooltip = "Light color" )
        float3 _color;
        PROPERTY( Category = "Light", DisplayName = "Intensity", Min = 0.0, Tooltip = "Light intensity" )
        float32 _intensity;
        uint32  _lightType; ///< `shaderslot::kLightType*`. 만들 때 파생이 정하고 바뀌지 않습니다
    };
} // namespace sw
