#include "pch.h"

#include "Engine/Environment/Wind/WindComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

namespace sw
{
    WindComponent::WindComponent()
        : _direction{ Radians::fromRadians( 0.6f ) }
        , _strength{ 0.35f }
        , _gustStrength{ 0.25f }
        , _gustFrequency{ 0.25f }
        , _swayFrequency{ 1.2f }
        , _speed{ 2.0f }
    {
    }

    WindSettings WindComponent::makeSettings() const
    {
        WindSettings settings;
        settings._direction     = float2{ MathUtil::cos( _direction.radians() ), MathUtil::sin( _direction.radians() ) };
        settings._strength      = _strength;
        settings._gustStrength  = _gustStrength;
        settings._gustFrequency = _gustFrequency;
        settings._swayFrequency = _swayFrequency;
        settings._speed         = _speed;
        return settings;
    }

    void WindComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<WindComponent>( this );
    }

    void WindComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<WindComponent>( this );
        Component::onUnregister( manager );
    }

    float3 WindSettings::computeVelocity( const float3& position, float32 time ) const
    {
        const float32 along = position._x * _direction._x + position._z * _direction._y;
        const float32 gust  = MathUtil::sin( time * _gustFrequency * MathUtil::kTwoPi + along * kGustPhasePerMeter ) * 0.5f + 0.5f;
        const float32 speed = _speed * ( 1.0f + _gustStrength * gust );
        return float3{ _direction._x * speed, 0.0f, _direction._y * speed };
    }

    bool WindComponent::findWind( const GameObjectManager& manager, WindSettings& outSettings )
    {
        outSettings = WindSettings{};
        // 바람은 씬에 하나다(씬 전체의 풀 · 천 바람) — 켜진 것이 둘이면 오류로 쓰지 않는다(등록 순서로 고르지 않는다).
        const WindComponent* pWind = manager.getComponentRegistry().findUnique<WindComponent>( []( const WindComponent& wind )
        { return wind.isPendingDestroy() == false && wind.isActive(); } );
        if ( pWind == nullptr )
            return false;
        outSettings = pWind->makeSettings();
        return true;
    }
} // namespace sw
