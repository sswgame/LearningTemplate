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
    /**
     * @class LightComponent
     * @brief 빛 하나의 공통 부분입니다. 종류(방향광 · 점광 · 스포트)는 파생이 정하고, 등록부와 수집이 그 종류로 나눕니다.
     * @details 언리얼 `ULightComponent` 의 자리입니다. 예전에는 세 빛이 색 · 세기의 세터 · 등록과 해제 · 위치 · 방향 함수를 각자 들었고,
     *          등록부도 종류마다 add · remove · getAll 세 벌이었습니다 — 빛 종류 하나를 더하면 여덟 자리를 고쳐야 했습니다. 방향 함수
     *          두 벌은 같은 결함(아래)을 같이 갖고 있었습니다. 이제 새 종류는 파생 하나 + 수집(`collectSceneLights`)의 분기 하나 + 셰이더
     *          분기 하나입니다.
     *
     *          **방향 규약.** 파생의 기본 방향은 **로컬** 방향이고, 월드 회전(부모 포함)이 그것을 돌립니다. 회전이 없는 루트 빛은 기본
     *          방향을 그대로 씁니다. 예전에는 "로컬 회전이 0 이면 기본 방향, 아니면 전방(+Z)" 이었습니다 — 부모의 회전을 무시했고,
     *          회전이 1e-3 라디안을 넘는 순간 방향이 기본 방향에서 +Z 로 튀었습니다.
     *
     *          `REFLECT( Abstract )` 라 만들 수 없고 컴포넌트 팩토리도 없습니다. 색 · 세기 PROPERTY 이름은 예전 그대로라(`_color` ·
     *          `_intensity`) 저장된 씬 · 바이너리 · 기본값이 이름으로 그대로 묶입니다.
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

    private:
        PROPERTY( Category = "Light", DisplayName = "Color", Meta = "Color", Tooltip = "Light color" )
        float3 _color;
        PROPERTY( Category = "Light", DisplayName = "Intensity", Min = 0.0, Tooltip = "Light intensity" )
        float32 _intensity;
        uint32  _lightType; ///< `shaderslot::kLightType*`. 만들 때 파생이 정하고 바뀌지 않습니다
    };
} // namespace sw
