#include "pch.h"

#include "Engine/Environment/Sky/SkyComponent.h"

#include "Core/Log/LogOnce.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Visual/Component/DirectionalLightComponent.h"

namespace sw
{
    SkyComponent::SkyComponent()
        : _zenithColor{ 0.25f, 0.45f, 0.85f }
        , _horizonColor{ 0.75f, 0.82f, 0.90f }
        , _groundColor{ 0.30f, 0.27f, 0.24f }
        , _intensity{ 1.0f }
        , _horizonExponent{ 4.0f }
        , _mode{ SkyMode::Gradient }
        , _hdriTexture{}
        , _hdriRotation{}
        , _hdriRadianceMap{}
        , _listHdriAmbient{}
        , _rayleighScale{ 1.0f }
        , _mieScale{ 1.0f }
        , _mieAnisotropy{ 0.76f }
        , _sunIntensity{ 20.0f }
        , _observerAltitudeKm{ 0.0f }
        , _sunDiscAngle{ Radians::fromRadians( 0.03f ) }
        , _planetRadiusKm{ 6360.0f }
        , _atmosphereHeightKm{ 60.0f }
        , _cloudMode{ SkyCloudMode::None }
        , _cloudCoverage{ 0.5f }
        , _cloudAltitudeKm{ 2.0f }
        , _cloudScale{ 0.4f }
        , _cloudOpacity{ 0.9f }
        , _cloudWindDirection{}
        , _cloudWindSpeedKmPerSecond{ 0.01f }
        , _cloudSoftness{ 0.15f }
        , _cloudColor{ 1.0f, 1.0f, 1.0f }
        , _cloudThicknessKm{ 1.5f }
        , _cloudExtinctionPerKm{ 18.0f }
        , _cloudMultiScattering{ 0.5f }
        , _cloudBottomOcclusion{ 0.5f }
        , _cloudGroundLight{ 1.0f }
        , _intensityScale{ 1.0f }
    {
    }

    void SkyComponent::setHdriTexture( string_view path )
    {
        _hdriTexture = path;
    }

    SkyGradient SkyComponent::makeGradient() const
    {
        SkyGradient gradient;
        gradient._zenithColor     = _zenithColor;
        gradient._horizonColor    = _horizonColor;
        gradient._groundColor     = _groundColor;
        gradient._intensity       = _intensity * _intensityScale;
        gradient._horizonExponent = _horizonExponent;
        gradient._mode            = _mode;
        gradient._hdriRotation    = _hdriRotation.radians();
        if ( _mode == SkyMode::Hdri )
        {
            gradient._hdriTexture     = hashed_string( _hdriTexture.c_str() );
            gradient._hdriRadianceMap = hashed_string( _hdriRadianceMap.c_str() );
            // 칸 수가 틀리면 간접 조명을 지어내지 않는다 — 계수는 0 이고 하늘 그림만 나온다.
            if ( _listHdriAmbient.size() == SH9Color::kCoefficientCount )
                Memory::copy( gradient._hdriAmbient._arrCoefficient, _listHdriAmbient.data(), sizeof( gradient._hdriAmbient._arrCoefficient ) );
            else
                SW_LOG_ERROR_ONCE_ID( "Sky.HdriAmbientCount", "HDRI sky needs %# ambient SH coefficients (got %#) - probes get no sky light", SH9Color::kCoefficientCount, static_cast<uint32>( _listHdriAmbient.size() ) );
        }

        SkyAtmosphere& atmosphere      = gradient._atmosphere;
        atmosphere._rayleighScale      = _rayleighScale;
        atmosphere._mieScale           = _mieScale;
        atmosphere._mieAnisotropy      = _mieAnisotropy;
        atmosphere._sunIntensity       = _sunIntensity;
        atmosphere._planetRadiusKm     = _planetRadiusKm;
        atmosphere._atmosphereHeightKm = _atmosphereHeightKm;
        atmosphere._observerAltitudeKm = _observerAltitudeKm;
        atmosphere._sunDiscAngle       = _sunDiscAngle.radians();

        SkyClouds& clouds            = gradient._clouds;
        clouds._mode                 = _cloudMode;
        clouds._coverage             = _cloudCoverage;
        clouds._altitudeKm           = _cloudAltitudeKm;
        clouds._scale                = _cloudScale;
        clouds._opacity              = _cloudOpacity;
        clouds._windDirection        = _cloudWindDirection.radians();
        clouds._windSpeedKmPerSecond = _cloudWindSpeedKmPerSecond;
        clouds._softness             = _cloudSoftness;
        clouds._color                = _cloudColor;
        clouds._thicknessKm          = _cloudThicknessKm;
        clouds._extinctionPerKm      = _cloudExtinctionPerKm;
        clouds._multiScattering      = _cloudMultiScattering;
        clouds._bottomOcclusion      = _cloudBottomOcclusion;
        clouds._groundLight          = _cloudGroundLight;
        return gradient;
    }

    bool SkyComponent::findSky( const GameObjectManager& manager, SkyGradient& outGradient )
    {
        outGradient = SkyGradient{};
        // 하늘은 씬에 하나다(언리얼 Sky Atmosphere 와 같다) — 켜진 것이 둘이면 오류로 쓰지 않는다(등록 순서로 고르지 않는다).
        if ( const SkyComponent* pSky = manager.getComponentRegistry().findUnique<SkyComponent>( []( const SkyComponent& sky )
        { return sky.isPendingDestroy() == false && sky.isActive(); } ) )
        {
            outGradient           = pSky->makeGradient();
            outGradient._sourceID = pSky->getComponentID();
            // 대기 모드의 해는 씬의 주광(가장 밝은 활성 방향광)이다. 없으면 해 색 0 — 하늘이 어둡다. 구름도 같은 해로 빛을 받는다.
            if ( const DirectionalLightComponent* const pDirectional = DirectionalLightComponent::findActive( &manager ) )
                outGradient.setSun( pDirectional->getLightDirection(), pDirectional->getColor() );
            return true;
        }
        return false;
    }
} // namespace sw
