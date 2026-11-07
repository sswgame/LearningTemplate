#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/SequenceAppearanceComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"

namespace sw
{
    SequenceAppearanceComponent::SequenceAppearanceComponent()
        : _eventSubscription{}
    {
    }

    void SequenceAppearanceComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        GameObject*              pOwner  = getOwner();
        SequencePlayerComponent* pPlayer = pOwner != nullptr ? pOwner->getComponent<SequencePlayerComponent>() : nullptr;
        if ( pPlayer == nullptr )
        {
            SW_LOG_WARNING_ID( "SequenceAppearance.SequenceAppearanceNoSequencePlayerComponent", "Sequence appearance on '%#' has no SequencePlayerComponent next to it - it hears nothing", pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            return;
        }
        _eventSubscription = pPlayer->registerSequenceEvent(
            SW_DELEGATE_METHOD( OnSequenceEventDelegate, &SequenceAppearanceComponent::handleSequenceEvent, this ) );
    }

    void SequenceAppearanceComponent::onEndPlay()
    {
        _eventSubscription.reset();
        Component::onEndPlay();
    }

    void SequenceAppearanceComponent::handleSequenceEvent( const SequenceFiredEvent& event )
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr )
            (void)applyAppearanceItem( *pManager, event ); // 실패는 안에서 오류로 남겼다
    }

    bool SequenceAppearanceComponent::applyAppearanceItem( GameObjectManager& manager, const SequenceFiredEvent& event )
    {
        const SequenceTrackItem& item = event._item;
        if ( item._kind != SequenceItemKind::Appearance )
            return false;
        GameObject*                   pTarget     = manager.resolveGameObject( event._target );
        CharacterAppearanceComponent* pAppearance = pTarget != nullptr ? pTarget->getComponent<CharacterAppearanceComponent>() : nullptr;
        if ( pAppearance == nullptr )
        {
            SW_LOG_ERROR_ID( "SequenceAppearance.SequenceItemWantsPreset", "Sequence item '%#' wants preset '%#' on binding %# but the object %#", item._name.c_str(), item._preset.c_str(), item._binding,
                             pTarget == nullptr ? "is not bound or no longer exists" : "has no CharacterAppearanceComponent" );
            return false;
        }
        pAppearance->setPreset( hashed_string{ item._preset.c_str() }, static_cast<uint32>( pAppearance->getSeed() ) );
        return true;
    }
} // namespace sw
