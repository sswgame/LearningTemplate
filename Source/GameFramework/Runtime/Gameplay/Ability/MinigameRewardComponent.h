/**
 * @file MinigameRewardComponent.h
 * @brief 허브 캐릭터가 미니게임 결과 값(`MinigameResultValue`)을 받아 몸(어빌리티 이펙트)과 경제 · 진행(지갑 · 평판 · 퀘스트)에 넘기는 컴포넌트입니다 — 결과 값 이름 → 보상은 데이터(PROPERTY)입니다.
 * @details 미니게임 디렉터는 허브 캐릭터를 직접 고치지 않고 결과 값으로만 내보낸다(디렉터 계약 ③ `MatchGameModeComponent::endMatch`). 받는 쪽은 허브 캐릭터이고,
 *          `MinigameFlow::onFinished`(돌아올 자리를 복원한 뒤)를 구독해 값마다 표의 이펙트를 건다 — 복원이 들어갈 때 찍은 체력으로 되감은 뒤에 얹으므로 보상이 지워지지 않는다.
 *          값의 수(`_amount`)는 이펙트 스펙의 SetByCaller `Data.Amount`(회복량 · 버프 세기) · 통화 액수 · 평판 변화 · 퀘스트 목표 수로 넘긴다. 같은 값 이름을 여러 표에 적으면 모두 준다.
 *          어느 표에도 없는 이름 · 카탈로그에 없는 이펙트 · 없는 받는 쪽은 오류다(조용히 버리지 않는다).
 *          지갑 · 평판 · 일지는 플레이어 상태 오브젝트(`_playerState` — 조종자)의 컴포넌트다 — 폰을 바꿔 타도 남는 자리다(언리얼 `APlayerState`).
 *          언리얼 Lyra 에서 경기 끝 보상을 GameplayEffect 로 플레이어 ASC 에 거는 자리, 용과 같이의 미니게임 상품이 본편 돈 · 아이템 · 회복으로 돌아오는 길과 같다.
 */
