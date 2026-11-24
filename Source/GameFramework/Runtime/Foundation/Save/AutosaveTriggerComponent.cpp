#include "pch.h"

#include "GameFramework/Runtime/Foundation/Save/AutosaveTriggerComponent.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    AutosaveTriggerComponent::AutosaveTriggerComponent()
        : TriggerComponent( TriggerAuthority::Server ) // 저장은 권위 있는 판 상태를 싣는다 — 클라이언트 월드의 복제본을 저장하지 않는다(언리얼 `USaveGame` 은 서버 · 호스트가 쓴다)
        , _trigger{ AutosaveTrigger::Checkpoint }
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
