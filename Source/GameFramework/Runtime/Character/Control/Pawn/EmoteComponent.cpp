#include "pch.h"

#include "GameFramework/Runtime/Character/Control/Pawn/EmoteComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"

#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

namespace sw
{
    EmoteComponent::EmoteComponent()
        : _listEmote{}
        , _activeState{}
        , _resumeState{}
        , _listButtonIndex{}
    {
    }

    void EmoteComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        const GameObject*    pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        if ( pPawn == nullptr || pOwner->getComponent<SkeletalAnimatorComponent>() == nullptr )
        {
            SW_LOG_ERROR_ID( "Emote.NoPawnOrAnimator", "Emotes of '%#' need a PawnComponent and a SkeletalAnimatorComponent on the same object", pOwner != nullptr ? pOwner->getName().c_str() : "" );
            return;
        }
        _listButtonIndex.clear();
        for ( const EmoteDef& emote : _listEmote )
        {
            _listButtonIndex.push_back( pPawn->findButton( emote._button ) );
            if ( _listButtonIndex.back().has_value() == false || emote._state.empty() )
                SW_LOG_ERROR_ID( "Emote.BadEntry", "Emote '%#' of '%#' names the button '%#' the pawn does not have, or no state", emote._state.c_str(), pOwner->getName().c_str(), emote._button.c_str() );
        }
    }

    void EmoteComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject&                         owner       = context.getSelf();
        const PawnComponent*                pPawn       = owner.getComponent<PawnComponent>();
        SkeletalAnimatorComponent*          pAnimator   = owner.getComponent<SkeletalAnimatorComponent>();
        const CharacterControllerComponent* pController = owner.getComponent<CharacterControllerComponent>();
        if ( pPawn == nullptr || pAnimator == nullptr )
            return;
        const ControlIntent& intent    = pPawn->getIntent();
        const bool           bOnGround = pController == nullptr || pController->getMovementMode() == CharacterMovementMode::Walking;
        const bool           bStill    = intent._move._x == 0.0f && intent._move._y == 0.0f && bOnGround;
        if ( _activeState.empty() == false )
        {
            const AnimationPlayer&    player   = pAnimator->getGraphPlayer().getPlayer();
            const IAnimationPlayable* pPlaying = player.getCurrentPlayable();
            const bool                bOnce    = player.isCrossfading() == false && player.isCurrentLooping() == false && pPlaying != nullptr;
            const bool                bEnded   = bOnce && player.getCurrentTime() >= pPlaying->getPlayLength() - MathUtil::kEpsilon;
            if ( pAnimator->getCurrentStateName() != _activeState && player.isCrossfading() == false )
                _activeState = hashed_string{}; // 다른 동작이 가져갔다 — 되돌리지 않는다
            else if ( bStill == false || bEnded )
                stop( *pAnimator );
        }
        if ( bStill == false )
            return;
        for ( size_t index = 0; index < _listButtonIndex.size(); ++index )
        {
            if ( intent.wasTriggered( _listButtonIndex[index] ) == false )
                continue;
            const EmoteDef& emote = _listEmote[index];
            if ( _activeState.empty() )
                _resumeState = pAnimator->getCurrentStateName();
            if ( pAnimator->play( emote._state, emote._bLoop ) ) // 없는 동작이면 애니메이터가 오류를 남긴다
                _activeState = emote._state;
            return;
        }
    }

    void EmoteComponent::stop( SkeletalAnimatorComponent& animator )
    {
        _activeState = hashed_string{};
        if ( _resumeState.empty() == false )
            (void)animator.play( _resumeState, true ); // 돌아갈 상태가 그래프에서 사라졌으면 그대로 둔다
        _resumeState = hashed_string{};
    }
} // namespace sw
