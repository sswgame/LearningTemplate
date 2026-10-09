#include "pch.h"

#include "GameFramework/Base/Actor/Navigation/NavAgent.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Actor/Navigation/FlowField.h"
#include "GameFramework/Base/Actor/Navigation/GridPathfinder.h"
#include "GameFramework/Base/Actor/Navigation/NavGrid.h"

namespace sw
{
    namespace
    {
        struct NavAgentInternal
        {
            static float3 flatten( const float3& value ) { return float3{ value._x, 0.0f, value._z }; }

            static float32 getFlatDistance( const float3& lhs, const float3& rhs ) { return flatten( lhs - rhs ).getLength(); }

            static float3 clampLength( const float3& value, float32 maxLength )
            {
                const float32 length = value.getLength();
                return length > maxLength && length > 1.0e-6f ? value * ( maxLength / length ) : value;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // Steering
    // ------------------------------------------------------------------------------
    float3 Steering::seek( const float3& position, const float3& target, float32 maxSpeed )
    {
        const float3  toTarget = NavAgentInternal::flatten( target - position );
        const float32 distance = toTarget.getLength();
        return distance > 1.0e-6f ? toTarget * ( maxSpeed / distance ) : float3{ 0.0f, 0.0f, 0.0f };
    }

    float3 Steering::arrive( const float3& position, const float3& target, float32 maxSpeed, float32 slowDistance )
    {
        const float3  toTarget = NavAgentInternal::flatten( target - position );
        const float32 distance = toTarget.getLength();
        if ( distance < 1.0e-6f )
            return float3{ 0.0f, 0.0f, 0.0f };
        const float32 speed = slowDistance > 0.0f ? maxSpeed * MathUtil::min( 1.0f, distance / slowDistance ) : maxSpeed;
        return toTarget * ( speed / distance );
    }

    float3 Steering::separate( const float3& position, float32 radius, const vector<float3>& listNeighbor, float32 neighborRadius )
    {
        float3        push{ 0.0f, 0.0f, 0.0f };
        const float32 reach = radius + neighborRadius;
        for ( const float3& neighbor : listNeighbor )
        {
            const float3  away     = NavAgentInternal::flatten( position - neighbor );
            const float32 distance = away.getLength();
            if ( distance < 1.0e-4f || distance >= reach )
                continue;
            push = push + away * ( ( reach - distance ) / ( reach * distance ) ); // 겹칠수록 1 에 가까운 세기 × 방향
        }
        return push;
    }

    float3 Steering::accelerate( const float3& velocity, const float3& desiredVelocity, float32 maxAcceleration, float32 deltaTime )
    {
        const float3 change = NavAgentInternal::clampLength( desiredVelocity - velocity, maxAcceleration * deltaTime );
        return velocity + change;
    }

    // ------------------------------------------------------------------------------
    // NavAgent
    // ------------------------------------------------------------------------------
    NavAgent::NavAgent()
        : _settings{}
        , _listPathPoint{}
        , _pFlowField{ nullptr }
        , _position{ 0.0f, 0.0f, 0.0f }
        , _velocity{ 0.0f, 0.0f, 0.0f }
        , _target{ 0.0f, 0.0f, 0.0f }
        , _waypointIndex{ 0 }
        , _stuckTimer{ 0.0f }
        , _state{ NavAgentState::Idle }
    {
    }

    void NavAgent::setPosition( const float3& position )
    {
        _position = position;
        stop();
    }

    bool NavAgent::moveTo( const NavGrid& grid, GridPathfinder& pathfinder, const float3& target )
    {
        GridPathQuery query;
        query._start = grid.computeCell( _position );
        query._goal  = grid.computeCell( target );
        vector<int2>         listCell;
        const GridPathResult result = pathfinder.findPath( grid, query, listCell );
        if ( result != GridPathResult::Found && result != GridPathResult::Partial )
            return false;
        vector<float3> listPoint;
        GridPathfinder::makeWorldPath( grid, listCell, listPoint );
        // 첫 점은 지금 칸 가운데 — 이미 그 칸 안이니 건너뛰고, 끝점은 칸 가운데가 아니라 실제 목표(닿은 경우).
        if ( listPoint.empty() == false )
            listPoint.erase( listPoint.begin() );
        if ( result == GridPathResult::Found )
        {
            if ( listPoint.empty() )
                listPoint.push_back( target );
            else
                listPoint.back() = float3{ target._x, listPoint.back()._y, target._z };
        }
        if ( listPoint.empty() )
            listPoint.push_back( _position );
        followPath( listPoint );
        return true;
    }

    void NavAgent::followPath( const vector<float3>& listPoint )
    {
        _listPathPoint = listPoint;
        _pFlowField    = nullptr;
        _waypointIndex = 0;
        _stuckTimer    = 0.0f;
        _target        = listPoint.empty() ? _position : listPoint.back();
        _state         = listPoint.empty() ? NavAgentState::Idle : NavAgentState::FollowingPath;
    }

    void NavAgent::followFlowField( const FlowField* pFlowField, const float3& target )
    {
        _listPathPoint.clear();
        _pFlowField    = pFlowField;
        _waypointIndex = 0;
        _stuckTimer    = 0.0f;
        _target        = target;
        _state         = pFlowField != nullptr ? NavAgentState::FollowingFlow : NavAgentState::Idle;
    }

    void NavAgent::stop()
    {
        _listPathPoint.clear();
        _pFlowField = nullptr;
        _velocity   = float3{ 0.0f, 0.0f, 0.0f };
        _stuckTimer = 0.0f;
        _state      = NavAgentState::Idle;
    }

    float3 NavAgent::computeDesiredVelocity( const NavGrid& grid )
    {
        if ( _state == NavAgentState::FollowingPath )
        {
            // 중간 경로점은 가까이 가면 넘긴다. 마지막 점은 도착 감속.
            while ( _waypointIndex + 1 < _listPathPoint.size() &&
                    NavAgentInternal::getFlatDistance( _position, _listPathPoint[_waypointIndex] ) < _settings._waypointDistance )
            {
                ++_waypointIndex;
            }
            const float3& waypoint = _listPathPoint[_waypointIndex];
            const bool    bLast    = _waypointIndex + 1 == _listPathPoint.size();
            return bLast ? Steering::arrive( _position, waypoint, _settings._maxSpeed, _settings._slowDistance )
                         : Steering::seek( _position, waypoint, _settings._maxSpeed );
        }
        if ( _state == NavAgentState::FollowingFlow && _pFlowField != nullptr )
        {
            // 목표 칸 근처면 흐름장 대신 목표로 바로 — 흐름장은 목적지 칸 안에서 방향이 없다.
            if ( NavAgentInternal::getFlatDistance( _position, _target ) < _settings._slowDistance + grid.getCellSize() )
                return Steering::arrive( _position, _target, _settings._maxSpeed, _settings._slowDistance );
            const float3 direction = _pFlowField->sampleDirection( grid, _position );
            if ( direction.getLengthSquared() < 1.0e-6f )
                return Steering::arrive( _position, _target, _settings._maxSpeed, _settings._slowDistance );
            return direction * _settings._maxSpeed;
        }
        return float3{ 0.0f, 0.0f, 0.0f };
    }

    void NavAgent::moveWithCollision( const NavGrid& grid, const float3& delta )
    {
        const float3 full = _position + delta;
        if ( grid.isWalkable( grid.computeCell( full ) ) )
        {
            _position = full;
            return;
        }
        // 벽 따라 미끄러지기 — 한 축씩.
        const float3 alongX{ _position._x + delta._x, _position._y, _position._z };
        if ( grid.isWalkable( grid.computeCell( alongX ) ) )
        {
            _position    = alongX;
            _velocity._z = 0.0f;
            return;
        }
        const float3 alongZ{ _position._x, _position._y, _position._z + delta._z };
        if ( grid.isWalkable( grid.computeCell( alongZ ) ) )
        {
            _position    = alongZ;
            _velocity._x = 0.0f;
            return;
        }
        _velocity = float3{ 0.0f, 0.0f, 0.0f };
    }

    void NavAgent::update( const NavGrid& grid, const vector<float3>& listNeighbor, float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        float3 desired = computeDesiredVelocity( grid );
        if ( _settings._separationWeight > 0.0f && listNeighbor.empty() == false )
            desired = desired + Steering::separate( _position, _settings._radius, listNeighbor, _settings._radius ) * ( _settings._separationWeight * _settings._maxSpeed );
        desired   = NavAgentInternal::clampLength( NavAgentInternal::flatten( desired ), _settings._maxSpeed );
        _velocity = Steering::accelerate( _velocity, desired, _settings._maxAcceleration, deltaTime );

        const float3 before = _position;
        moveWithCollision( grid, _velocity * deltaTime );

        if ( isMoving() == false )
            return;
        if ( NavAgentInternal::getFlatDistance( _position, _target ) <= _settings._arriveDistance &&
             ( _state == NavAgentState::FollowingFlow || _waypointIndex + 1 >= _listPathPoint.size() ) )
        {
            _state    = NavAgentState::Arrived;
            _velocity = float3{ 0.0f, 0.0f, 0.0f };
            return;
        }
        // 끼임 — 바라는 만큼의 1/10 도 못 움직이는 틱이 쌓이면.
        const float32 moved = NavAgentInternal::getFlatDistance( before, _position );
        _stuckTimer         = moved < _settings._maxSpeed * deltaTime * 0.1f ? _stuckTimer + deltaTime : 0.0f;
        if ( _stuckTimer >= _settings._stuckTime )
        {
            _state    = NavAgentState::Stuck;
            _velocity = float3{ 0.0f, 0.0f, 0.0f };
        }
    }
} // namespace sw
