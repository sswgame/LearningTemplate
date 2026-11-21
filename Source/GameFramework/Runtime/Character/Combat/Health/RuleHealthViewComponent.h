/**
 * @file RuleHealthViewComponent.h
 * @brief 키트 규칙(POD 시뮬레이션)의 체력을 화면 · HUD · 공용 체력 소비자에게 내는 뷰 컴포넌트입니다 — 디렉터가 그 틱의 규칙 체력을 스냅숏으로 밀어 넣습니다.
 * @details 키트 규칙의 체력(정수 HP 배열 · `HealthStateMachine` · 상태 바이트)은 롤백 · 락스텝 POD 규칙 때문에 오브젝트가 아니고 어빌리티 어트리뷰트로 합치지 않는다.
 *          그 유닛의 모습 오브젝트에 이 컴포넌트를 붙이고, 디렉터(`GameModeComponent::updateGame`)가 규칙을 돈 뒤 `applySnapshot` 으로 값을 넣으면
 *          HP 바(`HealthBarComponent`) · 피격 연출 · 쓰러짐 반응이 같은 계약(`HealthSourceComponent` → `HealthListenerComponent`)으로 읽는다.
 *          언리얼에서 서버 권위 체력을 복제해 받은 프록시 컴포넌트가 `OnHealthChanged` 를 내는 자리, 유니티 DOTS 의 시뮬레이션 → 표현 동기화(Presentation)와 같은 나눔이다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/Combat/Health/HealthSourceComponent.h"

namespace sw
{
    /**
     * @class RuleHealthViewComponent
     * @brief 규칙 체력의 스냅숏을 들고 체력 원천으로 내는 뷰입니다. 체력을 정하지 않는다 — 값의 주인은 디렉터가 쥔 키트 규칙이다.
     * @details 첫 스냅숏은 연출 없이(`Reset`) 알리고, 그 뒤로는 값이 바뀐 스냅숏만 알린다(같은 값을 매 틱 넣어도 HP 바가 흔들리지 않는다).
     *          스냅숏을 받기 전에는 체력이 없는 주인으로 읽혀(`_bHasHealth` SW_FALSE) HP 바가 저장된 비율을 쓴다.
     *          값은 매 틱 다시 들어오는 뷰 상태라 저장하지 않는다(`Transient`) — 복원한 규칙이 다음 틱에 다시 넣는다.
     */
    REFLECT( Category = "Combat", DisplayName = "Rule Health View", Tooltip = "Shows a kit rule's health (pushed by the director each step) to health bars and other health listeners" )
    class SW_GF_API RuleHealthViewComponent : public HealthSourceComponent
    {
    public:
        REFLECT_BODY();

        RuleHealthViewComponent();
        virtual ~RuleHealthViewComponent() override;

        /** @brief 스냅숏의 체력입니다. 스냅숏 전이면 `_bHasHealth` 가 SW_FALSE 이고, 체력이 0 이하이면 쓰러진 것이다. */
        HealthReading getHealthReading() const override;

        /**
         * @brief 그 틱의 규칙 체력 @p health · 최대 @p maxHealth 를 넣습니다. 디렉터가 규칙을 돈 뒤 게임 스레드(틱 밖)에서 부른다.
         * @details 첫 스냅숏은 `Reset` 으로, 값이 바뀐 스냅숏은 `Changed`(0 이하면 `Died`)로 같은 오브젝트의 받는 쪽에 알린다. 같은 값이면 알리지 않는다.
         */
        void applySnapshot( float32 health, float32 maxHealth );
        /** @brief 다음 스냅숏을 첫 스냅숏처럼 연출 없이 알리게 합니다 — 판을 새로 열거나 상태를 복원해 값이 이어지지 않을 때 디렉터가 부른다. */
        void resetSnapshot() { _bHasSnapshot = SW_FALSE; }
        /** @brief 스냅숏을 한 번이라도 받았으면 true 입니다. */
        bool hasSnapshot() const { return _bHasSnapshot == SW_TRUE; }

    private:
        PROPERTY( Category = "Health", DisplayName = "Health", Tooltip = "Rule health of the last snapshot", Transient, ReadOnly )
        float32 _health;
        PROPERTY( Category = "Health", DisplayName = "Max Health", Tooltip = "Rule max health of the last snapshot", Transient, ReadOnly )
        float32                _maxHealth;
        uint8                  _bHasSnapshot : 1; ///< 스냅숏을 받았다(전이면 체력이 없는 주인으로 읽힌다)
        [[maybe_unused]] uint8 _reserved     : 7;
    };
} // namespace sw
