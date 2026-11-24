#include "pch.h"

#include "GameFramework/Runtime/World/Environment/FlickerLightComponent.h"

#include "Core/Foundation/Constants.h"
#include "Core/String/StringUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Visual/Component/LightComponent.h"

namespace sw
{
    FlickerLightComponent::FlickerLightComponent()
        : _pattern{}
        , _rate{ 10.0f }
        , _step{ 0 }
        , _baseIntensity{ 1.0f }
        , _appliedScale{ -1.0f }
    {
    }

    void FlickerLightComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        const GameObject*     pOwner = getOwner();
        const LightComponent* pLight = pOwner != nullptr ? pOwner->getComponent<LightComponent>() : nullptr;
        if ( pLight != nullptr )
            _baseIntensity = pLight->getIntensity();
    }

    float32 FlickerLightComponent::computeScaleAtStep( int32 step ) const
    {
        if ( _pattern.empty() )
            return 1.0f;
        const int32 letterIndex = static_cast<int32>( static_cast<float32>( step ) * constant::time::kDefaultFixedStepSeconds * _rate ) % static_cast<int32>( _pattern.size() );
        const utf8  letter      = StringUtil::toLowerChar( _pattern[static_cast<size_t>( letterIndex )] );
        if ( letter < 'a' || 'z' < letter )
            return 1.0f;
        return static_cast<float32>( letter - 'a' ) / static_cast<float32>( 'm' - 'a' );
    }

    void FlickerLightComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _step += context.getIntervalCount();
        const GameObject*     pOwner               = getOwner();
        const LightComponent* pLight               = pOwner != nullptr ? pOwner->getComponent<LightComponent>() : nullptr;
        const float32         scale                = computeScaleAtStep( _step );
        const bool            bNoLightUpdateNeeded = pLight == nullptr || scale == _appliedScale;
        if ( bNoLightUpdateNeeded )
            return;
        _appliedScale = scale;
        context.deferPostTickOnComponent( *this, []( FlickerLightComponent& flicker )
        {
            LightComponent* pResolved = flicker.getOwner()->getComponent<LightComponent>();
            if ( pResolved != nullptr )
                pResolved->setIntensity( flicker._baseIntensity * flicker._appliedScale );
        } );
    }
} // namespace sw
