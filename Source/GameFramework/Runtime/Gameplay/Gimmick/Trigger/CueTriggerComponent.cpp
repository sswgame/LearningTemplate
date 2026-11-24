#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Trigger/CueTriggerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Presentation/AudioPlayableAsset.h"

#include "GameFramework/Runtime/Foundation/Presentation/GameSound.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickUtil.h"

namespace sw
{
    CueTriggerComponent::CueTriggerComponent()
        : TriggerComponent( TriggerAuthority::Local ) // 연출 · 소리는 월드마다 같이 도는 보이는 일이다
        , _cue{}
        , _sound{}
        , _cooldown{ 10.0f }
        , _cooldownLeft{ 0.0f }
    {
    }

    void CueTriggerComponent::onTick( TickContext& context )
    {
        TriggerComponent::onTick( context );
        _cooldownLeft = MathUtil::max( 0.0f, _cooldownLeft - context.getDeltaTime() );
    }

    bool CueTriggerComponent::onVolumeActivated( const GameObject& activator )
    {
        GameObject* const pOwner = getOwner();
        if ( pOwner == nullptr || _cooldownLeft > 0.0f )
            return false;
        _cooldownLeft               = _cooldown;
        const SceneComponent* pRoot = pOwner->getPrimarySceneComponent();
        GimmickUtil::sendCue( *pOwner, _cue, pRoot != nullptr ? pRoot->getWorldPosition() : float3{}, 0.0f, activator.getHandle() );
        if ( _sound.empty() == false )
            (void)GameSound::play( _sound.id() );
        return true;
    }
} // namespace sw
