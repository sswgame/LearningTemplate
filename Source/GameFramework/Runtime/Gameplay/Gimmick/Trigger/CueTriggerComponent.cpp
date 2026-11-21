#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Trigger/CueTriggerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Presentation/AudioPlayableAsset.h"

#include "GameFramework/Runtime/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/GameSound.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickEvents.h"

namespace sw
{
    CueTriggerComponent::CueTriggerComponent()
        : _cue{}
        , _sound{}
        , _requiredTags{}
        , _bOnce{ true }
        , _cooldown{ 10.0f }
        , _cooldownLeft{ 0.0f }
        , _fireCount{ 0 }
    {
        _requiredTags.addTag( TagID::request( "Player" ) );
    }

    void CueTriggerComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        _overlapSubscription = GameObjectEvents::listen<&CueTriggerComponent::handleOverlapBegin>( *this, &GameObjectEvents::_overlapBegin );
    }

    void CueTriggerComponent::handleOverlapBegin( const OverlapInfo& overlap )
    {
        const bool  bTagged     = overlap._pOther != nullptr && ( _requiredTags.getTagCount() == 0 || overlap._pOther->getTags().hasAllTags( _requiredTags ) );
        const bool  bReady      = ( _bOnce == false || _fireCount == 0 ) && _cooldownLeft <= 0.0f;
        GameObject* pOwner      = getOwner();
        const bool  bCannotFire = bTagged == false || bReady == false || pOwner == nullptr;
        if ( bCannotFire )
            return;
        ++_fireCount;
        _cooldownLeft = _cooldown;
        GimmickCueEvent event;
        event._source = pOwner->getHandle();
        event._target = overlap._pOther->getHandle();
        event._cue    = _cue;
        if ( pOwner->getPrimarySceneComponent() != nullptr )
            event._position = pOwner->getPrimarySceneComponent()->getWorldPosition();
        GameEventUtil::send( event );
        if ( _sound.empty() == false )
            (void)GameSound::play( _sound.id() );
    }

    void CueTriggerComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _cooldownLeft > 0.0f )
            _cooldownLeft = MathUtil::max( 0.0f, _cooldownLeft - context.getDeltaTime() );
    }
} // namespace sw
