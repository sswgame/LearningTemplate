/**
 * @file IHealthSource.h
 * @brief 오브젝트의 체력을 가진 컴포넌트(어빌리티 시스템, 키트의 유닛 스탯, 게임의 적)가 내보내는 리플렉션 인터페이스입니다.
 * @details 체력 읽기(지금, 최대, 쓰러짐)와 받는 쪽에 알리기를 한곳에 둡니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h" // HitInfo
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 체력 원천이 지금 가진 체력입니다(`IHealthSource::getHealthReading`). */
    struct HealthReading
    {
        float32 _health{ 0.0f };        ///< 지금 체력
        float32 _maxHealth{ 0.0f };     ///< 최대 체력. 0 이하면 비율은 0 이다
        uint8   _bHasHealth{ SW_TRUE }; ///< 체력이 있는 주인인지. 체력 어트리뷰트가 없는 어빌리티 시스템은 SW_FALSE 다
        uint8   _bDead{ SW_FALSE };     ///< 쓰러졌는지
        uint8   _arrReserved[2]{};
    };
} // namespace sw

namespace sw
{
    class GameObject;

    /**
     * @class IHealthSource
     * @brief 체력 읽기 계약입니다. 체력을 가진 컴포넌트는 자기 수치(정수 HP, 어트리뷰트, 게임의 float)를 그대로 보관하고, 포트 멤버로 이 인터페이스를 내보냅니다
     *        (`REFLECT_INTERFACE( IHealthSource, _healthSourcePort )`).
     * @details 언리얼 Lyra 의 `ULyraHealthComponent`(어빌리티 시스템의 체력을 HP 바와 죽음 처리에 내는 컴포넌트) 자리이고, 찾기는 유니티 `GetComponent<IHealth>()` 와 같다
     *          (`GameObject::findInterface<IHealthSource>()`). 예전에는 RTTI 가 없어 리플렉션 베이스(`Component` 파생)였고, 역할 하나가 단일 기반 자리를 차지해
     *          체력 원천이면서 다른 역할을 겸할 수 없었다 — 지금은 상태 없는 인터페이스라 구현 컴포넌트의 기반이 자유롭다.
     *
     *          알림은 `notifyHealthChanged` 한 곳에서 보냅니다. 비율과 종류(초기화, 바뀜, 쓰러짐)를 읽기에서 정해 같은 오브젝트의 `IHealthListener`(HP 바)에
     *          보냅니다. 시뮬레이션 키트(`HealthStateMachine`, 정수 HP 배열, 상태 바이트, 롤백)는 유닛이 오브젝트가 아니라서 내보내지 않습니다. 그 유닛 위에 HP 바를 띄울
     *          게임은 뷰 컴포넌트(`RuleHealthViewComponent`)가 이 인터페이스를 내보내 스냅샷을 읽습니다.
     */
    REFLECT( Interface, Category = "Combat", DisplayName = "Health Source", Tooltip = "Contract of the components that own their object's health (unit stats, ability system)" )
    struct SW_GF_API IHealthSource
    {
        REFLECT_BODY();

        virtual ~IHealthSource() = default;

        /** @brief 지금 체력입니다. */
        virtual HealthReading getHealthReading() const = 0;
        /** @brief 체력 비율 0..1 입니다. 최대가 0 이하이거나 체력이 없는 주인이면 0 입니다. */
        float32 getHealthRatio() const;

        /**
         * @brief @p source 의 지금 체력을 @p pOwner 의 받는 쪽(`IHealthListener`)에 알립니다. 체력이 바뀌는 곳(시작, 피해, 회복, 스탯 설정)에서 부릅니다.
         * @param bReset 연출 없이 그 값으로 바꿉니다(시작, 스탯 재설정, 부활). 아니면 쓰러졌을 때 `Died`, 그 밖에는 `Changed` 입니다.
         * @details 주인이 없거나 체력이 없는 주인이면 알리지 않습니다. 같은 스레드에서 바로 보내므로 받는 쪽은 자기 상태만 고칩니다. 상태 없는 정적 함수라 인터페이스 규칙을 어기지 않는다.
         */
        static void notifyHealthChanged( const IHealthSource& source, const GameObject* pOwner, bool bReset );
        /**
         * @brief 이 맞음으로 쓰러졌다고 정했을 때 부릅니다. @p pOwner 의 다른 컴포넌트에 치명 표시(`HitInfo::_bFatal`)를 단 사본을 다시 보냅니다.
         * @details 엔진은 체력을 몰라 때린 쪽(무기 레이캐스트 · 근접 판정)은 치명인지 모른다. 쓰러짐을 정하는 쪽이 체력 원천이라 치명 신호도 여기서 낸다 —
         *          래그돌 전환(`RagdollComponent::_bRagdollOnFatalHit`) · 절단(`SkeletalSeveringComponent`)이 같은 신호 하나로 움직인다. 언리얼에서 체력 컴포넌트의
         *          죽음 이벤트에 래그돌 · 절단을 묶는 자리다. 맞음 사건(`GameObjectEvents::_hitReceived`)으로 다시 내며 반응만 받는다 —
         *          피해는 받는 쪽 포트(`IHitReceiver`)로 들어와 사건을 구독하지 않으므로 다시 깎이지 않는다. 이미 치명 표시가 달린 맞음이면 보내지 않는다.
         */
        static void deliverFatalHit( GameObject* pOwner, const HitInfo& hit );
    };
} // namespace sw
