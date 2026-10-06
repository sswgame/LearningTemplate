#include "pch.h"

#include "GameFramework/Kits/Rpg/Overworld/PlayerLocomotion.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Utility/StateArchiveUtil.h"

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

    void PlayerLocomotion::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint8>( _state );
        outArchive << static_cast<uint8>( _facing );
        StateArchiveUtil::writeCountdown( outArchive, _stateTimer );
    }

    bool PlayerLocomotion::readState( Archive& archive )
    {
        uint8     state  = 0;
        uint8     facing = 0;
        Countdown stateTimer;
        archive >> state;
        archive >> facing;
        if ( StateArchiveUtil::readCountdown( archive, stateTimer ) == false )
            return false;
        const bool bValid = archive.isOk() && state <= static_cast<uint8>( LocomotionState::Interact ) && facing <= static_cast<uint8>( FacingDir::Up );
        if ( bValid == false )
            return false;
        _state      = static_cast<LocomotionState>( state );
        _facing     = static_cast<FacingDir>( facing );
        _stateTimer = stateTimer;
        return true;
    }
} // namespace sw
