#include "pch.h"

#include "GameFramework/Runtime/Character/Camera/SequenceTriggerComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"

#include "GameFramework/Runtime/Character/Control/Controller/PlayerStateComponent.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"

namespace sw
{
    SequenceTriggerComponent::SequenceTriggerComponent()
        : ActivatorVolumeComponent( ActivatorAuthority::Server ) // 서버가 틀고 넷 키트(`GF_NetSequence`)가 `_audience` 대로 보낸다
        , _sequence{}
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

    void SequenceTriggerComponent::onBeginPlay()
    {
        ActivatorVolumeComponent::onBeginPlay();
        // 트리거가 트는 디렉터는 스스로 틀지 않아야 한다(언리얼 Level Sequence 의 bAutoPlay 끔 + Trigger Box 와 같은 짝) — 둘 다 켜면 데이터 모순이다.
        const GameObject*              pOwner    = getOwner();
        const GameObjectManager*       pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const GameObject*              pSequence = pManager != nullptr ? pManager->resolveGameObject( _sequence ) : nullptr;
        const SequencePlayerComponent* pPlayer   = pSequence != nullptr ? pSequence->getComponent<SequencePlayerComponent>() : nullptr;
        if ( pPlayer != nullptr && pPlayer->isAutoPlay() )
        {
            SW_LOG_ERROR_ID( "SequenceTrigger.DirectorAutoPlays", "Sequence trigger '%#' points at '%#' whose SequencePlayerComponent plays by itself (_bAutoPlay) - it starts before anyone enters and the trigger never fires; turn Auto Play off",
                             pOwner->getName().c_str(), pSequence->getName().c_str() );
        }
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
