#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Movement/TeleportTriggerComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Physics/Scene/ScenePhysics.h"

#include "GameFramework/Runtime/Character/Control/Pawn/CharacterMovementComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

namespace sw
{
    TeleportTriggerComponent::TeleportTriggerComponent()
        : TriggerComponent( TriggerAuthority::Server ) // 옮기기는 게임 상태를 바꾼다 — 서버(와 혼자 하는 게임)만 판정한다
        , _destination{}
        , _bMatchFacing{ true }
        , _listArrival{}
        , _listPendingArrival{}
        , _overlapEndSubscription{}
    {
        setOnce( false ); // 텔레포터는 들어올 때마다 옮긴다
    }

    void TeleportTriggerComponent::onBeginPlay()
    {
        TriggerComponent::onBeginPlay();
        const GameObject* const pOwner = getOwner();
        if ( pOwner != nullptr && _destination.isValid() == false )
            SW_LOG_ERROR_ID( "TeleportTrigger.NoDestination", "Teleport trigger '%#' has no destination - nothing is moved", pOwner->getName().c_str() );
        _listPendingArrival.clear();
        for ( const GameObjectHandle& arrived : _listArrival )
        {
            _listPendingArrival.push_back( PendingArrival{ arrived, nullopt } );
        }
    }

    void TeleportTriggerComponent::onRegister( GameObjectManager& manager )
    {
        TriggerComponent::onRegister( manager );
        _overlapEndSubscription = GameObjectEvents::listen<&TeleportTriggerComponent::handleOverlapEnd>( *this, &GameObjectEvents::_overlapEnd );
    }

    void TeleportTriggerComponent::onTick( TickContext& context )
    {
        TriggerComponent::onTick( context );
        if ( _listArrival.empty() )
            return;
        // 텔레포트는 다음 물리 프레임의 시작에 바디로 가고 그 걸음이 겹침 진입을 낸다(틱 뒤). 도착 뒤 첫 틱에서 적은 번호보다 걸음이 돌았는데 진입이 없으면 볼륨 밖에 도착했다.
        const uint64             stepCount = context.getManager().getSubsystem<ScenePhysics>().getStepCount();
        vector<GameObjectHandle> listOutside;
        for ( PendingArrival& pending : _listPendingArrival )
        {
            if ( pending._step.has_value() == false )
                pending._step = stepCount;
            else if ( ( stepCount > *pending._step || isMeasuredByDistance( pending._object ) ) && isInsideByDistance( pending._object ) == false )
                listOutside.push_back( pending._object );
        }
        for ( const GameObjectHandle& arrived : _listArrival )
        {
            const bool bPending = VectorUtil::containsIf( _listPendingArrival, [arrived]( const PendingArrival& pending )
            { return pending._object == arrived; } );
            if ( bPending == false && isMeasuredByDistance( arrived ) && isInsideByDistance( arrived ) == false )
                listOutside.push_back( arrived );
        }
        for ( const GameObjectHandle& outside : listOutside )
        {
            removeArrival( outside );
        }
    }

    void TeleportTriggerComponent::onOwnerActiveInHierarchyChanged()
    {
        TriggerComponent::onOwnerActiveInHierarchyChanged();
        _listArrival.clear();
        _listPendingArrival.clear();
    }

    void TeleportTriggerComponent::onPropertyChanged( hashed_string propertyName )
    {
        TriggerComponent::onPropertyChanged( propertyName );
        if ( propertyName != getActivePropertyName() )
            return;
        _listArrival.clear();
        _listPendingArrival.clear();
    }

    void TeleportTriggerComponent::handleOverlapEnd( const OverlapInfo& overlap )
    {
        if ( overlap._bOtherTrigger == SW_FALSE && overlap._pOther != nullptr )
            removeArrival( overlap._pOther->getHandle() );
    }

    void TeleportTriggerComponent::removeArrival( GameObjectHandle object )
    {
        (void)VectorUtil::removeSingleSwap( _listArrival, object );
        VectorUtil::removeIf( _listPendingArrival, [object]( const PendingArrival& pending )
        { return pending._object == object; } );
    }

    bool TeleportTriggerComponent::onVolumeActivated( const GameObject& activator )
    {
        const GameObjectHandle handle = activator.getHandle();
        if ( VectorUtil::contains( _listArrival, handle ) )
        {
            VectorUtil::removeIf( _listPendingArrival, [handle]( const PendingArrival& pending )
            { return pending._object == handle; } );
            return false; // 이 볼륨으로 도착해 아직 나가지 않았다 — 되돌려 보내지 않는다
        }
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
        if ( isActive() == false )
            return; // 꺼진 볼륨은 진입을 내지 않는다 — 다시 켜졌을 때 안에 있으면 들어온 것이다(트리거 계약)
        const GameObjectManager* const pManager = getOwner() != nullptr ? getOwner()->getManager() : nullptr;
        // 도착한 채 사라진 것의 표시를 남기지 않는다.
        VectorUtil::removeIf( _listArrival, [pManager]( const GameObjectHandle& handle )
        { return pManager == nullptr || pManager->resolveGameObject( handle ) == nullptr; } );
        if ( VectorUtil::addUnique( _listArrival, arrived ) )
            _listPendingArrival.push_back( PendingArrival{ arrived, nullopt } );
    }
} // namespace sw
