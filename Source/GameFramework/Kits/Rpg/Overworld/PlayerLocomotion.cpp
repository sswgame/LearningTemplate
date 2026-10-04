#include "pch.h"

#include "GameFramework/Kits/Rpg/Overworld/PlayerLocomotion.h"

namespace sw
{
    PlayerLocomotion::PlayerLocomotion()
        : _state{ LocomotionState::Idle }
        , _facing{ FacingDir::Down }
        , _stateTimer{}
    {
    }

    void PlayerLocomotion::setState( LocomotionState state )
    {
        _state = state;
        _stateTimer.clear();
    }

    void PlayerLocomotion::update( float32 deltaTime )
    {
        if ( _stateTimer.isActive() == false )
            return;
        if ( _stateTimer.tick( deltaTime ) == false )
            return;

        if ( _state == LocomotionState::Walk || _state == LocomotionState::Interact )
            _state = LocomotionState::Idle;
    }

    void PlayerLocomotion::notifyStepStarted()
    {
        _state = LocomotionState::Walk;
        _stateTimer.start( kStepDuration );
    }

    void PlayerLocomotion::notifyStepFinished()
    {
        if ( _state == LocomotionState::Walk )
            _state = LocomotionState::Idle;
        _stateTimer.clear();
    }

    void PlayerLocomotion::beginInteract( float32 duration )
    {
        _state = LocomotionState::Interact;
        _stateTimer.start( duration );
    }

    void PlayerLocomotion::setFacingFromDelta( int32 dx, int32 dy )
    {
        if ( dy < 0 )
            _facing = FacingDir::Up;
        else if ( dy > 0 )
            _facing = FacingDir::Down;
        else if ( dx < 0 )
            _facing = FacingDir::Left;
        else if ( dx > 0 )
            _facing = FacingDir::Right;
    }

    bool PlayerLocomotion::canAcceptMoveInput() const
    {
        return _state == LocomotionState::Idle;
    }
} // namespace sw
