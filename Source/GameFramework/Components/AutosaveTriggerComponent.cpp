#include "pch.h"

#include "GameFramework/Components/AutosaveTriggerComponent.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Framework/GameService.h"

namespace sw
{
    AutosaveTriggerComponent::AutosaveTriggerComponent()
        : _trigger{ AutosaveTrigger::Checkpoint }
        , _label{}
        , _activatorTag{}
        , _bOnce{ true }
        , _bFired{ SW_FALSE }
    {
    }

    void AutosaveTriggerComponent::onOverlapBegin( const OverlapInfo& overlap )
    {
        Component::onOverlapBegin( overlap );
        // 상대의 트리거(감지 범위)는 활성자가 아니다 — 몸이 들어와야 한다.
        if ( overlap._bOtherTrigger == SW_FALSE )
            (void)activate( overlap._pOther );
    }

    void AutosaveTriggerComponent::configure( AutosaveTrigger trigger, const hashed_string& label, TagID activatorTag )
    {
        _trigger      = trigger;
        _label        = label;
        _activatorTag = activatorTag;
        _bFired       = SW_FALSE;
    }

    bool AutosaveTriggerComponent::activate( const GameObject* pActivator )
    {
        if ( pActivator == nullptr || ( _bOnce && _bFired == SW_TRUE ) )
            return false;
        if ( _activatorTag.isValid() && pActivator->hasTag( _activatorTag ) == false )
            return false;
        AutosaveManager* pAutosave = game::getService<AutosaveManager>();
        if ( pAutosave == nullptr )
            return false;
        switch ( _trigger )
        {
            case AutosaveTrigger::Checkpoint:
            {
                pAutosave->reachCheckpoint( _label );
                break;
            }
            case AutosaveTrigger::BeforeBoss:
            {
                pAutosave->notifyBeforeBoss( _label );
                break;
            }
            case AutosaveTrigger::AreaChanged:
            {
                pAutosave->notifyAreaChanged( _label );
                break;
            }
            default:
            {
                pAutosave->requestSave( _trigger, _label );
                break;
            }
        }
        _bFired = SW_TRUE;
        return true;
    }
} // namespace sw
