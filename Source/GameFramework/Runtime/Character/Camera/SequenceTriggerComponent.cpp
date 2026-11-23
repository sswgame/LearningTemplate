#include "pch.h"

#include "GameFramework/Runtime/Character/Camera/SequenceTriggerComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"

#include "GameFramework/Runtime/Character/Control/Controller/PlayerStateComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"

namespace sw
{
    SequenceTriggerComponent::SequenceTriggerComponent()
        : TriggerComponent( TriggerAuthority::Server ) // 서버가 틀고 넷 키트(`GF_NetSequence`)가 `_audience` 대로 보낸다
        , _sequence{}
        , _audience{ SequenceTriggerAudience::Everyone }
        , _listPlay{}
        , _listPrivateDirector{}
        , _activationCount{ 0 }
    {
    }

    void SequenceTriggerComponent::onRegister( GameObjectManager& manager )
    {
        TriggerComponent::onRegister( manager );
        manager.getComponentRegistry().add<SequenceTriggerComponent>( this );
    }

    void SequenceTriggerComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<SequenceTriggerComponent>( this );
        TriggerComponent::onUnregister( manager );
    }

    void SequenceTriggerComponent::onEndPlay()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        for ( const GameObjectHandle& director : _listPrivateDirector )
        {
            GameObject* pDirector = pManager != nullptr ? pManager->resolveGameObject( director ) : nullptr;
            if ( pDirector != nullptr )
                pManager->destroyGameObject( pDirector );
        }
        _listPrivateDirector.clear();
        _listPlay.clear();
        TriggerComponent::onEndPlay();
    }

    void SequenceTriggerComponent::onBeginPlay()
    {
        TriggerComponent::onBeginPlay();
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
        (void)VectorUtil::removeIf( _listPlay, [pManager]( const SequenceTriggerPlay& play )
        {
            const GameObject*              pDirector = pManager->resolveGameObject( play._director );
            const SequencePlayerComponent* pDone     = pDirector != nullptr ? pDirector->getComponent<SequencePlayerComponent>() : nullptr;
            return pDone == nullptr || pDone->isPlaying() == false;
        } ); // 지운 수는 쓰지 않는다
        // 원격 플레이어만 보는 재생은 서버가 시간만 잰다 — 씬의 디렉터와 묶인 오브젝트는 그 플레이어의 월드가 움직인다.
        // 재생 중 다시 들어오면 처음부터 다시 틀지 않는다(언리얼 `UMovieSceneSequencePlayer::Play` 는 재생 중이면 그대로다) — 발동으로 치지 않는다.
        if ( isRemotePlay( *pManager, activator ) )
            pPlayer = hasPlayOf( activator ) ? nullptr : preparePrivateDirector( *pManager, *pPlayer );
        else if ( pPlayer->isPlaying() )
            pPlayer = nullptr;
        if ( pPlayer == nullptr )
            return false;
        pPlayer->play();
        // 데이터 결함(범위 제한 칸 없음 · 속도 0)이면 play 가 오류를 남기고 틀지 않는다 — 발동으로 치지 않아 한 번만 하는 볼륨이 잠기지 않는다.
        if ( pPlayer->isPlaying() == false )
            return false;
        ++_activationCount;
        _listPlay.push_back( SequenceTriggerPlay{ activator.getHandle(), pPlayer->getOwner()->getHandle(), _activationCount } );
        return true;
    }

    bool SequenceTriggerComponent::isRemotePlay( const GameObjectManager& manager, const GameObject& activator ) const
    {
        // 혼자 도는 월드 · 리슨 서버 호스트의 폰은 이 월드의 화면이 본다 — 씬의 디렉터가 튼다.
        if ( _audience != SequenceTriggerAudience::Activator || manager.getSubsystem<SceneNetLink>().isBound() == false )
            return false;
        const PawnComponent* pPawn = activator.getComponent<PawnComponent>();
        return pPawn != nullptr && pPawn->findInputConnection().has_value();
    }

    bool SequenceTriggerComponent::hasPlayOf( const GameObject& activator ) const
    {
        for ( const SequenceTriggerPlay& play : _listPlay )
        {
            if ( play._activator == activator.getHandle() )
                return true;
        }
        return false;
    }

    SequencePlayerComponent* SequenceTriggerComponent::preparePrivateDirector( GameObjectManager& manager, const SequencePlayerComponent& source )
    {
        for ( const GameObjectHandle& director : _listPrivateDirector )
        {
            GameObject*              pDirector = manager.resolveGameObject( director );
            SequencePlayerComponent* pIdle     = pDirector != nullptr ? pDirector->getComponent<SequencePlayerComponent>() : nullptr;
            if ( pIdle != nullptr && pIdle->isPlaying() == false )
            {
                pIdle->copyTimelineFrom( source );
                return pIdle;
            }
        }
        fixed_string<constant::buffer::kMax128> directorName;
        formatstring( directorName.data(), directorName.capacity(), "%#_Private%#", source.getOwner()->getName().c_str(), static_cast<uint32>( _listPrivateDirector.size() ) );
        GameObject* pDirector = manager.createGameObject( hashed_string( directorName.c_str() ) );
        if ( pDirector == nullptr )
            return nullptr;
        pDirector->setTransient( true ); // 트리거가 다시 세우는 파생 — 세이브 · 상태 해시에 넣지 않는다(`GameObject::setTransient`)
        SequencePlayerComponent* pPlayer = pDirector->addComponent<SequencePlayerComponent>();
        if ( pPlayer == nullptr )
            return nullptr;
        pPlayer->copyTimelineFrom( source );
        _listPrivateDirector.push_back( pDirector->getHandle() );
        return pPlayer;
    }

    GameFlags* SequenceTriggerComponent::findProgressFlags( const GameObject* pActivator ) const
    {
        if ( _audience == SequenceTriggerAudience::Everyone )
            return TriggerComponent::findProgressFlags( pActivator );
        if ( pActivator == nullptr )
            return nullptr; // 활성자 없이 묻는 것(`hasFired`) — 플레이어별 기록은 고를 플레이어가 없다
        const GameObject*        pOwner            = getOwner();
        const GameObjectManager* pManager          = pOwner != nullptr ? pOwner->getManager() : nullptr;
        GameObject*              pControllerObject = pManager != nullptr ? PawnComponent::findControllerObject( *pManager, *pActivator ) : nullptr;
        PlayerStateComponent*    pState            = pControllerObject != nullptr ? pControllerObject->getComponent<PlayerStateComponent>() : nullptr;
        if ( pState == nullptr )
        {
            SW_LOG_ERROR_ID( "SequenceTrigger.ActivatorNoPlayerState", "Sequence trigger '%#' records progress flag '%#' per player but '%#' has no controller with a PlayerStateComponent - nothing plays",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?", getProgressFlag().c_str(), pActivator->getName().c_str() );
            return nullptr;
        }
        return &pState->getProgressFlags();
    }
} // namespace sw
