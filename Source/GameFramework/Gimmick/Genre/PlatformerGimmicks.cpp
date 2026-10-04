#include "pch.h"

#include "GameFramework/Gimmick/Genre/PlatformerGimmicks.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Components/GravityComponent.h"
#include "GameFramework/Framework/GameEventUtil.h"
#include "GameFramework/Gimmick/Genre/GenreGimmickUtil.h"
#include "GameFramework/Gimmick/GimmickSensorComponent.h"

namespace sw
{
    CrumblePlatformComponent::CrumblePlatformComponent()
        : _crumbleDelay{ 0.5f }
        , _respawnDelay{ 3.0f }
        , _stepsInState{ 0 }
        , _state{ CrumbleState::Solid }
        , _clock{ GenreGimmickUtil::makeClock() }
    {
    }

    void CrumblePlatformComponent::setDelays( float32 crumbleDelay, float32 respawnDelay )
    {
        _crumbleDelay = crumbleDelay;
        _respawnDelay = respawnDelay;
    }

    void CrumblePlatformComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        const int32 stepCount = _clock.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
            stepOnce();
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
                // 흔들리기 시작하면 내려와도 무너진다(되돌리지 않는다).
                if ( _stepsInState >= GenreGimmickUtil::toSteps( _crumbleDelay, 1 ) )
                {
                    _state        = CrumbleState::Fallen;
                    _stepsInState = 0;
                    GenreGimmickUtil::setBodyActive( *pOwner, false, this );
                }
                break;
            }
            case CrumbleState::Fallen:
            {
                if ( _respawnDelay > 0.0f && _stepsInState >= GenreGimmickUtil::toSteps( _respawnDelay, 1 ) )
                {
                    _state        = CrumbleState::Solid;
                    _stepsInState = 0;
                    GenreGimmickUtil::setBodyActive( *pOwner, true, this );
                }
                break;
            }
        }
    }

    OneWayPlatformComponent::OneWayPlatformComponent()
        : _normal{ 0.0f, 1.0f, 0.0f }
        , _bDropThrough{ true }
    {
    }

    bool OneWayPlatformComponent::isSolidFor( const float3& velocity, bool bDropRequested ) const
    {
        if ( _bDropThrough && bDropRequested )
            return false;
        return _normal.dot( velocity ) < 0.0f;
    }

    LaunchPadComponent::LaunchPadComponent()
        : _launchVelocity{ 0.0f, 12.0f, 0.0f }
        , _requiredTags{}
        , _launchCount{ 0 }
    {
    }

    void LaunchPadComponent::onOverlapBegin( const OverlapInfo& overlap )
    {
        Component::onOverlapBegin( overlap );
        const bool bAccepted = overlap._pOther != nullptr && overlap._bOtherTrigger == SW_FALSE &&
                               ( _requiredTags.getTagCount() == 0 || overlap._pOther->getTags().hasAllTags( _requiredTags ) );
        if ( bAccepted )
            launch( *overlap._pOther );
    }

    void LaunchPadComponent::launch( GameObject& target )
    {
        ++_launchCount;
        GravityComponent* pGravity = target.getComponent<GravityComponent>();
        if ( pGravity != nullptr )
            pGravity->jump( _launchVelocity._y );
        GimmickLaunchEvent event;
        event._target   = target.getHandle();
        event._source   = getOwner() != nullptr ? getOwner()->getHandle() : GameObjectHandle{};
        event._velocity = _launchVelocity;
        GameEventUtil::send( event );
    }

    ConveyorComponent::ConveyorComponent()
        : _velocity{ 2.0f, 0.0f, 0.0f }
        , _bMoveOccupants{ true }
    {
    }

    void ConveyorComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*                   pOwner   = getOwner();
        GameObjectManager*            pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const GimmickSensorComponent* pSensor  = pOwner != nullptr ? pOwner->getComponent<GimmickSensorComponent>() : nullptr;
        if ( _bMoveOccupants == false || pManager == nullptr || pSensor == nullptr )
            return;
        // 겹친 것이 제 틱에서 쓴 자리 위에 더한다 — 틱 뒤 게임 스레드에서(같은 틱에서 쓰면 그쪽 쓰기와 순서가 정해지지 않는다).
        const ComponentHandle self   = getHandle();
        const float3          offset = _velocity * deltaTime;
        pManager->executeOrDeferPostTick( [pManager, self, offset]()
        {
            const ConveyorComponent*      pConveyor   = static_cast<const ConveyorComponent*>( pManager->resolveComponent( self ) );
            const GameObject*             pBelt       = pConveyor != nullptr ? pConveyor->getOwner() : nullptr;
            const GimmickSensorComponent* pBeltSensor = pBelt != nullptr ? pBelt->getComponent<GimmickSensorComponent>() : nullptr;
            if ( pBeltSensor == nullptr )
                return;
            for ( const GameObjectHandle& handle : pBeltSensor->getOccupants() )
            {
                GameObject*     pOccupant = pManager->resolveGameObject( handle );
                SceneComponent* pScene    = pOccupant != nullptr ? pOccupant->getPrimarySceneComponent() : nullptr;
                if ( pScene != nullptr )
                    pScene->setWorldPosition( pScene->getWorldPosition() + offset );
            }
        } );
    }

    ClimbZoneComponent::ClimbZoneComponent()
        : _axis{ 0.0f, 1.0f, 0.0f }
        , _climbSpeed{ 3.0f }
        , _bSwing{ false }
    {
    }

    bool ClimbZoneComponent::contains( GameObjectHandle object ) const
    {
        const GameObject*             pOwner  = getOwner();
        const GimmickSensorComponent* pSensor = pOwner != nullptr ? pOwner->getComponent<GimmickSensorComponent>() : nullptr;
        if ( pSensor == nullptr )
            return false;
        for ( const GameObjectHandle& occupant : pSensor->getOccupants() )
        {
            if ( occupant == object )
                return true;
        }
        return false;
    }

    const ClimbZoneComponent* ClimbZoneComponent::findClimbZone( const GameObjectManager& manager, GameObjectHandle object )
    {
        const ClimbZoneComponent* pFound = nullptr;
        manager.forEachComponentOfType<ClimbZoneComponent>( [&pFound, object]( ClimbZoneComponent* pZone )
        {
            if ( pFound == nullptr && pZone != nullptr && pZone->isActive() && pZone->contains( object ) )
                pFound = pZone;
        } );
        return pFound;
    }
} // namespace sw
