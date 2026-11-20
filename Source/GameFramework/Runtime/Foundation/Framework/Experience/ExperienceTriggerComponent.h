/**
 * @file ExperienceTriggerComponent.h
 * @brief 활성자가 들어오면 Experience 로 들어가는 볼륨입니다 — 허브의 장소 · 사물(오락실 기계 · 카드 탁자)이 미니게임으로 들어가는 자리이고, 허브 이름을 적으면 허브로 돌아갑니다.
 */
#pragma once
#include "Core/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/ActivatorVolumeComponent.h"

namespace sw
{
    /**
     * @class ExperienceTriggerComponent
     * @brief 활성자가 들어오면(`ActivatorVolumeComponent` — 겹침 또는 거리) `_experience` 로 들어가기를 `ExperienceManager` 에 맡깁니다(`requestEnter` — 다음 게임 프레임 앞, 틱 밖).
     * @details 언리얼 Lyra 의 포털 패드(겹침에서 `UserFacingExperience` 로 이동을 거는 블루프린트), 유니티의 `OnTriggerEnter` 에서 `SceneManager.LoadScene` 을 부르는 스크립트 자리다 —
     *          게임마다 적는 일을 데이터 칸 하나로 둔다. 진입이 되었는지는 관리자가 알린다(맡기기까지가 발동이다). 판정은 서버만 한다(게임 상태를 바꾸는 일).
     *          돌아올 때 허브는 찍어 둔 상태로 복원되어 활성자가 이 볼륨 안에 다시 나타난다 — 그 진입을 세지 않으려면 `_spawnInside` 를 `Ignore` 로 둔다(나갔다 다시 들어와야 다시 들어간다).
     *          게임 인스턴스(관리자)가 없는 월드(에디터 편집 씬)는 오류를 남기고 발동으로 치지 않는다.
     */
    REFLECT( Category = "Gameplay", DisplayName = "Experience Trigger", Tooltip = "Enters an experience (mini-game, or the hub to return) when the activator enters (trigger overlap or distance)" )
    class SW_GF_API ExperienceTriggerComponent : public ActivatorVolumeComponent
    {
    public:
        REFLECT_BODY();

        ExperienceTriggerComponent();
        ~ExperienceTriggerComponent() override = default;

        /** @brief 들어갈 Experience 이름을 정합니다. 코드로 만든 볼륨 · 테스트가 씁니다. 씬에 놓은 볼륨은 PROPERTY 가 채웁니다. */
        void                 setExperience( const hashed_string& experience ) { _experience = experience; }
        const hashed_string& getExperience() const { return _experience; }

    protected:
        [[nodiscard]] bool onVolumeActivated( const GameObject& activator ) override;

    private:
        PROPERTY( Category = "Experience", DisplayName = "Experience", Tooltip = "Experience to enter (<pack>/experiences/<name>.experience.xml); the hub's name returns to the hub" )
        hashed_string _experience;
    };
} // namespace sw
