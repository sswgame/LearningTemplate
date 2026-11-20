#include "pch.h"

#include "GameFramework/Runtime/Character/Camera/SequenceTriggerComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"

namespace sw
{
    SequenceTriggerComponent::SequenceTriggerComponent()
        : _sequence{}
    {
    }

    bool SequenceTriggerComponent::onVolumeActivated( const GameObject& activator )
    {
        (void)activator;
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
        return pPlayer->isPlaying();
    }
} // namespace sw
