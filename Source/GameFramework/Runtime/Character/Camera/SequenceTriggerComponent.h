/**
 * @file SequenceTriggerComponent.h
 * @brief 활성자가 들어오면 시퀀스(컷신)를 트는 볼륨입니다. 마을 도착 · 보스 방 입구 같은 자리에 둡니다.
 */
#pragma once
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/ActivatorVolumeComponent.h"

namespace sw
{
    /**
     * @class SequenceTriggerComponent
     * @brief 활성자가 들어오면(`ActivatorVolumeComponent` — 겹침 또는 거리) `_sequence` 오브젝트의 `SequencePlayerComponent` 를 처음부터 틉니다.
     * @details 컷 카메라 · 블렌드는 그 오브젝트의 `CinematicCameraComponent` 가, 외형 바꾸기는 `SequenceAppearanceComponent` 가 받는다 — 이 볼륨은 트는 때만 정한다.
     *          언리얼에서 트리거 박스의 겹침 이벤트로 Level Sequence Actor 를 Play 하는 블루프린트, 유니티에서 `OnTriggerEnter` 로 `PlayableDirector.Play` 를 부르는
     *          스크립트에 해당한다(둘 다 게임마다 손으로 적는다). 오브젝트에 시퀀스 플레이어가 없으면 오류를 남기고 발동한 것으로 치지 않는다.
     */
    REFLECT( Category = "Cinematics", DisplayName = "Sequence Trigger", Tooltip = "Plays a sequence player's sequence when the activator enters (trigger overlap or distance)" )
    class SW_GF_API SequenceTriggerComponent : public ActivatorVolumeComponent
    {
    public:
        REFLECT_BODY();

        SequenceTriggerComponent();
        ~SequenceTriggerComponent() override = default;

        /** @brief 틀 시퀀스 플레이어의 오브젝트를 정합니다. 코드로 만든 볼륨 · 테스트가 씁니다. 씬에 놓은 볼륨은 PROPERTY 가 채웁니다. */
        void setSequence( GameObjectHandle sequence ) { _sequence = sequence; }

    protected:
        [[nodiscard]] bool onVolumeActivated( const GameObject& activator ) override;

    private:
        PROPERTY( Category = "Sequence", DisplayName = "Sequence", Tooltip = "Object with the SequencePlayerComponent to play" )
        GameObjectHandle _sequence;
    };
} // namespace sw
