#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/AutosaveTriggerComponent.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    AutosaveTriggerComponent::AutosaveTriggerComponent()
        : _trigger{ AutosaveTrigger::Checkpoint }
        , _label{}
    {
    }

    void AutosaveTriggerComponent::configure( AutosaveTrigger trigger, const hashed_string& label, TagID activatorTag )
    {
        _trigger = trigger;
        _label   = label;
        resetActivation( activatorTag );
    }

    bool AutosaveTriggerComponent::onVolumeActivated( const GameObject& activator )
    {
        (void)activator;
        AutosaveManager* pAutosave = game::findSubsystem<AutosaveManager>();
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
        return true;
    }
} // namespace sw
