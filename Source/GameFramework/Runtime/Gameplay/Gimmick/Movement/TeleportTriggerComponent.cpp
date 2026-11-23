#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Movement/TeleportTriggerComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Control/Pawn/CharacterMovementComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

namespace sw
{
    TeleportTriggerComponent::TeleportTriggerComponent()
        : TriggerComponent( TriggerAuthority::Server ) // 옮기기는 게임 상태를 바꾼다 — 서버(와 혼자 하는 게임)만 판정한다
        , _destination{}
        , _bMatchFacing{ true }
        , _listArrival{}
    {
        setOnce( false ); // 텔레포터는 들어올 때마다 옮긴다
    }

    void TeleportTriggerComponent::onBeginPlay()
    {
        TriggerComponent::onBeginPlay();
        const GameObject* const pOwner = getOwner();
        if ( pOwner != nullptr && _destination.isValid() == false )
            SW_LOG_ERROR_ID( "TeleportTrigger.NoDestination", "Teleport trigger '%#' has no destination - nothing is moved", pOwner->getName().c_str() );
    }

    bool TeleportTriggerComponent::onVolumeActivated( const GameObject& activator )
    {
        const GameObjectHandle handle = activator.getHandle();
        if ( VectorUtil::removeSingleSwap( _listArrival, handle ) )
            return false; // 이 볼륨으로 막 도착했다 — 되돌려 보내지 않는다
        GameObjectManager* const    pManager     = getOwner() != nullptr ? getOwner()->getMutableManager() : nullptr;
        GameObject* const           pTarget      = pManager != nullptr ? pManager->resolveGameObject( handle ) : nullptr;
        GameObject* const           pDestination = pManager != nullptr ? pManager->resolveGameObject( _destination ) : nullptr;
        SceneComponent* const       pRoot        = pTarget != nullptr ? pTarget->getPrimarySceneComponent() : nullptr;
        const SceneComponent* const pPlace       = pDestination != nullptr ? pDestination->getPrimarySceneComponent() : nullptr;
        if ( pRoot == nullptr || pPlace == nullptr )
        {
            SW_LOG_WARNING_ID( "TeleportTrigger.CannotMove", "Teleport trigger '%#' could not move '%#' (no destination or no transform)",
                               getOwner() != nullptr ? getOwner()->getName().c_str() : "", activator.getName().c_str() );
            return false;
        }
        pRoot->teleportTo( pPlace->getWorldPosition(), TeleportType::ResetPhysics );
        if ( _bMatchFacing )
        {
            const float3  forward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, pPlace->getWorldMatrix() );
            const float32 yaw     = MathUtil::atan2( forward._x, forward._z );
            if ( CharacterMovementComponent* const pMovement = pTarget->getComponent<CharacterMovementComponent>() )
                pMovement->setFacingYaw( yaw );
            if ( PawnComponent* const pPawn = pTarget->getComponent<PawnComponent>() )
                pPawn->requestControlRotation( yaw, pPawn->getIntent()._controlPitch );
        }
        if ( TeleportTriggerComponent* const pPair = pDestination->getComponent<TeleportTriggerComponent>() )
            pPair->expectArrival( handle );
        return true;
    }

    void TeleportTriggerComponent::expectArrival( GameObjectHandle arrived )
    {
        const GameObjectManager* const pManager = getOwner() != nullptr ? getOwner()->getManager() : nullptr;
        // 도착한 채 사라진 것의 표시를 남기지 않는다.
        VectorUtil::removeIf( _listArrival, [pManager]( const GameObjectHandle& handle )
        { return pManager == nullptr || pManager->resolveGameObject( handle ) == nullptr; } );
        (void)VectorUtil::addUnique( _listArrival, arrived );
    }
} // namespace sw
