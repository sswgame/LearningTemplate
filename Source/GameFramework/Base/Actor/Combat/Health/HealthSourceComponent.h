/**
 * @file HealthSourceComponent.h
 * @brief 오브젝트의 체력을 가진 컴포넌트(어빌리티 시스템 · 키트의 유닛 스탯 · 게임의 적)의 베이스입니다 — 읽기(지금 · 최대 · 쓰러짐)와 받는 쪽에 알리기를 한 곳에 둡니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 체력 원천이 지금 들고 있는 체력입니다(`HealthSourceComponent::getHealthReading`). */
    struct HealthReading
    {
        float32 _health{ 0.0f };        ///< 지금 체력
        float32 _maxHealth{ 0.0f };     ///< 최대 체력 — 0 이하면 비율은 0
        uint8   _bHasHealth{ SW_TRUE }; ///< 체력이 있는 주인인지 — 체력 어트리뷰트가 없는 어빌리티 시스템은 SW_FALSE
        uint8   _bDead{ SW_FALSE };     ///< 쓰러졌는지
        uint8   _arrReserved[2]{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class HealthSourceComponent
     * @brief 오브젝트의 체력을 가진 컴포넌트의 베이스입니다. 파생은 자기 수치를 그대로 들고(정수 HP · 어트리뷰트 · 게임의 float) 읽기 하나(`getHealthReading`)만 냅니다.
     * @details 언리얼 Lyra 의 `ULyraHealthComponent`(어빌리티 시스템의 체력을 HP 바 · 죽음 처리에 내는 컴포넌트)와 같은 자리입니다. 다른 점: 감싸는 컴포넌트를
     *          따로 두지 않고 체력을 가진 컴포넌트가 직접 상속합니다 — 어빌리티 시스템은 체력 어트리뷰트 이름을 이미 들고 있어 감쌀 것이 없고, 감싸면 프리팹마다
     *          컴포넌트가 하나씩 는다. RTTI 가 없어 순수 인터페이스로는 오브젝트에서 찾을 수 없으므로 리플렉션 베이스로 둡니다
     *          (`getComponent<HealthSourceComponent>()` — 언리얼 `FindComponentByClass`).
     *
     *          알림은 `notifyHealthChanged` 한 곳입니다 — 비율 · 종류(다시 두기 · 바뀜 · 쓰러짐)를 읽기에서 정해 같은 오브젝트의 `HealthListenerComponent`(HP 바)에
     *          보냅니다. 시뮬레이션 키트(`Vitality` · 정수 HP 배열 — 상태 바이트 · 롤백)는 유닛이 오브젝트가 아니라 상속하지 않습니다. 그 유닛 위에 HP 바를 띄울
     *          게임은 뷰 컴포넌트가 상속해 스냅샷을 읽습니다.
     */
    REFLECT( Abstract, Category = "Combat", DisplayName = "Health Source", Tooltip = "Base of the components that own their object's health (unit stats, ability system)" )
    class SW_GF_API HealthSourceComponent : public Component
    {
    public:
        REFLECT_BODY();

        HealthSourceComponent();
        virtual ~HealthSourceComponent() override;

        /** @brief 지금 체력입니다. */
        virtual HealthReading getHealthReading() const = 0;
        /** @brief 체력 비율 0..1 입니다. 최대가 0 이하이거나 체력이 없는 주인이면 0 입니다. */
        float32 getHealthRatio() const;

    protected:
        /**
         * @brief 지금 체력을 같은 오브젝트의 받는 쪽(`HealthListenerComponent`)에 알립니다. 체력이 바뀌는 자리(시작 · 피해 · 회복 · 스탯 설정)가 부릅니다.
         * @param bReset 흔적 없이 그 값으로(시작 · 스탯 재설정 · 부활). 아니면 쓰러졌으면 `Died`, 아니면 `Changed` 입니다.
         * @details 주인이 없거나 체력이 없는 주인이면 알리지 않습니다. 그 자리에서(같은 스레드) 보냅니다 — 받는 쪽은 자기 상태만 고칩니다.
         */
        void notifyHealthChanged( bool bReset ) const;
    };
} // namespace sw