#pragma once
#include "Core/Concurrency/DevCounter.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObject;
    struct AbilityEffectDef;
    struct MinigameResult;
    struct MinigameResultValue;

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

    /** @brief 결과 값 이름 하나가 지갑에 더할 통화입니다(`_listCurrency` 의 한 줄). 값의 수가 액수다(0 보다 커야 한다). */
    REFLECT()
    struct MinigameRewardCurrency
    {
        REFLECT_BODY();
        PROPERTY( Required, Tooltip = "Result value name the minigame director reports" )
        hashed_string _value{};
        PROPERTY( Required, Tooltip = "Currency added to the player's wallet by the value amount" )
        hashed_string _currency{};
    };

    /** @brief 결과 값 이름 하나가 바꿀 세력 평판입니다(`_listReputation` 의 한 줄). 값의 수가 변화량이다(음수면 깎는다). */
    REFLECT()
    struct MinigameRewardReputation
    {
        REFLECT_BODY();
        PROPERTY( Required, Tooltip = "Result value name the minigame director reports" )
        hashed_string _value{};
        PROPERTY( Required, Tooltip = "Faction whose reputation changes by the value amount" )
        hashed_string _faction{};
    };

    /** @brief 결과 값 이름 하나가 퀘스트 일지에 알릴 일입니다(`_listQuest` 의 한 줄 — `QuestLog::notify`). 그 목표를 세는 퀘스트가 없으면 할 일이 없다. */
    REFLECT()
    struct MinigameRewardQuest
    {
        REFLECT_BODY();
        PROPERTY( Required, Tooltip = "Result value name the minigame director reports" )
        hashed_string _value{};
        PROPERTY( Required, Tooltip = "Objective kind notified to the player's quest log" )
        hashed_string _kind{};
        PROPERTY( Required, Tooltip = "Objective target notified to the player's quest log" )
        hashed_string _target{};
    };

    /**
     * @class MinigameRewardComponent
     * @brief 미니게임 결과 값을 같은 오브젝트의 `AbilitySystemComponent` 이펙트와 플레이어 상태 오브젝트의 지갑 · 평판 · 퀘스트 일지에 넘기는 받는 쪽입니다(파일 머리말 참고).
     */
    REFLECT( Category = "Gameplay", DisplayName = "Minigame Reward", Tooltip = "Turns minigame result values into ability effects on this object and wallet, reputation and quest progress on the player state object" )
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
         * @brief 결과 @p result 의 값마다 표의 보상을 줍니다 — 이펙트는 같은 오브젝트의 어빌리티 시스템, 통화 · 평판 · 퀘스트는 플레이어 상태 오브젝트. 흐름의 알림이 부르고 테스트도 부른다. 게임 스레드(틱 밖)에서 부른다.
         * @return 모든 보상을 주었으면 true 입니다. 어느 표에도 없는 이름, 없는 받는 쪽(어빌리티 시스템 · 지갑 · 열린 평판 · 열린 일지), 카탈로그에 없는 이펙트 · 대상 태그에 막힌 이펙트 · 0 이하의 통화가 있으면
         *         그것마다 오류를 남기고 false 다(다른 보상은 그대로 준다 — 하나의 오류가 다른 보상을 지우지 않는다).
         */
        [[nodiscard]] bool applyResult( const MinigameResult& result );
        /** @brief 결과 값 이름 → 이펙트 표를 정합니다. 씬 데이터는 PROPERTY `_listReward` 로 적고, 코드로 세우는 캐릭터 · 테스트가 이것을 부른다. */
        void setRewardTable( vector<MinigameRewardEffect> listReward ) { _listReward = std::move( listReward ); }
        /** @brief 결과 값 이름 → 통화 · 평판 · 퀘스트 표와 그것을 받을 플레이어 상태 오브젝트를 정합니다. 씬 데이터는 PROPERTY 로 적고, 코드로 세우는 캐릭터 · 테스트가 이것을 부른다. */
        void setPlayerRewardTable( GameObjectHandle playerState, vector<MinigameRewardCurrency> listCurrency, vector<MinigameRewardReputation> listReputation, vector<MinigameRewardQuest> listQuest );
        /** @brief 준 보상 수입니다(감시 · 진단). */
        const DevCounter& getAppliedCount() const { return _appliedCount; }

    private:
        /** @brief 흐름의 결과 알림입니다(`applyResult` 로 넘긴다 — 실패는 그 안에서 알렸다). */
        void onMinigameFinished( const MinigameResult& result );
        /** @brief 이펙트 하나를 같은 오브젝트의 어빌리티 시스템에 겁니다. 걸지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool applyEffect( const MinigameResult& result, const MinigameResultValue& value, const MinigameRewardEffect& reward );
        /** @brief 플레이어 상태 오브젝트의 @p T 입니다. 오브젝트가 없거나 컴포넌트가 없거나 열리지 않았으면 오류를 남기고 nullptr 입니다. */
        template <typename T>
        [[nodiscard]] T* findOnPlayerState( const MinigameResult& result, const MinigameResultValue& value ) const;

    private:
        PROPERTY( Category = "Reward", DisplayName = "Effects", Tooltip = "Result value name to effect on this object; a value missing from every table is an error" )
        vector<MinigameRewardEffect> _listReward;
        PROPERTY( Category = "Reward", DisplayName = "Player State", Tooltip = "Object that holds the player's wallet, reputation and quest log (the player controller object); needed by the tables below" )
        GameObjectHandle _playerState;
        PROPERTY( Category = "Reward", DisplayName = "Currencies", Tooltip = "Result value name to currency added to the player's wallet" )
        vector<MinigameRewardCurrency> _listCurrency;
        PROPERTY( Category = "Reward", DisplayName = "Reputation", Tooltip = "Result value name to faction reputation of the player" )
        vector<MinigameRewardReputation> _listReputation;
        PROPERTY( Category = "Reward", DisplayName = "Quests", Tooltip = "Result value name to quest objective of the player's quest log" )
        vector<MinigameRewardQuest> _listQuest;

        Subscription _finishedSubscription; ///< `MinigameFlow::onFinished` 구독(플레이 시작에 걸고 끝에 푼다)
        DevCounter   _appliedCount;         ///< 준 보상 수
    };
} // namespace sw
