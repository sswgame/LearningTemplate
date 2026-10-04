#include "pch.h"

#include "GameFramework/Interaction/GrabberComponent.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Components/GravityComponent.h"
#include "GameFramework/Framework/GameService.h"

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
