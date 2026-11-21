/**
 * @file MinigameRewardComponent.h
 * @brief 허브 캐릭터가 미니게임 결과 값(`MinigameResultValue`)을 받아 같은 오브젝트의 어빌리티 시스템에 이펙트로 거는 컴포넌트입니다 — 결과 값 이름 → 이펙트는 데이터(PROPERTY)입니다.
 * @details 미니게임 디렉터는 허브 캐릭터를 직접 고치지 않고 결과 값으로만 내보낸다(디렉터 계약 ③ `GameModeComponent::finishMinigame`). 받는 쪽은 허브 캐릭터이고,
 *          `MinigameFlow::onFinished`(돌아올 자리를 복원한 뒤)를 구독해 값마다 표의 이펙트를 건다 — 복원이 들어갈 때 찍은 체력으로 되감은 뒤에 얹으므로 보상이 지워지지 않는다.
 *          값의 수(`_amount`)는 스펙의 SetByCaller `Data.Amount` 로 넘긴다(회복량 · 버프 세기). 표에 없는 이름 · 카탈로그에 없는 이펙트는 오류다(조용히 버리지 않는다).
 *          언리얼 Lyra 에서 경기 끝 보상을 GameplayEffect 로 플레이어 ASC 에 거는 자리, 용과 같이의 미니게임 상품이 본편 아이템 · 회복으로 돌아오는 길과 같다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct AbilityEffectDef;
    struct MinigameResult;

    /** @brief 결과 값 이름 하나가 걸 이펙트입니다(`MinigameRewardComponent::_listReward` 의 한 줄). */
    REFLECT()
    struct MinigameRewardEffect
    {
        REFLECT_BODY();
        PROPERTY( Required, Tooltip = "Result value name the minigame director reports (MinigameResultValue::_name)" )
        hashed_string _value{};
        PROPERTY( Required, Tooltip = "Effect id (AbilityCatalog) applied to this object's ability system; the value amount is SetByCaller Data.Amount" )
        CatalogKey<AbilityEffectDef> _effect{};
    };

    /**
     * @class MinigameRewardComponent
     * @brief 미니게임 결과 값을 같은 오브젝트의 `AbilityComponent` 이펙트로 거는 받는 쪽입니다(파일 머리말 참고).
     */
    REFLECT( Category = "Gameplay", DisplayName = "Minigame Reward", Tooltip = "Applies minigame result values to this object's ability system as effects (value name to effect table)" )
    class SW_GF_API MinigameRewardComponent : public Component
    {
    public:
        REFLECT_BODY();

        /** @brief 결과 값의 수를 이펙트 스펙에 넣는 SetByCaller 이름입니다. */
        static constexpr const utf8* kAmountName = "Data.Amount";

        MinigameRewardComponent();
        ~MinigameRewardComponent() override;

        /** @brief 미니게임 흐름(`MinigameFlow::onFinished`)을 구독합니다. 흐름이 없는 호스트(테스트 · 도구)에서는 구독하지 않는다. */
        void onBeginPlay() override;
        /** @brief 구독을 풉니다. */
        void onEndPlay() override;

        /**
         * @brief 결과 @p result 의 값마다 표의 이펙트를 같은 오브젝트의 어빌리티 시스템에 겁니다. 흐름의 알림이 부르고 테스트도 부른다. 게임 스레드(틱 밖)에서 부른다.
         * @return 모든 값을 걸었으면 true 입니다. 어빌리티 시스템이 없거나, 표에 없는 이름 · 카탈로그에 없는 이펙트 · 대상 태그에 막힌 값이 있으면 그 값마다 오류를 남기고 false 다
         *         (다른 값은 그대로 건다 — 한 값의 오류가 다른 보상을 지우지 않는다).
         */
        [[nodiscard]] bool applyResult( const MinigameResult& result );
        /** @brief 결과 값 이름 → 이펙트 표를 정합니다. 씬 데이터는 PROPERTY `_listReward` 로 적고, 코드로 세우는 캐릭터 · 테스트가 이것을 부른다. */
        void setRewardTable( vector<MinigameRewardEffect> listReward ) { _listReward = std::move( listReward ); }
        /** @brief 결과 값을 이펙트로 건 수입니다(감시 · 진단). */
        uint32 getAppliedCount() const { return _appliedCount; }

    private:
        /** @brief 흐름의 결과 알림입니다(`applyResult` 로 넘긴다 — 실패는 그 안에서 알렸다). */
        void onMinigameFinished( const MinigameResult& result );
        /** @brief @p value 에 맞는 표의 줄입니다. 없으면 nullptr 입니다. */
        const MinigameRewardEffect* findReward( const hashed_string& value ) const;

    private:
        PROPERTY( Category = "Reward", DisplayName = "Rewards", Tooltip = "Result value name to effect table; a value missing from it is an error" )
        vector<MinigameRewardEffect> _listReward;

        Subscription _finishedSubscription; ///< `MinigameFlow::onFinished` 구독(플레이 시작에 걸고 끝에 푼다)
        uint32       _appliedCount;         ///< 이펙트로 건 결과 값 수
    };
} // namespace sw
