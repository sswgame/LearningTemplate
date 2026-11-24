#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Traversal/CrumblePlatformComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/GameObject/Tick/TickInterval.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickUtil.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Trigger/GimmickSensorComponent.h"

namespace sw
{
    CrumblePlatformComponent::CrumblePlatformComponent()
        : _crumbleDelay{ 0.5f }
        , _respawnDelay{ 3.0f }
        , _stepsInState{ 0 }
        , _state{ CrumbleState::Solid }
    {
    }

    void CrumblePlatformComponent::setDelays( float32 crumbleDelay, float32 respawnDelay )
    {
        _crumbleDelay = crumbleDelay;
        _respawnDelay = respawnDelay;
    }

    void CrumblePlatformComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        const int32 stepCount = context.getIntervalCount();
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepOnce();
        }
    }

    void CrumblePlatformComponent::stepOnce()
    {
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        ++_stepsInState;
        switch ( _state )
        {
            case CrumbleState::Solid:
            {
                const GimmickSensorComponent* pSensor = pOwner->getComponent<GimmickSensorComponent>();
                if ( pSensor != nullptr && pSensor->getOccupantCount() > 0 )
                {
                    _state        = CrumbleState::Shaking;
                    _stepsInState = 0;
                }
                break;
            }
            case CrumbleState::Shaking:
            {
                // 흔들리기 시작하면 내려와도 무너진다. 되돌리지 않는다.
                if ( _stepsInState >= TickIntervalUtil::secondsToCount( kTickInterval, _crumbleDelay, 1 ) )
                {
                    _state        = CrumbleState::Fallen;
                    _stepsInState = 0;
                    GimmickUtil::setBodyActive( *pOwner, false, this );
                }
                break;
            }
            case CrumbleState::Fallen:
            {
                if ( _respawnDelay > 0.0f && _stepsInState >= TickIntervalUtil::secondsToCount( kTickInterval, _respawnDelay, 1 ) )
                {
                    _state        = CrumbleState::Solid;
                    _stepsInState = 0;
                    GimmickUtil::setBodyActive( *pOwner, true, this );
                }
                break;
            }
        }
    }
} // namespace sw
