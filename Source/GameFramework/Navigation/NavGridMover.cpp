#include "pch.h"

#include "GameFramework/Navigation/NavGridMover.h"

#include "GameFramework/Navigation/NavAgent.h"

namespace sw
{
    NavGridMover::NavGridMover( NavAgent& agent, const NavGrid& grid, GridPathfinder& pathfinder )
        : _agent{ agent }
        , _grid{ grid }
        , _pathfinder{ pathfinder }
        , _bFailed{ false }
    {
    }

    bool NavGridMover::moveTo( const float3& destination )
    {
        _bFailed = _agent.moveTo( _grid, _pathfinder, destination ) == false;
        return _bFailed == false;
    }

    void NavGridMover::stopMoving()
    {
        _agent.stop();
        _bFailed = false;
    }

    NavMoveStatus NavGridMover::getMoveStatus() const
    {
        if ( _bFailed )
            return NavMoveStatus::Failed;
        switch ( _agent.getState() )
        {
            case NavAgentState::Idle:
                return NavMoveStatus::Idle;
            case NavAgentState::FollowingPath:
            case NavAgentState::FollowingFlow:
                return NavMoveStatus::Moving;
            case NavAgentState::Arrived:
                return NavMoveStatus::Arrived;
            case NavAgentState::Stuck:
                return NavMoveStatus::Failed;
        }
        return NavMoveStatus::Idle;
    }

    float3 NavGridMover::getMoveVelocity() const
    {
        return _agent.getVelocity();
    }

    float3 NavGridMover::getMovePosition() const
    {
        return _agent.getPosition();
    }
} // namespace sw
