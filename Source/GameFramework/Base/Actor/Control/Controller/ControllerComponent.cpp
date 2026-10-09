#include "pch.h"

#include "GameFramework/Base/Actor/Control/Controller/ControllerComponent.h"

#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Actor/Control/ControlSystem.h"
#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"

namespace sw
{
    ControllerComponent::ControllerComponent()
        : _possessAtStart{}
        , _pawn{}
        , _controlYaw{ 0.0f }
        , _controlPitch{ 0.0f }
        , _inputPeer{ 0 }
        , _bSwitchingPawn{ SW_FALSE }
        , _bSpawnedForPawn{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void ControllerComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<ControllerComponent>( this );
        (void)ControlSystem::ensureFor( manager );
    }

    void ControllerComponent::onUnregister( GameObjectManager& manager )
    {
        // 시스템이 없으면 씬을 비우는 중이다 — 폰도 곧 지워지므로 끈만 버린다.
        if ( ControlSystem::find( manager ) != nullptr )
            unpossess();
        _pawn = ComponentHandle{};
        manager.getComponentRegistry().remove<ControllerComponent>( this );
        ControlSystem::releaseIfUnused( manager );
        Component::onUnregister( manager );
    }

    void ControllerComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _possessAtStart.isValid() == false || _pawn.isValid() )
            return;
        GameObject*        pOwner      = getOwner();
        GameObjectManager* pManager    = pOwner != nullptr ? pOwner->getManager() : nullptr;
        GameObject*        pPawnObject = pManager != nullptr ? pManager->resolveGameObject( _possessAtStart ) : nullptr;
        PawnComponent*     pPawn       = pPawnObject != nullptr ? pPawnObject->getComponent<PawnComponent>() : nullptr;
        if ( pPawn != nullptr )
            possess( *pPawn );
    }

    void ControllerComponent::possess( PawnComponent& pawn )
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || pawn.getOwner() == nullptr || _pawn == pawn.getHandle() )
            return;
        _bSwitchingPawn = SW_TRUE;
        unpossess();
        _bSwitchingPawn = SW_FALSE;
        // 남이 쥔 폰이면 그 조종자가 놓는다 — 폰 하나에 조종자 하나.
        if ( pawn.isPossessed() )
        {
            ControllerComponent* pOther = static_cast<ControllerComponent*>( pManager->resolveComponent( pawn.getController() ) );
            if ( pOther != nullptr )
                pOther->unpossess();
            pawn._controller = ComponentHandle{};
        }
        _pawn            = pawn.getHandle();
        pawn._controller = getHandle();
        pawn._inputPeer  = _inputPeer; // 폰의 연결은 빙의를 따라간다 — 탈것은 운전석 조종자의 연결
        _controlYaw      = pawn.getIntent()._controlYaw;
        _controlPitch    = pawn.getIntent()._controlPitch;
        onPossessed( pawn );
    }

    void ControllerComponent::requestPossess( const GameObjectHandle& pawnObject )
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        const ComponentHandle self = getHandle();
        pManager->executeOrDeferPostTick( [pManager, self, pawnObject]()
        {
            ControllerComponent* pController = static_cast<ControllerComponent*>( pManager->resolveComponent( self ) );
            GameObject*          pPawnObject = pManager->resolveGameObject( pawnObject );
            PawnComponent*       pPawn       = pPawnObject != nullptr ? pPawnObject->getComponent<PawnComponent>() : nullptr;
            if ( pController != nullptr && pPawn != nullptr )
                pController->possess( *pPawn );
        } );
    }

    void ControllerComponent::unpossess()
    {
        PawnComponent* pPawn = findPawn();
        _pawn                = ComponentHandle{};
        if ( pPawn == nullptr )
            return;
        if ( pPawn->_controller == getHandle() )
        {
            pPawn->_controller = ComponentHandle{};
            pPawn->_inputPeer  = 0;
            pPawn->clearMotion();
        }
        onUnpossessed( *pPawn );
    }

    PawnComponent* ControllerComponent::findPawn() const
    {
        if ( _pawn.isValid() == false )
            return nullptr;
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        return pManager != nullptr ? static_cast<PawnComponent*>( pManager->resolveComponent( _pawn ) ) : nullptr;
    }

    void ControllerComponent::setInputPeer( uint32 inputPeer )
    {
        _inputPeer           = inputPeer;
        PawnComponent* pPawn = findPawn();
        if ( pPawn != nullptr )
            pPawn->_inputPeer = inputPeer;
    }

    void ControllerComponent::setControlRotation( float32 yaw, float32 pitch )
    {
        _controlYaw   = yaw;
        _controlPitch = pitch;
    }
} // namespace sw
