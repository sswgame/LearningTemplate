#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/HorseFollowAIController.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

namespace sw
{
    namespace
    {
        struct HorseFollowAIControllerInternal
        {
            static constexpr utf8 kCalledKey[] = "Called"; ///< 휘파람을 들었다(트리가 닿으면 지운다)
        };
    } // namespace
} // namespace sw

namespace sw
{
    HorseFollowAIController::HorseFollowAIController()
        : _ownerObject{}
    {
        requireBehaviorTree();
    }

    void HorseFollowAIController::whistle()
    {
        getBlackboard().setBool( HorseFollowAIControllerInternal::kCalledKey, true );
    }

    bool HorseFollowAIController::isCalled() const
    {
        return getBlackboard().isSet( HorseFollowAIControllerInternal::kCalledKey );
    }

    void HorseFollowAIController::updateBlackboard( const ControlFrameContext& context, const PawnComponent& pawn )
    {
        (void)context;
        Blackboard&              blackboard = getBlackboard();
        const GameObject*        pHorse     = pawn.getOwner();
        const GameObjectManager* pManager   = pHorse != nullptr ? pHorse->getManager() : nullptr;
        const GameObject*        pOwner     = pManager != nullptr && _ownerObject.isValid() ? pManager->resolveGameObject( _ownerObject ) : nullptr;
        const SceneComponent*    pHorseRoot = pHorse != nullptr ? pHorse->getPrimarySceneComponent() : nullptr;
        const SceneComponent*    pOwnerRoot = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pHorseRoot == nullptr || pOwnerRoot == nullptr )
        {
            blackboard.clearValue( "FollowPosition" );
            blackboard.clearValue( "OwnerDistance" );
            return;
        }
        const float3 ownerPosition = pOwnerRoot->getWorldPosition();
        const float3 horsePosition = pHorseRoot->getWorldPosition();
        // 말은 땅을 걷는다 — 목적지 높이는 말의 것이고, 거리도 수평으로 잰다.
        blackboard.setVector( "FollowPosition", float3{ ownerPosition._x, horsePosition._y, ownerPosition._z } );
        blackboard.setFloat( "OwnerDistance", float3::getHorizontalDistance( horsePosition, ownerPosition ) );
    }

    void HorseFollowAIController::onRegister( GameObjectManager& manager )
    {
        AIControllerComponent::onRegister( manager );
        manager.getComponentRegistry().add<HorseFollowAIController>( this );
    }

    void HorseFollowAIController::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<HorseFollowAIController>( this );
        AIControllerComponent::onUnregister( manager );
    }
} // namespace sw
