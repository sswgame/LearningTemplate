#include "pch.h"

#include "GameFramework/Kits/Action/ActionAdventure/AdventureTargeting.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    const utf8* toString( AdventureTargetingState state )
    {
        switch ( state )
        {
            case AdventureTargetingState::Free:
                return "Free";
            case AdventureTargetingState::Parallel:
                return "Parallel";
            case AdventureTargetingState::Locked:
                return "Locked";
            case AdventureTargetingState::StrafeLeft:
                return "StrafeLeft";
            case AdventureTargetingState::StrafeRight:
                return "StrafeRight";
            case AdventureTargetingState::Backflip:
                return "Backflip";
            case AdventureTargetingState::SideHopLeft:
                return "SideHopLeft";
            case AdventureTargetingState::SideHopRight:
                return "SideHopRight";
        }
        return "Unknown";
    }

    AdventureTargeting::AdventureTargeting()
        : _settings{}
        , _selector{}
        , _targetPosition{}
        , _evadeRemaining{ 0.0f }
        , _invulnerableRemaining{ 0.0f }
        , _state{ AdventureTargetingState::Free }
        , _bHeld{ SW_FALSE }
    {
    }

    void AdventureTargeting::initialize( const AdventureTargetingSettings& settings )
    {
        _settings = settings;
        _selector.setSettings( settings._lockOn );
        release();
    }

    bool AdventureTargeting::press( const float3& eye, const float3& forward, const vector<LockOnCandidate>& listCandidate )
    {
        _bHeld = SW_TRUE;
        if ( _selector.pickBest( eye, forward, listCandidate ) == 0 )
        {
            _state = AdventureTargetingState::Parallel;
            return false;
        }
        refreshTargetPosition( listCandidate );
        _state = AdventureTargetingState::Locked;
        return true;
    }

    void AdventureTargeting::release()
    {
        _selector.release();
        _bHeld                 = SW_FALSE;
        _evadeRemaining        = 0.0f;
        _invulnerableRemaining = 0.0f;
        _state                 = AdventureTargetingState::Free;
    }

    uint64 AdventureTargeting::cycleTarget( const float3& eye, const float3& forward, const vector<LockOnCandidate>& listCandidate, int32 direction )
    {
        if ( _selector.hasTarget() == false )
            return 0;
        const uint64 target = _selector.cycle( eye, forward, listCandidate, direction );
        refreshTargetPosition( listCandidate );
        return target;
    }

    AdventureTargetingState AdventureTargeting::update( const float3& eye, const vector<LockOnCandidate>& listCandidate, const float2& moveInput, bool bJumpPressed,
                                                        float32 deltaTime )
    {
        _invulnerableRemaining = MathUtil::max( 0.0f, _invulnerableRemaining - deltaTime );
        if ( _bHeld == SW_FALSE )
        {
            _state = AdventureTargetingState::Free;
            return _state;
        }
        if ( _selector.hasTarget() && _selector.update( eye, listCandidate, deltaTime ) )
            refreshTargetPosition( listCandidate );
        // 회피 동작은 끝날 때까지 입력을 받지 않는다(공중에서 방향을 바꾸지 못한다).
        if ( _evadeRemaining > 0.0f )
        {
            _evadeRemaining -= deltaTime;
            if ( _evadeRemaining > 0.0f )
                return _state;
            _evadeRemaining = 0.0f;
        }
        const bool bLocked  = _selector.hasTarget();
        const bool bBack    = moveInput._y < -_settings._deadZone && MathUtil::abs( moveInput._y ) >= MathUtil::abs( moveInput._x );
        const bool bSide    = MathUtil::abs( moveInput._x ) > _settings._deadZone && MathUtil::abs( moveInput._x ) > MathUtil::abs( moveInput._y );
        const bool bToRight = moveInput._x > 0.0f;
        if ( bLocked && bJumpPressed && ( bBack || bSide ) )
        {
            if ( bBack )
            {
                _state          = AdventureTargetingState::Backflip;
                _evadeRemaining = _settings._backflipDuration;
            }
            else
            {
                _state          = bToRight ? AdventureTargetingState::SideHopRight : AdventureTargetingState::SideHopLeft;
                _evadeRemaining = _settings._sideHopDuration;
            }
            _invulnerableRemaining = _settings._evadeInvulnerable;
            return _state;
        }
        if ( bSide )
            _state = bToRight ? AdventureTargetingState::StrafeRight : AdventureTargetingState::StrafeLeft;
        else
            _state = bLocked ? AdventureTargetingState::Locked : AdventureTargetingState::Parallel;
        return _state;
    }

    float3 AdventureTargeting::computeMoveDirection( const float3& position, const float3& cameraForward, const float2& moveInput ) const
    {
        float3        forward = _selector.hasTarget() ? float3{ _targetPosition._x - position._x, 0.0f, _targetPosition._z - position._z }
                                                      : float3{ cameraForward._x, 0.0f, cameraForward._z };
        const float32 length  = forward.getLength();
        if ( length < 1.0e-5f )
            return float3{};
        forward *= 1.0f / length;
        const float3 right{ forward._z, 0.0f, -forward._x };
        return float3{ right._x * moveInput._x + forward._x * moveInput._y, 0.0f, right._z * moveInput._x + forward._z * moveInput._y };
    }

    void AdventureTargeting::refreshTargetPosition( const vector<LockOnCandidate>& listCandidate )
    {
        for ( const LockOnCandidate& candidate : listCandidate )
        {
            if ( candidate._id == _selector.getTarget() )
                _targetPosition = candidate._position;
        }
    }
} // namespace sw
