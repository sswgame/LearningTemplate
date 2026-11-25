/**
 * @file DamageEvents.h
 * @brief 유닛이 피해를 받았음을 알리는 이벤트입니다. 채널은 GameEvents.h 의 `gameEventChannel()`("game")입니다.
 */
#pragma once
#include "Core/Event/EventType.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/GameObjectHandle.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @brief 유닛이 피해를 받아 HP 가 깎였음을 알립니다(언리얼 `OnTakeAnyDamage`). 받는 쪽은 액션 룸 HUD(`ActionRoomHUDComponent` — 카메라 흔들림)이고, HP 바와 피해 숫자는 체력 원천 · 피해 숫자 창구가 직접 잇는다.
     * @details 전투 어트리뷰트 세트(`CombatAttributeSet`)가 메타 피해를 체력으로 옮기는 **한 곳**에서만 낸다. 투사체, 공격 판정, 기믹, 어빌리티 피해가 모두 그곳을 지난다. 무적이나 죽음 때문에 깎이지 않은 피해는 내지 않는다.
     *
     *          게임 스레드(이벤트 버스를 비우는 스레드)에서 적용되면 그 자리에서 낸다(`publish`). 그러면 구독자는 같은 프레임에 받는다. 다른 스레드에서 적용되면 큐에 넣고(`push`) 다음 `processEvents` 에서 받는다.
     *          한 오브젝트의 피격만 받으려면 어빌리티 시스템의 피격 게임플레이 이벤트(`CombatAttributeSet::getHitEventTag`)를 쓴다.
     *          대상은 핸들이다. 큐로 받았다면 그 사이 대상이 사라졌을 수 있으므로 핸들로 대상을 찾아 확인한다.
     */
    struct SW_GF_API DamageAppliedEvent final : EventBase
    {
        GameObjectHandle       _instigator;   ///< 피해를 낸 쪽(쏘거나 휘두른 오브젝트). 모르면 무효 핸들
        GameObjectHandle       _target;       ///< 피해를 받은 유닛
        int32                  _amount;       ///< 들어간 피해. 방어로 감쇠한 값을 반올림했다. 죽인 피해는 남았던 HP 보다 클 수 있다(넘친 피해)
        int32                  _remainingHP;  ///< 깎인 뒤 남은 HP
        uint8                  _bKilled  : 1; ///< 이 피해로 죽었는지
        [[maybe_unused]] uint8 _reserved : 7;

        /** @brief 무효 핸들, 0, 살아 있음으로 초기화합니다. */
        DamageAppliedEvent() noexcept
            : _instigator{}
            , _target{}
            , _amount{ 0 }
            , _remainingHP{ 0 }
            , _bKilled{ SW_FALSE }
            , _reserved{ 0 } {}

        SW_DECLARE_GAMEPLAY_EVENT( DamageAppliedEvent );
    };
} // namespace sw
