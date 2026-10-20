/**
 * @file SkyComponent.h
 * @brief 씬의 하늘입니다. 천정 · 지평선 · 땅 색의 그러데이션 하나로 방향마다의 하늘빛(복사휘도)을 정합니다.
 * @details 렌더러는 이 값을 패킷으로 받아 스카이 패스(`RenderPassType::Sky`, `sky.hlsl`)가 아무것도 그리지 않은 곳(깊이 원경)을 칠한다.
 *          같은 식(`SkyGradient::evaluateRadiance`)을 간접 조명 베이크가 프로브마다 SH 로 투영한다(계획 `IndirectLighting` P2).
 *          언리얼 `SkyAtmosphere` + `SkyLight`, 유니티 `RenderSettings.skybox`(Gradient 앰비언트) 자리이고, 대기 산란 · 큐브맵은 아직 없다.
 */
#pragma once
#include "Core/Common/Types.h"
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
        static bool findSky( const GameObjectManager& manager, SkyGradient& outGradient );

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
    };
} // namespace sw
