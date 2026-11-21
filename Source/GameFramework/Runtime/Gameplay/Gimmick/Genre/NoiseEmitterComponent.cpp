#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Genre/NoiseEmitterComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Runtime/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickEvents.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickSensorComponent.h"

namespace sw
{
    NoiseEmitterComponent::NoiseEmitterComponent()
        : _radius{ 8.0f }
        , _interval{ 0.0f }
        , _bOnStep{ true }
        , _noiseRadius{ 0.0f }
        , _steps{ 0 }
        , _lastOccupantCount{ 0 }
        , _emitCount{ 0 }
        , _clock{}
    {
    }

    void NoiseEmitterComponent::emit( float32 radius )
    {
        _noiseRadius       = MathUtil::max( _noiseRadius, radius );
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        ++_emitCount;
        GimmickNoiseEvent event;
        event._source = pOwner->getHandle();
        event._radius = radius;
        if ( pOwner->getPrimarySceneComponent() != nullptr )
            event._position = pOwner->getPrimarySceneComponent()->getWorldPosition();
        GameEventUtil::send( event );
    }

    void NoiseEmitterComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        const int32 stepCount = _clock.consume( context.getDeltaTime() );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepOnce();
        }
    }

    void NoiseEmitterComponent::stepOnce()
    {
        _noiseRadius = 0.0f;
        ++_steps;
        if ( _interval > 0.0f && _steps % _clock.secondsToSteps( _interval, 1 ) == 0 )
            emit( _radius );
        const GameObject*             pOwner  = getOwner();
        const GimmickSensorComponent* pSensor = pOwner != nullptr ? pOwner->getComponent<GimmickSensorComponent>() : nullptr;
        const int32                   count   = pSensor != nullptr ? pSensor->getOccupantCount() : 0;
        if ( _bOnStep && count > _lastOccupantCount )
            emit( _radius );
        _lastOccupantCount = count;
    }
} // namespace sw
