#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameTriggerComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/IMinigameMatchmaking.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameFlow.h"

namespace sw
{
    MinigameTriggerComponent::MinigameTriggerComponent()
        : TriggerComponent( TriggerAuthority::Local ) // 매치형은 닿은 클라이언트가 줄을 서고, 나머지는 서버(와 혼자 하는 게임)만 판정한다 — `canEnterHere` 가 가른다
        , _minigame{}
        , _subLevelOffset{}
    {
    }

    bool MinigameTriggerComponent::canEnterHere( const MinigameFlow& flow, const GameObject& activator ) const
    {
        const NetLinkValue* const pLink = SceneNetLink::findBoundFor( *this );
        if ( pLink == nullptr )
            return true; // 혼자 하는 게임
        const bool bServer = pLink->_role == NetLinkRole::Server;
        if ( flow.isMatchMinigame( _minigame.view() ) == false )
            return bServer;
        const IMinigameMatchmaking* const pMatchmaking = game::findSubsystem<IMinigameMatchmaking>();
        return bServer == false && pMatchmaking != nullptr && pMatchmaking->isLocalPlayer( activator );
    }

    bool MinigameTriggerComponent::onVolumeActivated( const GameObject& activator )
    {
        MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
        if ( _minigame.empty() || pFlow == nullptr )
        {
            const GameObject* const pOwner = getOwner();
            SW_LOG_ERROR_ID( "MinigameTrigger.NothingToEnter", "Minigame trigger '%#' %# - nothing is entered", pOwner != nullptr ? pOwner->getName().c_str() : "?",
                             _minigame.empty() ? "has no minigame name" : "is in a world without a running game" );
            return false;
        }
        if ( canEnterHere( *pFlow, activator ) == false )
            return false;
        // 하위 레벨 미니게임은 이 볼륨의 변환에 얹는다(언리얼 레벨 인스턴스 액터 자리) — 씬 전환 미니게임은 자리를 보지 않는다.
        const GameObject* const     pOwner    = getOwner();
        optional<SubLevelPlacement> placement = nullopt;
        if ( pOwner != nullptr )
            placement = MinigameFlow::makeAnchorPlacement( *pOwner );
        if ( placement.has_value() )
            placement->_position = placement->_position + _subLevelOffset;
        pFlow->requestEnter( _minigame.view(), placement ); // 틱 뒤 작업이다 — 스냅숏 · 씬 요청은 다음 게임 프레임 앞에서 한다
        return true;
    }
} // namespace sw
