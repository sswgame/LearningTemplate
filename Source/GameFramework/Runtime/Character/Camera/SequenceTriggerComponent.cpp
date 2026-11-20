#include "pch.h"

#include "GameFramework/Runtime/Character/Camera/SequenceTriggerComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"

#include "GameFramework/Runtime/Character/Control/Controller/PlayerStateComponent.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"

namespace sw
{
    SequenceTriggerComponent::SequenceTriggerComponent()
        : _sequence{}
        , _audience{ SequenceTriggerAudience::Everyone }
        , _lastActivator{}
        , _activationCount{ 0 }
    {
    }

    void SequenceTriggerComponent::onRegister( GameObjectManager& manager )
    {
        ActivatorVolumeComponent::onRegister( manager );
        manager.getComponentRegistry().add<SequenceTriggerComponent>( this );
    }

    void SequenceTriggerComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<SequenceTriggerComponent>( this );
        ActivatorVolumeComponent::onUnregister( manager );
    }

    bool SequenceTriggerComponent::onVolumeActivated( const GameObject& activator )
    {
        const GameObject*        pOwner    = getOwner();
        GameObjectManager*       pManager  = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        GameObject*              pSequence = pManager != nullptr ? pManager->resolveGameObject( _sequence ) : nullptr;
        SequencePlayerComponent* pPlayer   = pSequence != nullptr ? pSequence->getComponent<SequencePlayerComponent>() : nullptr;
        if ( pPlayer == nullptr )
        {
            SW_LOG_ERROR_ID( "SequenceTrigger.SequenceObjectNoPlayer", "Sequence trigger '%#' points at %# - nothing plays", pOwner != nullptr ? pOwner->getName().c_str() : "?",
                             pSequence == nullptr ? "no object" : "an object without a SequencePlayerComponent" );
            return false;
        }
        // 재생 중 다시 들어오면 처음부터 다시 틀지 않는다(언리얼 `UMovieSceneSequencePlayer::Play` 는 재생 중이면 그대로다) — 발동으로 치지 않는다.
        if ( pPlayer->isPlaying() )
            return false;
        pPlayer->play();
        // 데이터 결함(범위 제한 칸 없음 · 속도 0)이면 play 가 오류를 남기고 틀지 않는다 — 발동으로 치지 않아 한 번만 하는 볼륨이 잠기지 않는다.
        if ( pPlayer->isPlaying() == false )
            return false;
        _lastActivator = activator.getHandle();
        ++_activationCount;
        return true;
    }

    bool SequenceTriggerComponent::hasActivationAuthority() const
    {
        // 판정은 서버만 한다 — 클라이언트 월드도 같은 트리거 계약으로 진입을 내지만 틀지 않는다(서버가 보낸 재생을 넷 키트가 튼다).
        const NetLinkValue* pLink = SceneNetLink::findBoundFor( *this );
        return pLink == nullptr || pLink->_role != NetLinkRole::Client;
    }

    GameFlags* SequenceTriggerComponent::findProgressFlags( const GameObject* pActivator ) const
    {
        if ( _audience == SequenceTriggerAudience::Everyone )
            return ActivatorVolumeComponent::findProgressFlags( pActivator );
        if ( pActivator == nullptr )
            return nullptr; // 활성자 없이 묻는 것(`hasFired`) — 플레이어별 기록은 고를 플레이어가 없다
        const GameObject*     pOwner   = getOwner();
        GameObjectManager*    pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        PlayerStateComponent* pState   = pManager != nullptr ? PlayerStateComponent::findForPawnObject( *pManager, *pActivator ) : nullptr;
        if ( pState == nullptr )
        {
            SW_LOG_ERROR_ID( "SequenceTrigger.ActivatorNoPlayerState", "Sequence trigger '%#' records progress flag '%#' per player but '%#' has no controller with a PlayerStateComponent - nothing plays",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?", getProgressFlag().c_str(), pActivator->getName().c_str() );
            return nullptr;
        }
        return &pState->getProgressFlags();
    }
} // namespace sw
