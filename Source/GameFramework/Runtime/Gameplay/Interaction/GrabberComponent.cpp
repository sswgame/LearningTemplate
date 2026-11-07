#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Interaction/GrabberComponent.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Component/RigidBody2DComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    bool TransformGrabPhysics::attach( GameObject& holder, GameObject& target, const float3& localOffset )
    {
        SceneComponent* pHolderScene  = holder.getPrimarySceneComponent();
        SceneComponent* pTargetScene  = target.getPrimarySceneComponent();
        const bool      bCannotAttach = pHolderScene == nullptr || pTargetScene == nullptr || pTargetScene->attachToComponent( pHolderScene, AttachRule::KeepWorld ) == false;
        if ( bCannotAttach )
            return false;
        pTargetScene->setLocalPosition( localOffset );
        return true;
    }

    void TransformGrabPhysics::release( GameObject& holder, GameObject& target, const float3& velocity )
    {
        (void)holder;
        (void)velocity; // 강체가 없으면 속도를 받을 곳이 없다 - 놓은 자리에 남는다. 던지려면 대상에 강체를 둔다(RigidBodyGrabPhysics)
        SceneComponent* pTargetScene = target.getPrimarySceneComponent();
        if ( pTargetScene != nullptr )
            pTargetScene->detachFromComponent( AttachRule::KeepWorld );
    }

    bool RigidBodyGrabPhysics::attach( GameObject& holder, GameObject& target, const float3& localOffset )
    {
        if ( _transform.attach( holder, target, localOffset ) == false )
            return false;
        // 강체는 손을 따르는 키네마틱으로 둔다. 동적으로 남겨 두면 중력과 접촉이 계층 구조와 충돌한다.
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
        IGrabPhysics* pService = game::findSubsystem<IGrabPhysics>();
        if ( pService == nullptr )
            return _fallback;
        return *pService;
    }

    bool GrabberComponent::grab( GameObject& target )
    {
        GameObject* pOwner      = getOwner();
        const bool  bCannotGrab = pOwner == nullptr || &target == pOwner || isHolding();
        if ( bCannotGrab )
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
