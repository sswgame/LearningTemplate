#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/SequenceAppearanceComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"

#include "GameFramework/Base/Gameplay/Appearance/CharacterAppearanceComponent.h"

namespace sw
{
    SW_LOG_CALLER( "SequenceAppearance" );

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
            SW_LOG_WARNING( "Sequence appearance on '%#' has no SequencePlayerComponent next to it - it hears nothing", pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            return;
        }
        _eventSubscription = pPlayer->registerSequenceEvent(
            SW_DELEGATE_METHOD( OnSequenceEventDelegate, &SequenceAppearanceComponent::handleSequenceEvent, this ) );
    }

    void SequenceAppearanceComponent::onEndPlay()
    {
        GameObject*              pOwner  = getOwner();
        SequencePlayerComponent* pPlayer = pOwner != nullptr ? pOwner->getComponent<SequencePlayerComponent>() : nullptr;
        if ( pPlayer != nullptr && _eventSubscription.isValid() )
            pPlayer->unregisterSequenceEvent( _eventSubscription );
        _eventSubscription = DelegateHandle{};
        Component::onEndPlay();
    }

    void SequenceAppearanceComponent::handleSequenceEvent( const SequenceTrackItem& item )
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr )
            (void)applyAppearanceItem( *pManager, item ); // 실패는 안에서 오류로 남겼다
    }

    bool SequenceAppearanceComponent::applyAppearanceItem( GameObjectManager& manager, const SequenceTrackItem& item )
    {
        if ( item._kind != SequenceItemKind::Appearance )
            return false;
        GameObject*                   pTarget     = manager.findGameObjectByName( hashed_string{ item._targetObject.c_str() } );
        CharacterAppearanceComponent* pAppearance = pTarget != nullptr ? pTarget->getComponent<CharacterAppearanceComponent>() : nullptr;
        if ( pAppearance == nullptr )
        {
            SW_LOG_ERROR( "Sequence item '%#' wants preset '%#' on '%#' but the object %#", item._name.c_str(), item._preset.c_str(), item._targetObject.c_str(),
                          pTarget == nullptr ? "is not in the scene" : "has no CharacterAppearanceComponent" );
            return false;
        }
        pAppearance->setPreset( hashed_string{ item._preset.c_str() }, static_cast<uint32>( pAppearance->getSeed() ) );
        return true;
    }
} // namespace sw
