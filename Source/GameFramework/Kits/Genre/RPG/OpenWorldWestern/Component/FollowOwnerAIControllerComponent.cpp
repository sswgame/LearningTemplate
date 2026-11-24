#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/FollowOwnerAIControllerComponent.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

namespace sw
{
    namespace
    {
        struct FollowOwnerAIControllerComponentInternal
        {
            static constexpr utf8 kCalledKey[] = "Called"; ///< 휘파람을 들었다(트리가 닿으면 지운다)
        };
    } // namespace
} // namespace sw

namespace sw
{
    FollowOwnerAIControllerComponent::FollowOwnerAIControllerComponent()
        : _ownerObject{}
    {
        requireBehaviorTree();
    }

    void FollowOwnerAIControllerComponent::recall()
    {
        getBlackboard().setBool( FollowOwnerAIControllerComponentInternal::kCalledKey, true );
    }

    bool FollowOwnerAIControllerComponent::isCalled() const
    {
        return getBlackboard().isSet( FollowOwnerAIControllerComponentInternal::kCalledKey );
    }

    void FollowOwnerAIControllerComponent::updateBlackboard( const ControlFrameContext& context, const PawnComponent& pawn )
    {
        (void)context;
        Blackboard&              blackboard  = getBlackboard();
        const GameObject*        pPawnObject = pawn.getOwner();
        const GameObjectManager* pManager    = pPawnObject != nullptr ? pPawnObject->getManager() : nullptr;
        const GameObject*        pOwner      = pManager != nullptr && _ownerObject.isValid() ? pManager->resolveGameObject( _ownerObject ) : nullptr;
        const SceneComponent*    pPawnRoot   = pPawnObject != nullptr ? pPawnObject->getPrimarySceneComponent() : nullptr;
        const SceneComponent*    pOwnerRoot  = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pPawnRoot == nullptr || pOwnerRoot == nullptr )
        {
            blackboard.clearValue( "FollowPosition" );
            blackboard.clearValue( "OwnerDistance" );
            return;
        }
        const float3 ownerPosition = pOwnerRoot->getWorldPosition();
        const float3 pawnPosition  = pPawnRoot->getWorldPosition();
        // 폰은 땅을 걷는다 — 목적지 높이는 폰의 것이고, 거리도 수평으로 잰다.
        blackboard.setVector( "FollowPosition", float3{ ownerPosition._x, pawnPosition._y, ownerPosition._z } );
        blackboard.setFloat( "OwnerDistance", float3::getHorizontalDistance( pawnPosition, ownerPosition ) );
    }
} // namespace sw
