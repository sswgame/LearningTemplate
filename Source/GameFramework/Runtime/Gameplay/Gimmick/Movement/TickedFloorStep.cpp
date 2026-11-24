#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Movement/TickedFloorStep.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/Scene/ScenePhysics.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Movement/InterpToMovementComponent.h"

namespace sw
{
    void TickedFloorOffset::update( uint32 serverTick, uint32 inputTick, int32& inoutOffset, uint8& inoutbKnown )
    {
        const int64 wanted = static_cast<int64>( serverTick ) - static_cast<int64>( inputTick );
        const int64 drift  = wanted - static_cast<int64>( inoutOffset );
        if ( inoutbKnown == SW_TRUE && -kMaxDrift <= drift && drift <= kMaxDrift )
            return; // 몰려 온 입력은 저마다 한 틱씩 간다 — 바닥이 명령 사이에서 멈추거나 뛰지 않는다
        inoutOffset = static_cast<int32>( wanted );
        inoutbKnown = SW_TRUE;
    }

    optional<int64> TickedFloorOffset::findTick( int32 offset, uint8 bKnown, uint32 inputTick )
    {
        if ( bKnown == SW_FALSE )
            return nullopt;
        return static_cast<int64>( inputTick ) + static_cast<int64>( offset );
    }

    TickedFloorStep::TickedFloorStep( GameObjectManager& manager, float32 stepSeconds )
        : _listFloor{}
        , _pScene{ manager.getSubsystem<ScenePhysics>().findScene3D() }
        , _stepSeconds{ static_cast<float64>( stepSeconds ) }
    {
        if ( _pScene == nullptr )
            return;
        for ( const InterpToMovementComponent* pFloor : manager.getComponentRegistry().getAll<InterpToMovementComponent>() )
        {
            const GameObject*         pOwner = pFloor != nullptr ? pFloor->getOwner() : nullptr;
            const RigidBodyComponent* pBody  = pOwner != nullptr ? pOwner->getComponent<RigidBodyComponent>() : nullptr;
            Floor                     floor;
            floor._pFloor = pFloor;
            floor._body   = pBody != nullptr ? pBody->getBodyHandle() : PhysicsBodyHandle{};
            // 키네마틱이 아닌 바닥은 물리가 움직인다 — 이 장치가 놓지 않는다.
            const bool bHeld = floor._body.isValid() && _pScene->getBodyType( floor._body ) == PhysicsBodyType::Kinematic &&
                               _pScene->getBodyTransform( floor._body, floor._savedPosition, floor._savedRotation );
            if ( bHeld == false )
                continue;
            floor._savedLinearVelocity  = _pScene->getLinearVelocity( floor._body );
            floor._savedAngularVelocity = _pScene->getAngularVelocity( floor._body );
            _listFloor.push_back( floor );
        }
    }

    TickedFloorStep::~TickedFloorStep()
    {
        for ( const Floor& floor : _listFloor )
        {
            _pScene->setBodyTransform( floor._body, floor._savedPosition, floor._savedRotation );
            _pScene->setLinearVelocity( floor._body, floor._savedLinearVelocity );
            _pScene->setAngularVelocity( floor._body, floor._savedAngularVelocity );
        }
    }

    void TickedFloorStep::place( const Floor& floor, int64 tick ) const
    {
        float3     position;
        quaternion rotation;
        if ( floor._pFloor->computePose( static_cast<float64>( tick ) * _stepSeconds, position, rotation ) )
            _pScene->setBodyTransform( floor._body, position, rotation );
    }

    void TickedFloorStep::beginTick( int64 tick )
    {
        for ( const Floor& floor : _listFloor )
        {
            place( floor, tick - 1 );
            float3     fromPosition;
            float3     toPosition;
            quaternion rotation;
            const bool bPosed = floor._pFloor->computePose( static_cast<float64>( tick - 1 ) * _stepSeconds, fromPosition, rotation ) &&
                                floor._pFloor->computePose( static_cast<float64>( tick ) * _stepSeconds, toPosition, rotation );
            if ( bPosed == false || _stepSeconds <= 0.0 )
                continue;
            _pScene->setLinearVelocity( floor._body, ( toPosition - fromPosition ) * static_cast<float32>( 1.0 / _stepSeconds ) );
            _pScene->setAngularVelocity( floor._body, float3{ 0.0f, floor._pFloor->getTurnRate().radians(), 0.0f } );
        }
    }

    void TickedFloorStep::endTick( int64 tick )
    {
        for ( const Floor& floor : _listFloor )
        {
            place( floor, tick );
        }
    }
} // namespace sw
