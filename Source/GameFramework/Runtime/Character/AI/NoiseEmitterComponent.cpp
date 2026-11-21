#include "pch.h"

#include "GameFramework/Runtime/Character/AI/NoiseEmitterComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Runtime/Character/AI/AIStimulusSourceComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickEvents.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Trigger/GimmickSensorComponent.h"

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

    void NoiseEmitterComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 소리는 같은 오브젝트의 자극 원천이 AI 청각 색인에 건다 — 없으면 아무 AI 도 듣지 못하므로 조용히 넘기지 않는다.
        const GameObject* pOwner = getOwner();
        if ( pOwner != nullptr && pOwner->getComponent<AIStimulusSourceComponent>() == nullptr )
            SW_LOG_ERROR_ID( "NoiseEmitterComponent.NoStimulusSource", "Noise emitter '%#' has no AIStimulusSourceComponent on its object - AI cannot hear it", pOwner->getName().c_str() );
    }

    void NoiseEmitterComponent::emit( float32 radius )
    {
        _noiseRadius       = MathUtil::max( _noiseRadius, radius );
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        ++_emitCount;
        // AI 청각은 받는 쪽이 같은 오브젝트의 자극 원천 하나다 — 원천이 색인에 반경을 걸고 감각들이 색인을 묻는다(언리얼 `UAISense_Hearing::ReportNoiseEvent` 자리).
        AIStimulusSourceComponent* pStimulus = pOwner->getComponent<AIStimulusSourceComponent>();
        if ( pStimulus != nullptr )
            pStimulus->reportNoise( _noiseRadius );
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
        // 지난 프레임에 낸 소리를 거둔다. 소리는 적어도 한 프레임 걸려 있어야 그 프레임에 생각하는 감각이 듣는다(한 프레임에 스텝이 여럿이어도).
        if ( _noiseRadius > 0.0f )
        {
            _noiseRadius                         = 0.0f;
            GameObject*                pOwner    = getOwner();
            AIStimulusSourceComponent* pStimulus = pOwner != nullptr ? pOwner->getComponent<AIStimulusSourceComponent>() : nullptr;
            if ( pStimulus != nullptr )
                pStimulus->clearNoise();
        }
        const int32 stepCount = _clock.consume( context.getDeltaTime() );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepOnce();
        }
    }

    void NoiseEmitterComponent::stepOnce()
    {
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
