#include "pch.h"

#include "GameFramework/Base/Gameplay/Interaction/GrabberComponent.h"

#include "Engine/Object/Component/Physics/RigidBody2DComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/World/World/GravityComponent.h"

namespace sw
{
    bool TransformGrabPhysics::attach( GameObject& holder, GameObject& target, const float3& localOffset )
    {
        SceneComponent* pHolderScene = holder.getPrimarySceneComponent();
        SceneComponent* pTargetScene = target.getPrimarySceneComponent();
        if ( pHolderScene == nullptr || pTargetScene == nullptr || pTargetScene->attachToComponent( pHolderScene, AttachRule::KeepWorld ) == false )
            return false;
        pTargetScene->setLocalPosition( localOffset );
        return true;
    }

    void TransformGrabPhysics::release( GameObject& holder, GameObject& target, const float3& velocity )
    {
        (void)holder;
        SceneComponent* pTargetScene = target.getPrimarySceneComponent();
        if ( pTargetScene != nullptr )
            pTargetScene->detachFromComponent( AttachRule::KeepWorld );
        // 강체가 없다 — 수직 속도만 중력 컴포넌트에 넘긴다(수평 속도는 강체 백엔드가 생기면 쓴다).
        GravityComponent* pGravity = target.getComponent<GravityComponent>();
        if ( pGravity != nullptr && velocity._y != 0.0f )
            pGravity->jump( velocity._y );
    }

    bool RigidBodyGrabPhysics::attach( GameObject& holder, GameObject& target, const float3& localOffset )
    {
        if ( _transform.attach( holder, target, localOffset ) == false )
            return false;
        // 강체는 손을 따르는 키네마틱 — 동적으로 남으면 중력 · 접촉이 계층과 싸운다.
        RigidBodyComponent* pBody3D = target.getComponent<RigidBodyComponent>();
        if ( pBody3D != nullptr )
            pBody3D->setBodyType( PhysicsBodyType::Kinematic );
        RigidBody2DComponent* pBody2D = target.getComponent<RigidBody2DComponent>();
        if ( pBody2D != nullptr )
            pBody2D->setBodyType( PhysicsBodyType::Kinematic );
        return true;
    }

    void RigidBodyGrabPhysics::release( GameObject& holder, GameObject& target, const float3& velocity )
    {
        RigidBodyComponent*   pBody3D = target.getComponent<RigidBodyComponent>();
        RigidBody2DComponent* pBody2D = target.getComponent<RigidBody2DComponent>();
        if ( pBody3D == nullptr && pBody2D == nullptr )
        {
            _transform.release( holder, target, velocity );
            return;
        }
        SceneComponent* pTargetScene = target.getPrimarySceneComponent();
        if ( pTargetScene != nullptr )
            pTargetScene->detachFromComponent( AttachRule::KeepWorld );
        if ( pBody3D != nullptr )
        {
            pBody3D->setBodyType( PhysicsBodyType::Dynamic );
            pBody3D->setLinearVelocity( velocity );
            pBody3D->wake();
        }
        if ( pBody2D != nullptr )
        {
            pBody2D->setBodyType( PhysicsBodyType::Dynamic );
            pBody2D->setLinearVelocity( float2{ velocity._x, velocity._y } );
            pBody2D->wake();
        }
    }

    GrabberComponent::GrabberComponent()
        : _holdOffset{ 0.0f, 1.0f, 0.6f }
        , _held{}
        , _fallback{}
    {
    }

    IGrabPhysics& GrabberComponent::getBackend()
    {
        IGrabPhysics* pService = game::getService<IGrabPhysics>();
        if ( pService == nullptr )
            return _fallback;
        return *pService;
    }

    bool GrabberComponent::grab( GameObject& target )
    {
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr || &target == pOwner || isHolding() )
            return false;
        if ( getBackend().attach( *pOwner, target, _holdOffset ) == false )
            return false;
        _held = target.getHandle();
        return true;
    }

    bool GrabberComponent::throwHeld( const float3& velocity )
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        GameObject*        pHeld    = pManager != nullptr ? pManager->resolveGameObject( _held ) : nullptr;
        _held                       = GameObjectHandle{};
        if ( pHeld == nullptr )
            return false;
        getBackend().release( *pOwner, *pHeld, velocity );
        return true;
    }
} // namespace sw
