#include "pch.h"

#include "Engine/Object/Component/Navigation/NavMeshAgentComponent.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/SceneNavigation.h"

namespace sw
{
    bool NavMeshAgentMover::moveTo( const float3& destination )
    {
        _agent.setDestination( destination );
        return _agent.getMoveStatus() != NavMoveStatus::Failed;
    }

    void NavMeshAgentMover::stopMoving()
    {
        _agent.stop();
    }

    NavMoveStatus NavMeshAgentMover::getMoveStatus() const
    {
        return _agent.getMoveStatus();
    }

    float3 NavMeshAgentMover::getMoveVelocity() const
    {
        return _agent.getVelocity();
    }

    float3 NavMeshAgentMover::getMovePosition() const
    {
        return _agent.getAgentPosition();
    }
} // namespace sw

namespace sw
{
    NavMeshAgentComponent::NavMeshAgentComponent()
        : _agentType{}
        , _maxSpeed{ 3.5f }
        , _maxAcceleration{ 10.0f }
        , _stoppingDistance{ 0.3f }
        , _radius{ 0.0f }
        , _separationWeight{ 2.0f }
        , _avoidanceQuality{ NavAvoidanceQuality::Medium }
        , _turnRate{ 8.0f }
        , _driveMode{ NavAgentDriveMode::Transform }
        , _bUpdateRotation{ true }
        , _mover{ *this }
        , _pSceneNavigation{ nullptr }
        , _destination{}
        , _pendingWarp{}
        , _velocity{}
        , _desiredVelocity{}
        , _agentPosition{}
        , _nextCorner{}
        , _writtenPosition{}
        , _crowdAgentId{ NavigationConstant::kInvalidAgentId }
        , _navIndex{ SceneNavigation::kNotRegistered }
        , _crowdIndex{ SceneNavigation::kNotRegistered }
        , _moveStatus{ NavMoveStatus::Idle }
        , _bHasDestination{ SW_FALSE }
        , _bDestinationDirty{ SW_FALSE }
        , _bWarpPending{ SW_FALSE }
        , _bOnNavMesh{ SW_FALSE }
        , _bHasWritten{ SW_FALSE }
        , _bParamsDirty{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void NavMeshAgentComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        _pSceneNavigation = &manager.getSceneNavigation();
        _pSceneNavigation->registerAgent( this );
    }

    void NavMeshAgentComponent::onUnregister( GameObjectManager& manager )
    {
        if ( _pSceneNavigation != nullptr )
            _pSceneNavigation->unregisterAgent( this );
        _pSceneNavigation = nullptr;
        Component::onUnregister( manager );
    }

    void NavMeshAgentComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene != nullptr )
            _agentPosition = pScene->getWorldPosition();
    }

    void NavMeshAgentComponent::onEndPlay()
    {
        if ( _pSceneNavigation != nullptr )
            _pSceneNavigation->releaseAgent( this );
        _moveStatus      = NavMoveStatus::Idle;
        _bHasDestination = SW_FALSE;
        Component::onEndPlay();
    }

    void NavMeshAgentComponent::setDestination( const float3& destination )
    {
        _destination       = destination;
        _bHasDestination   = SW_TRUE;
        _bDestinationDirty = SW_TRUE;
        if ( _moveStatus != NavMoveStatus::Moving )
            _moveStatus = NavMoveStatus::Moving;
    }

    void NavMeshAgentComponent::stop()
    {
        _bHasDestination   = SW_FALSE;
        _bDestinationDirty = SW_TRUE;
        _moveStatus        = NavMoveStatus::Idle;
    }

    void NavMeshAgentComponent::warp( const float3& position )
    {
        _pendingWarp  = position;
        _bWarpPending = SW_TRUE;
    }

    void NavMeshAgentComponent::setMaxSpeed( float32 maxSpeed )
    {
        _maxSpeed     = maxSpeed;
        _bParamsDirty = SW_TRUE;
    }

    void NavMeshAgentComponent::setRadius( float32 radius )
    {
        _radius       = radius;
        _bParamsDirty = SW_TRUE;
    }

    NavCrowdAgentParams NavMeshAgentComponent::makeCrowdParams() const
    {
        NavCrowdAgentParams params;
        params._radius                = _radius;
        params._maxSpeed              = _maxSpeed;
        params._maxAcceleration       = _maxAcceleration;
        params._separationWeight      = _separationWeight;
        params._avoidanceQuality      = _avoidanceQuality;
        params._collisionQueryRange   = _radius * 12.0f;
        params._pathOptimizationRange = _radius * 30.0f;
        return params;
    }
} // namespace sw
