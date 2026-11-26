#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Presentation/AudioPlayableAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Presentation/GameSound.h"
#include "GameFramework/Runtime/Foundation/Trigger/TriggerComponent.h"
#include "GameFramework/Runtime/Gameplay/Mechanism/MechanismUtil.h"

namespace sw
{
    struct AudioPlayableAsset;

    /**
     * @class CueTriggerComponent
     * @brief 활성자(보통 태그 `Player`)가 들어오면(`TriggerComponent`: 겹침 또는 거리) 연출 신호(`MechanismUtil::sendCue`, `_cue`: 큐 표의 연출과 `MechanismCueEvent`)와 소리를 냅니다.
     * @details 한 번만 하지 않는 볼륨(`_bOnce` 꺼짐)은 `_cooldown` 뒤에 다시 작동합니다. 연출은 보이는 일이라 월드마다 판정합니다(`TriggerAuthority::Local`).
     */
    REFLECT( Category = "Mechanism", DisplayName = "Cue Trigger", Tooltip = "Fires a presentation cue and a sound when the activator enters" )
    class CueTriggerComponent : public TriggerComponent
    {
    public:
        REFLECT_BODY();

        CueTriggerComponent();
        ~CueTriggerComponent() override = default;

        /** @brief 거리 판정 뒤에 다시 작동하기까지의 시간을 줄입니다. */
        void onTick( TickContext& context ) override;

    protected:
        [[nodiscard]] bool onVolumeActivated( const GameObject& activator ) override;

    private:
        PROPERTY( Required, Category = "Cue", DisplayName = "Cue", Tooltip = "Cue name for the sequencer / camera / audio listeners" )
        hashed_string _cue;
        PROPERTY( Category = "Cue", DisplayName = "Sound" )
        AssetRef<AudioPlayableAsset> _soundAsset;
        PROPERTY( Category = "Cue", DisplayName = "Cooldown", Tooltip = "Time before a volume that is not Once fires again", Min = 0.0, Units = s )
        float32 _cooldown;
        PROPERTY( RuntimeState, Category = "Cue", DisplayName = "Cooldown Left", Tooltip = "Runtime", Units = s )
        float32 _cooldownLeft;
    };

    CueTriggerComponent::CueTriggerComponent()
        : TriggerComponent( TriggerAuthority::Local ) // 연출 · 소리는 월드마다 같이 도는 보이는 일이다
        , _cue{}
        , _soundAsset{}
        , _cooldown{ 10.0f }
        , _cooldownLeft{ 0.0f }
    {
    }

    void CueTriggerComponent::onTick( TickContext& context )
    {
        TriggerComponent::onTick( context );
        _cooldownLeft = MathUtil::max( 0.0f, _cooldownLeft - context.getDeltaTime().count() );
    }

    bool CueTriggerComponent::onVolumeActivated( const GameObject& activator )
    {
        GameObject* const pOwner = getOwner();
        if ( pOwner == nullptr || _cooldownLeft > 0.0f )
            return false;
        _cooldownLeft               = _cooldown;
        const SceneComponent* pRoot = pOwner->getPrimarySceneComponent();
        MechanismUtil::sendCue( *pOwner, _cue, pRoot != nullptr ? pRoot->getWorldPosition() : float3{}, 0.0f, activator.getHandle() );
        if ( _soundAsset.empty() == false )
            (void)GameSound::play( _soundAsset.id() );
        return true;
    }
} // namespace sw

#include "CueTriggerComponent.cpp.gen.inl"
