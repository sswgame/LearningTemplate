/**
 * @file SkyComponent.h
 * @brief 씬의 하늘입니다. 천정 · 지평선 · 땅 색의 그러데이션 또는 대기 산란(레일리 · 미)으로 방향마다의 하늘빛(복사휘도)을 정하고, 그 위에 구름 층을 얹을 수 있습니다.
 * @details 렌더러는 이 값을 패킷으로 받아 스카이 패스(`RenderPassType::Sky`, `sky.hlsl`)가 아무것도 그리지 않은 곳(깊이 원경)을 칠한다.
 *          같은 식(`SkyGradient::evaluateRadiance`)을 간접 조명 베이크가 프로브마다 SH 로 투영한다.
 *          언리얼 `SkyAtmosphere` + `SkyLight`, 유니티 `RenderSettings.skybox`(Gradient 앰비언트) 자리이고, 큐브맵은 아직 없다.
 *          대기 모드의 시간대는 씬의 방향광 방향이 정한다(해 = 방향광이 오는 쪽) — 방향광이 없으면 하늘이 어둡다. 구름은 평면 노이즈 층이다(`SkyClouds`).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Environment/Sky/SkyGradient.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObjectManager;
} // namespace sw

namespace sw
{
    /**
     * @class SkyComponent
     * @brief 전역 하늘 하나입니다. 여럿이면 처음 찾은 것이 이깁니다(`WindComponent` 와 같은 규칙). 없으면 배경은 파이프라인의 SceneColor 클리어 색입니다.
     */
    REFLECT( Category = "Environment", DisplayName = "Sky", Tooltip = "Global sky gradient drawn behind the scene", Meta = "Icon=LightGlobal, IconColor=Environment" )
    class SW_API SkyComponent : public Component
    {
    public:
        REFLECT_BODY();

        SkyComponent();
        virtual ~SkyComponent() override = default;

        /** @brief 씬의 하늘 목록(`ComponentRegistry`)에 등록합니다. `findSky` 가 씬 전체를 훑지 않고 이 목록을 봅니다. */
        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief 지금 값입니다. */
        SkyGradient makeGradient() const;
        void        setZenithColor( const float3& color ) { _zenithColor = color; }
        void        setHorizonColor( const float3& color ) { _horizonColor = color; }
        void        setGroundColor( const float3& color ) { _groundColor = color; }
        void        setIntensity( float32 intensity ) { _intensity = intensity; }

        /** @brief @p manager 의 첫 활성 하늘입니다. 없으면 @p outGradient 를 비우고(세기 0) false 입니다. 읽기만 합니다. */
        [[nodiscard]] static bool findSky( const GameObjectManager& manager, SkyGradient& outGradient );

    private:
        PROPERTY( Category = "Sky", DisplayName = "Zenith Color", Meta = "Color", Tooltip = "Linear RGB straight up" )
        float3 _zenithColor;
        PROPERTY( Category = "Sky", DisplayName = "Horizon Color", Meta = "Color", Tooltip = "Linear RGB at the horizon" )
        float3 _horizonColor;
        PROPERTY( Category = "Sky", DisplayName = "Ground Color", Meta = "Color", Tooltip = "Linear RGB straight down (below the horizon)" )
        float3 _groundColor;
        PROPERTY( Category = "Sky", DisplayName = "Intensity", Min = 0.0, Tooltip = "Multiplies the three colors" )
        float32 _intensity;
        PROPERTY( Category = "Sky", DisplayName = "Horizon Exponent", Min = 0.1, Tooltip = "Higher keeps the horizon color in a narrower band" )
        float32 _horizonExponent;
        PROPERTY( Category = "Sky", DisplayName = "Mode", Tooltip = "Gradient: three colors. Atmosphere: Rayleigh + Mie scattering, the sun is the directional light (Ground Color is the ground albedo)" )
        SkyMode _mode;
        PROPERTY( Category = "Atmosphere", DisplayName = "Rayleigh Scale", Min = 0.0, EditCondition = "_mode == Atmosphere", Tooltip = "Multiplies the Rayleigh scattering (blue sky); 0 removes it" )
        float32 _rayleighScale;
        PROPERTY( Category = "Atmosphere", DisplayName = "Mie Scale", Min = 0.0, EditCondition = "_mode == Atmosphere", Tooltip = "Multiplies the Mie scattering (haze, the glow around the sun)" )
        float32 _mieScale;
        PROPERTY( Category = "Atmosphere", DisplayName = "Mie Anisotropy", Min = 0.0, Max = 0.95, EditCondition = "_mode == Atmosphere", Tooltip = "Higher keeps the glow in a narrower cone around the sun" )
        float32 _mieAnisotropy;
        PROPERTY( Category = "Atmosphere", DisplayName = "Sun Intensity", Min = 0.0, EditCondition = "_mode == Atmosphere", Tooltip = "Sun radiance multiplier (sun color is the directional light color)" )
        float32 _sunIntensity;
        PROPERTY( Category = "Atmosphere", DisplayName = "Observer Altitude", Min = 0.0, Units = km, EditCondition = "_mode == Atmosphere", Tooltip = "Eye altitude above the planet surface in kilometres (0.1 = 100 m)" )
        float32 _observerAltitudeKm;
        PROPERTY( Category = "Atmosphere", DisplayName = "Sun Disc Angle", Min = 0.0, Max = 0.2, Units = rad, EditCondition = "_mode == Atmosphere", Tooltip = "Angular radius of the sun disc in radians; 0 hides the disc" )
        float32 _sunDiscAngle;
        PROPERTY( Category = "Atmosphere", DisplayName = "Planet Radius", Min = 100.0, Units = km, EditCondition = "_mode == Atmosphere", Tooltip = "Planet radius in kilometres" )
        float32 _planetRadiusKm;
        PROPERTY( Category = "Atmosphere", DisplayName = "Atmosphere Height", Min = 1.0, Units = km, EditCondition = "_mode == Atmosphere", Tooltip = "Atmosphere thickness in kilometres" )
        float32 _atmosphereHeightKm;
        PROPERTY( Category = "Clouds", DisplayName = "Clouds", Tooltip = "Draw a noise cloud layer over the sky" )
        bool _bClouds;
        PROPERTY( Category = "Clouds", DisplayName = "Coverage", Min = 0.0, Max = 1.0, EditCondition = "_bClouds", Tooltip = "Share of the sky the clouds cover" )
        float32 _cloudCoverage;
        PROPERTY( Category = "Clouds", DisplayName = "Altitude", Min = 0.1, Units = km, EditCondition = "_bClouds", Tooltip = "Height of the cloud layer above the eye in kilometres" )
        float32 _cloudAltitudeKm;
        PROPERTY( Category = "Clouds", DisplayName = "Scale", Min = 0.01, EditCondition = "_bClouds", Tooltip = "Noise frequency per kilometre; higher makes smaller clouds" )
        float32 _cloudScale;
        PROPERTY( Category = "Clouds", DisplayName = "Opacity", Min = 0.0, Max = 1.0, EditCondition = "_bClouds", Tooltip = "Maximum cloud opacity" )
        float32 _cloudOpacity;
        PROPERTY( Category = "Clouds", DisplayName = "Wind Direction", Units = rad, EditCondition = "_bClouds", Tooltip = "Wind direction in radians (from +x toward +z)" )
        float32 _cloudWindDirection;
        PROPERTY( Category = "Clouds", DisplayName = "Wind Speed", Min = 0.0, EditCondition = "_bClouds", Tooltip = "Cloud drift speed in kilometres per second" )
        float32 _cloudWindSpeedKmPerSecond;
        PROPERTY( Category = "Clouds", DisplayName = "Softness", Min = 0.0, Max = 0.5, EditCondition = "_bClouds", Tooltip = "Softness of the cloud edge" )
        float32 _cloudSoftness;
        PROPERTY( Category = "Clouds", DisplayName = "Color", Meta = "Color", EditCondition = "_bClouds", Tooltip = "Linear RGB multiplied onto the lit cloud" )
        float3 _cloudColor;
    };
} // namespace sw
