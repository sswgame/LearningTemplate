#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameTriggerComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameFlow.h"

namespace sw
{
    MinigameTriggerComponent::MinigameTriggerComponent()
        : TriggerComponent( TriggerAuthority::Server ) // 들어가기는 게임 상태를 바꾼다 — 서버(와 혼자 하는 게임)만 판정한다
        , _minigame{}
    {
    }

    bool MinigameTriggerComponent::onVolumeActivated( const GameObject& activator )
    {
        (void)activator;
        MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
        if ( _minigame.empty() || pFlow == nullptr )
        {
            const GameObject* const pOwner = getOwner();
            SW_LOG_ERROR_ID( "MinigameTrigger.NothingToEnter", "Minigame trigger '%#' %# - nothing is entered", pOwner != nullptr ? pOwner->getName().c_str() : "?",
                             _minigame.empty() ? "has no minigame name" : "is in a world without a running game" );
            return false;
        }
        // 하위 레벨 미니게임은 이 볼륨의 변환에 얹는다(언리얼 레벨 인스턴스 액터 자리) — 씬 전환 미니게임은 자리를 보지 않는다.
        const GameObject* const     pOwner    = getOwner();
        optional<SubLevelPlacement> placement = nullopt;
        if ( pOwner != nullptr )
            placement = MinigameFlow::makeAnchorPlacement( *pOwner );
        pFlow->requestEnter( _minigame.view(), placement ); // 틱 뒤 작업이다 — 스냅숏 · 씬 요청은 다음 게임 프레임 앞에서 한다
        return true;
    }
} // namespace sw
