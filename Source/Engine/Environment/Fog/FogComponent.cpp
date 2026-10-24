#include "pch.h"

#include "Engine/Environment/Fog/FogComponent.h"

#include "Engine/Environment/Water/WaterBodyComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

namespace sw
{
    FogComponent::FogComponent()
        : _color{ 0.50f, 0.60f, 0.70f }
        , _density{ 0.02f }
        , _heightFalloff{ 0.2f }
        , _baseHeight{ 0.0f }
        , _startDistance{ 0.0f }
        , _maxOpacity{ 1.0f }
        , _inscatterColor{ 1.0f, 0.9f, 0.7f }
        , _inscatterIntensity{ 0.0f }
        , _inscatterExponent{ 8.0f }
        , _bVolumetric{ false }
        , _volumeDistance{ 64.0f }
        , _scattering{ 1.0f }
        , _anisotropy{ 0.3f }
    {
    }

    FogSettings FogComponent::makeSettings() const
    {
        FogSettings settings;
        settings._color              = _color;
        settings._density            = _density;
        settings._heightFalloff      = _heightFalloff;
        settings._baseHeight         = _baseHeight;
        settings._startDistance      = _startDistance;
        settings._maxOpacity         = _maxOpacity;
        settings._inscatterColor     = _inscatterColor;
        settings._inscatterIntensity = _inscatterIntensity;
        settings._inscatterExponent  = _inscatterExponent;
        settings._volumeDistance     = _volumeDistance;
        settings._scattering         = _scattering;
        settings._anisotropy         = _anisotropy;
        settings._bVolumetric        = _bVolumetric ? SW_TRUE : SW_FALSE;
        return settings;
    }

    void FogComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<FogComponent>( this );
    }

    void FogComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<FogComponent>( this );
        Component::onUnregister( manager );
    }

    bool FogComponent::findFog( const GameObjectManager& manager, const float3& viewPosition, FogSettings& outSettings )
    {
        outSettings = FogSettings{};
        // 물속은 컴포넌트의 값을 덮는다: 높이와 무관한 균일 안개이고(감쇠 0) 라이트 산란 · 볼륨은 없다. 시작 거리 0, 불투명도 상한 1 이다.
        WaterUnderwaterFog underwater;
        if ( WaterBodyComponent::findUnderwaterFog( manager, viewPosition, underwater ) )
        {
            outSettings._color         = float3{ underwater._color._x, underwater._color._y, underwater._color._z };
            outSettings._density       = underwater._density;
            outSettings._heightFalloff = 0.0f;
            outSettings._bUnderwater   = SW_TRUE;
            return true;
        }
        for ( const FogComponent* pFog : manager.getComponentRegistry().getAll<FogComponent>() )
        {
            if ( pFog->isPendingDestroy() || pFog->isActive() == false )
                continue;
            outSettings = pFog->makeSettings();
            return true;
        }
        return false;
    }
} // namespace sw
