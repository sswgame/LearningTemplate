/**
 * @file MinigameTriggerComponent.h
 * @brief 활성자가 들어오면 미니게임으로 들어가는 볼륨입니다 — 맵의 장소 · 사물(오락실 기계 · 카드 탁자)이 미니게임으로 들어가는 자리입니다.
 */
#pragma once
#include "Core/Math/VectorMath.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Trigger/TriggerComponent.h"

namespace sw
{
    class MinigameFlow;

    /**
     * @class MinigameTriggerComponent
     * @brief 활성자가 들어오면(`TriggerComponent`: 겹침 또는 거리) `_minigame` 으로 들어가기를 `MinigameFlow` 에 맡깁니다(`requestEnter`: 다음 게임 프레임 앞, 틱 밖).
     * @details 용과 같이 · GTA 의 가게 문 · 오락실 기계, 유니티의 `OnTriggerEnter` 에서 `SceneManager.LoadScene` 을 부르는 스크립트 자리다 —
     *          게임마다 적는 일을 데이터 칸 하나로 둔다. 진입이 되었는지는 흐름이 알린다(맡기기까지가 발동이다).
     *          판정 자리: 접속한 세션에서 보통 미니게임은 서버만 판정한다(게임 상태를 바꾸는 일). 매치형은 닿은 클라이언트가 매칭 서버에 줄을 세워 달라고 요청한다 —
     *          줄 · 배정 · 매치 서버 접속은 서버가 판정하고, 세션을 옮기는 것은 그 클라이언트 하나라 서버 월드는 일하지 않는다(언리얼 Lyra 의 매치메이킹 → `ClientTravel`).
     *          나오기는 미니게임 쪽 일이다(`minigame.leave` · 미니게임의 끝) — 이 볼륨은 들어가기만 한다.
     *          돌아올 때 바탕 씬은 찍어 둔 상태로 복원되어 활성자가 이 볼륨 안에 다시 나타난다 — 그 진입을 세지 않으려면 `_spawnInside` 를 `Ignore` 로 둔다(나갔다 다시 들어와야 다시 들어간다).
     *          게임 인스턴스(흐름)가 없는 월드(에디터 편집 씬)는 오류를 남기고 발동으로 치지 않는다.
     */
    REFLECT( Category = "Gameplay", DisplayName = "Minigame Trigger", Tooltip = "Enters a minigame when the activator enters (trigger overlap or distance)" )
    class SW_GF_API MinigameTriggerComponent : public TriggerComponent
    {
    public:
        REFLECT_BODY();

        MinigameTriggerComponent();
        ~MinigameTriggerComponent() override = default;

        /** @brief 들어갈 미니게임 이름을 정합니다. 코드로 만든 볼륨 · 테스트가 씁니다. 씬에 놓은 볼륨은 PROPERTY 가 채웁니다. */
        void                 setMinigame( const hashed_string& minigame ) { _minigame = minigame; }
        const hashed_string& getMinigame() const { return _minigame; }
        /** @brief 하위 레벨을 얹을 자리를 이 볼륨에서 얼마나 옮길지 정합니다(월드 축, 미터). 입구를 세우는 코드가 쓴다. 씬에 놓은 볼륨은 PROPERTY 가 채웁니다. */
        void setSubLevelOffset( const float3& offset ) { _subLevelOffset = offset; }

    protected:
        [[nodiscard]] bool onVolumeActivated( const GameObject& activator ) override;

    private:
        /** @brief 이 월드가 이 입구를 판정하는지입니다 — 파일 머리말의 판정 자리 규칙. */
        [[nodiscard]] bool canEnterHere( const MinigameFlow& flow, const GameObject& activator ) const;

        PROPERTY( Category = "Minigame", DisplayName = "Minigame", Tooltip = "Minigame to enter (<pack>/minigames/<name>.minigame.xml)" )
        hashed_string _minigame;
        PROPERTY( Category = "Minigame", DisplayName = "Sub-Level Offset", Tooltip = "Where a sub-level minigame's map origin goes, from this volume (world axes)", Units = m )
        float3 _subLevelOffset;
    };
} // namespace sw
