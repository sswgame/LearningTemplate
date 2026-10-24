#include "pch.h"

#include "Engine/Environment/Sky/SkyComponent.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

namespace sw
{
    SkyComponent::SkyComponent()
        : _zenithColor{ 0.25f, 0.45f, 0.85f }
        , _horizonColor{ 0.75f, 0.82f, 0.90f }
        , _groundColor{ 0.30f, 0.27f, 0.24f }
        , _intensity{ 1.0f }
        , _horizonExponent{ 4.0f }
        , _mode{ SkyMode::Gradient }
        , _rayleighScale{ 1.0f }
        , _mieScale{ 1.0f }
        , _mieAnisotropy{ 0.76f }
        , _sunIntensity{ 20.0f }
        , _observerAltitudeKm{ 0.0f }
        , _sunDiscAngle{ 0.03f }
        , _planetRadiusKm{ 6360.0f }
        , _atmosphereHeightKm{ 60.0f }
        , _bClouds{ false }
        , _cloudCoverage{ 0.5f }
        , _cloudAltitudeKm{ 2.0f }
        , _cloudScale{ 0.4f }
        , _cloudOpacity{ 0.9f }
        , _cloudWindDirection{ 0.0f }
        , _cloudWindSpeedKmPerSecond{ 0.01f }
        , _cloudSoftness{ 0.15f }
        , _cloudColor{ 1.0f, 1.0f, 1.0f }
    {
    }

    SkyGradient SkyComponent::makeGradient() const
    {
        SkyGradient gradient;
        gradient._zenithColor     = _zenithColor;
        gradient._horizonColor    = _horizonColor;
        gradient._groundColor     = _groundColor;
        gradient._intensity       = _intensity;
        gradient._horizonExponent = _horizonExponent;
        gradient._mode            = _mode;

        SkyAtmosphere& atmosphere      = gradient._atmosphere;
        atmosphere._rayleighScale      = _rayleighScale;
        atmosphere._mieScale           = _mieScale;
        atmosphere._mieAnisotropy      = _mieAnisotropy;
        atmosphere._sunIntensity       = _sunIntensity;
        atmosphere._planetRadiusKm     = _planetRadiusKm;
        atmosphere._atmosphereHeightKm = _atmosphereHeightKm;
        atmosphere._observerAltitudeKm = _observerAltitudeKm;
        atmosphere._sunDiscAngle       = _sunDiscAngle;

        SkyClouds& clouds            = gradient._clouds;
        clouds._bEnabled             = _bClouds ? SW_TRUE : SW_FALSE;
        clouds._coverage             = _cloudCoverage;
        clouds._altitudeKm           = _cloudAltitudeKm;
        clouds._scale                = _cloudScale;
        clouds._opacity              = _cloudOpacity;
        clouds._windDirection        = _cloudWindDirection;
        clouds._windSpeedKmPerSecond = _cloudWindSpeedKmPerSecond;
        clouds._softness             = _cloudSoftness;
        clouds._color                = _cloudColor;
        return gradient;
    }

    void SkyComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<SkyComponent>( this );
    }

    void SkyComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<SkyComponent>( this );
        Component::onUnregister( manager );
    }

    bool SkyComponent::findSky( const GameObjectManager& manager, SkyGradient& outGradient )
    {
        outGradient = SkyGradient{};
        for ( const SkyComponent* pSky : manager.getComponentRegistry().getAll<SkyComponent>() )
        {
            if ( pSky->isPendingDestroy() || pSky->isActive() == false )
                continue;
            outGradient = pSky->makeGradient();
            // 대기 모드의 해는 씬의 첫 활성 방향광이다(Scene::findActiveDirectionalLight 와 같은 규칙). 없으면 해 색 0 — 하늘이 어둡다. 구름도 같은 해로 빛을 받는다.
            for ( LightComponent* pLight : manager.getComponentRegistry().getAll<LightComponent>( shaderslot::kLightTypeDirectional ) )
            {
                if ( pLight == nullptr || pLight->isActive() == false )
                    continue;
                DirectionalLightComponent* pDirectional = static_cast<DirectionalLightComponent*>( pLight );
                outGradient.setSun( pDirectional->getLightDirection(), pDirectional->getColor() );
                break;
            }
            return true;
        }
        return false;
    }
} // namespace sw
