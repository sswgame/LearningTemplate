/**
 * @file FogComponent.h
 * @brief 씬의 안개입니다. 높이 · 거리 지수 안개(색, 밀도, 높이 감쇠, 기준 높이, 시작 거리, 최대 불투명도)와 방향광 쪽 산란, 선택으로 라이트 산란을 넣는 볼륨 안개를 정합니다.
 * @details 렌더러는 이 값을 패킷으로 받아 안개 패스(`RenderPassType::Fog`, fog.hlsl)가 불투명 · 반투명을 그린 뒤 장면 색에 섞는다(`FogSettings` 머리 주석의 식).
 *          언리얼 ExponentialHeightFog(+ Volumetric Fog), 유니티 HDRP Fog 자리다.
 *          카메라가 물속이면(`WaterBodyComponent::findUnderwaterFog`) 물의 안개 값이 이 컴포넌트의 값을 덮는다 — 균일한 안개이고 안개 컴포넌트가 없는 씬도 물속은 안개가 낀다(`findFog`).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Environment/Fog/FogSettings.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObjectManager;
} // namespace sw

namespace sw
{
    /**
     * @class FogComponent
     * @brief 전역 안개 하나입니다. 여럿이면 처음 찾은 것이 이깁니다(`SkyComponent` 와 같은 규칙). 없으면 안개 패스가 돌지 않습니다(물속만 예외).
     */
    REFLECT( Category = "Environment", DisplayName = "Fog", Tooltip = "Global exponential height fog with optional light-scattering volume", Meta = "Icon=LightGlobal, IconColor=Environment" )
    class SW_API FogComponent : public Component
    {
    public:
        REFLECT_BODY();

        FogComponent();
        virtual ~FogComponent() override = default;

        /** @brief 씬의 안개 목록(`ComponentRegistry`)에 등록합니다. `findFog` 가 씬 전체를 훑지 않고 이 목록을 봅니다. */
        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;

        /** @brief 지금 값입니다. */
        FogSettings makeSettings() const;
        void        setDensity( float32 density ) { _density = density; }
        void        setHeightFalloff( float32 heightFalloff ) { _heightFalloff = heightFalloff; }
        void        setColor( const float3& color ) { _color = color; }
        void        setVolumetric( bool bVolumetric ) { _bVolumetric = bVolumetric; }

        /**
         * @brief @p manager 의 안개를 @p outSettings 에 구합니다. @p viewPosition(주 시점의 눈)이 어떤 물의 물속이면 그 물의 안개가 컴포넌트의 값을 덮습니다.
         * @return 안개가 있으면(컴포넌트 또는 물속) true, 없으면 false 이고 @p outSettings 는 기본값입니다. 읽기만 합니다.
         */
        [[nodiscard]] static bool findFog( const GameObjectManager& manager, const float3& viewPosition, FogSettings& outSettings );

    private:
        PROPERTY( Category = "Fog", DisplayName = "Color", Meta = "Color", Tooltip = "Linear RGB of the fog" )
        float3 _color;
        PROPERTY( Category = "Fog", DisplayName = "Density", Min = 0.0, Units = m, Tooltip = "Fog density at the base height (per metre)" )
        float32 _density;
        PROPERTY( Category = "Fog", DisplayName = "Height Falloff", Min = 0.0, Tooltip = "How fast the density drops with height (per metre); 0 is uniform fog" )
        float32 _heightFalloff;
        PROPERTY( Category = "Fog", DisplayName = "Base Height", Units = m, Tooltip = "World height where the density equals Density" )
        float32 _baseHeight;
        PROPERTY( Category = "Fog", DisplayName = "Start Distance", Min = 0.0, Units = m, Tooltip = "No fog closer than this" )
        float32 _startDistance;
        PROPERTY( Category = "Fog", DisplayName = "Max Opacity", Min = 0.0, Max = 1.0, Tooltip = "Upper bound of the fog opacity" )
        float32 _maxOpacity;
        PROPERTY( Category = "Directional Inscattering", DisplayName = "Color", Meta = "Color", Tooltip = "Linear RGB added toward the directional light" )
        float3 _inscatterColor;
        PROPERTY( Category = "Directional Inscattering", DisplayName = "Intensity", Min = 0.0, Tooltip = "0 turns the directional inscattering off" )
        float32 _inscatterIntensity;
        PROPERTY( Category = "Directional Inscattering", DisplayName = "Exponent", Min = 1.0, Tooltip = "Higher keeps the glow in a narrower cone around the light" )
        float32 _inscatterExponent;
        PROPERTY( Category = "Volumetric", DisplayName = "Volumetric", Tooltip = "Light the fog with the scene lights (froxel volume); costs a compute pass" )
        bool _bVolumetric;
        PROPERTY( Category = "Volumetric", DisplayName = "Volume Distance", Min = 1.0, Units = m, EditCondition = "_bVolumetric", Tooltip = "How far the volume reaches; beyond it the plain fog continues" )
        float32 _volumeDistance;
        PROPERTY( Category = "Volumetric", DisplayName = "Scattering", Min = 0.0, EditCondition = "_bVolumetric", Tooltip = "Multiplies how bright the lights shine in the fog" )
        float32 _scattering;
        PROPERTY( Category = "Volumetric", DisplayName = "Anisotropy", Min = -0.95, Max = 0.95, EditCondition = "_bVolumetric", Tooltip = "Phase function: above 0 scatters toward the light, below 0 away" )
        float32 _anisotropy;
    };
} // namespace sw
