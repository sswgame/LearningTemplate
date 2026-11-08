/**
 * @file AutosaveTriggerComponent.h
 * @brief 닿으면 자동 저장을 부르는 볼륨입니다. 레벨 디자이너가 체크포인트, 보스 방 앞, 지역 경계에 배치합니다.
 */
#pragma once
#include "Core/Name/TagID.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/ActivatorVolumeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Autosave.h"

namespace sw
{
    /**
     * @class AutosaveTriggerComponent
     * @brief 활성자가 들어오면(`ActivatorVolumeComponent` — 겹침 또는 거리) 게임 서비스 `AutosaveManager` 에 까닭과 이름을 넘깁니다.
     * @details 체크포인트는 `reachCheckpoint`(같은 체크포인트는 한 번), 보스 앞은 `notifyBeforeBoss`, 지역 경계는 `notifyAreaChanged`, 그 밖은 `requestSave`.
     *          저장 자체는 관리자의 다음 `update` 가 한다(막기 · 최소 간격). 관리자는 gamesettings 의 `<autosaveSettings>` 를 적은 게임에만 있고, 없으면 아무것도 하지 않는다(발동한 것으로 치지 않는다).
     *          언리얼에서 게임마다 만드는 체크포인트 트리거 볼륨, 유니티의 `OnTriggerEnter` 체크포인트 스크립트에 해당합니다.
     */
    REFLECT( Category = "Gameplay", DisplayName = "Autosave Trigger", Tooltip = "Requests an autosave or a checkpoint when the activator enters this object's trigger" )
    class SW_GF_API AutosaveTriggerComponent : public ActivatorVolumeComponent
    {
    public:
        REFLECT_BODY();

        AutosaveTriggerComponent();
        virtual ~AutosaveTriggerComponent() override = default;

        /** @brief 까닭, 이름, 활성자 태그를 정하고 발동 표시를 지웁니다. 코드로 만든 볼륨이 씁니다. 씬에 놓은 볼륨은 PROPERTY 가 채웁니다. */
        void configure( AutosaveTrigger trigger, const hashed_string& label, TagID activatorTag );

    protected:
        [[nodiscard]] bool onVolumeActivated( const GameObject& activator ) override;

    private:
        PROPERTY( Category = "Autosave", DisplayName = "Trigger", Tooltip = "Why this volume saves: Checkpoint, BeforeBoss, AreaChanged, Manual" )
        AutosaveTrigger _trigger;
        PROPERTY( Category = "Autosave", DisplayName = "Label", Tooltip = "Checkpoint id, boss or area name written into the slot" )
        hashed_string _label;
    };
} // namespace sw
