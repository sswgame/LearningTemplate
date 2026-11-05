#include "pch.h"

#include "GameFramework/Kits/Feature/World/Overworld/PlayerLocomotion.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Foundation/Utility/StateArchiveUtil.h"

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
        const bool bBusy  = state != static_cast<uint8>( LocomotionState::Idle );
        if ( bValid == false || ( bBusy && stateTimer.isActive() == false ) ) // 남은 시간 없는 걸음은 깨어날 길이 없다
            return false;
        _state      = static_cast<LocomotionState>( state );
        _facing     = static_cast<FacingDir>( facing );
        _stateTimer = stateTimer;
        return true;
    }
} // namespace sw
