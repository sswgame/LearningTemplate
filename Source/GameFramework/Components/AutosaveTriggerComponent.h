/**
 * @file AutosaveTriggerComponent.h
 * @brief 닿으면 자동 저장을 부르는 볼륨 — 체크포인트 · 보스 방 앞 · 지역 경계에 놓습니다(레벨 디자이너가 씬에 배치).
 */
#pragma once
#include "Core/String/TagID.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Framework/Autosave.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class AutosaveTriggerComponent
     * @brief 같은 오브젝트의 트리거 콜라이더에 활성자(태그가 맞는 오브젝트)가 들어오면 게임 서비스 `AutosaveManager` 에 까닭과 이름을 넘깁니다.
     * @details 체크포인트는 `reachCheckpoint`(같은 체크포인트는 한 번), 보스 앞은 `notifyBeforeBoss`, 지역 경계는 `notifyAreaChanged`, 그 밖은 `requestSave`.
     *          저장 자체는 관리자의 다음 `update` 가 한다(막기 · 최소 간격). 관리자가 게임 서비스로 묶여 있지 않으면 아무것도 하지 않는다.
     *          언리얼에서 게임마다 짓는 체크포인트 트리거 볼륨 · 유니티 `OnTriggerEnter` 체크포인트 스크립트의 자리입니다.
     */
    REFLECT( Category = "Gameplay", DisplayName = "Autosave Trigger", Tooltip = "Requests an autosave or a checkpoint when the activator enters this object's trigger" )
    class SW_GF_API AutosaveTriggerComponent : public Component
    {
    public:
        REFLECT_BODY();

        AutosaveTriggerComponent();
        virtual ~AutosaveTriggerComponent() override = default;

        void onOverlapBegin( const OverlapInfo& overlap ) override;

        /** @brief 까닭 · 이름 · 활성자 태그를 둡니다(코드로 세운 볼륨 — 씬에 놓은 것은 PROPERTY 가 채운다). 발동 표시를 지운다. */
        void configure( AutosaveTrigger trigger, const hashed_string& label, TagID activatorTag );
        /** @brief 활성자가 들어온 것으로 칩니다(겹침 이벤트 · 시험이 부른다). 넘겼으면 true 입니다. */
        bool activate( const GameObject* pActivator );
        bool hasFired() const { return _bFired == SW_TRUE; }

    private:
        PROPERTY( Category = "Autosave", DisplayName = "Trigger", Tooltip = "Why this volume saves: Checkpoint, BeforeBoss, AreaChanged, Manual" )
        AutosaveTrigger _trigger;
        PROPERTY( Category = "Autosave", DisplayName = "Label", Tooltip = "Checkpoint id, boss or area name written into the slot" )
        hashed_string _label;
        PROPERTY( Category = "Autosave", DisplayName = "Activator Tag", Tooltip = "Only objects with this tag activate it (empty: any object)" )
        TagID _activatorTag;
        PROPERTY( Category = "Autosave", DisplayName = "Once", Tooltip = "Fire only the first time" )
        bool  _bOnce;
        uint8 _bFired;
    };
} // namespace sw
