/**
 * @file CueTriggerComponent.h
 * @brief 활성자가 들어오면 연출 신호와 소리를 내는 볼륨입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/TriggerComponent.h"

namespace sw
{
    struct AudioPlayableAsset;

    /**
     * @class CueTriggerComponent
     * @brief 활성자(보통 태그 `Player`)가 들어오면(`TriggerComponent` — 겹침 또는 거리) 연출 신호(`GimmickUtil::sendCue`, `_cue` — 큐 표의 연출과 `GimmickCueEvent`)와 소리를 냅니다.
     * @details 한 번만 하지 않는 볼륨(`_bOnce` 꺼짐)은 `_cooldown` 뒤에 다시 작동합니다. 연출은 보이는 일이라 월드마다 판정합니다(`TriggerAuthority::Local`).
     */
    REFLECT( Category = "Gimmick", DisplayName = "Cue Trigger", Tooltip = "Fires a presentation cue and a sound when the activator enters" )
    class SW_GF_API CueTriggerComponent : public TriggerComponent
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
        AssetRef<AudioPlayableAsset> _sound;
        PROPERTY( Category = "Cue", DisplayName = "Cooldown", Tooltip = "Time before a volume that is not Once fires again", Min = 0.0, Units = s )
        float32 _cooldown;
        PROPERTY( RuntimeState, Category = "Cue", DisplayName = "Cooldown Left", Tooltip = "Runtime", Units = s )
        float32 _cooldownLeft;
    };
} // namespace sw
