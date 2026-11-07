#include "pch.h"

#include "GameFramework/Kits/Feature/World/Overworld/PlayerLocomotion.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

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
        if ( _stateTimer.update( deltaTime ) == false )
            return;

        if ( _state == LocomotionState::Walk || _state == LocomotionState::Interact )
            _state = LocomotionState::Idle;
    }

    void PlayerLocomotion::notifyStepStarted( float32 stepSeconds )
    {
        // 시간이 0 이하면 타이머가 서지 않아 Walk 에 갇힌다 - 그 자리에서 끝난 걸음이다.
        _stateTimer.start( stepSeconds );
        _state = _stateTimer.isActive() ? LocomotionState::Walk : LocomotionState::Idle;
    }

    void PlayerLocomotion::notifyStepFinished()
    {
        if ( _state == LocomotionState::Walk )
            _state = LocomotionState::Idle;
        _stateTimer.clear();
    }

    void PlayerLocomotion::beginInteract( float32 duration )
    {
        _stateTimer.start( duration );
        _state = _stateTimer.isActive() ? LocomotionState::Interact : LocomotionState::Idle;
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

    void PlayerLocomotion::serializeState( StateIO& io )
    {
        io.enumValue( _state, LocomotionState::Interact );
        io.enumValue( _facing, FacingDir::Up );
        io.value( _stateTimer );
        io.check( _state == LocomotionState::Idle || _stateTimer.isActive() ); // 남은 시간 없는 걸음은 깨어날 길이 없다
    }

} // namespace sw
