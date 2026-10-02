/**
 * @file ActionCombatEvents.h
 * @brief 액션 룸 · 전투 룸 이벤트입니다. 채널은 GameEvents.h 의 `gameEventChannel()`("game") 입니다.
 * @details 프레임워크가 실제로 내는 것은 `DamageAppliedEvent` 하나입니다(`UnitStatsComponent` 가 피해를 적용한 자리에서). 나머지 셋은
 *          게임이 주고받는 어휘입니다 — 쏘는 곳이 없습니다(2026-10-03 확인).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Event/EventType.h"

namespace sw
{
    /**
     * @brief 유닛이 피해를 받아 HP 가 깎였음을 알립니다(언리얼 `OnTakeAnyDamage`). HP 바 · 피해 숫자가 구독합니다.
     * @details `UnitStatsComponent` 가 HP 를 깎은 **그 자리 하나**에서 냅니다 — 투사체 · 공격 판정 · 게임 코드의 `takeDamage` 가 모두 거기를
     *          지납니다. 무적 · 죽음으로 깎이지 않은 피해는 내지 않습니다.
     *
     *          `EventDispatcher::push` 로 큐에 싣습니다(아무 스레드나 되는 쪽). 피해는 틱 직후 큐 · 겹침 전달 · 게임 코드 어디서든 적용될 수
     *          있어 버스 쪽(`publish`, 큐를 비우는 스레드 전용)으로는 낼 수 없습니다. 구독자는 다음 `processEvents`(`EngineLoop` 프레임 첫머리)에
     *          받습니다 — 그때 대상이 이미 사라졌을 수 있으니 핸들로 풀어 확인합니다.
     */
    struct DamageAppliedEvent final : IEvent
    {
        GameObjectHandle       _instigator;   ///< 피해를 낸 쪽(쏜 · 휘두른 오브젝트). 모르면 무효 핸들
        GameObjectHandle       _target;       ///< 피해를 받은 유닛
        int32                  _amount;       ///< 들어간 피해 — 방어력을 빼고 최소 1 을 지난 값. 죽인 피해는 남았던 HP 보다 클 수 있다(넘친 피해)
        int32                  _remainingHp;  ///< 깎인 뒤 남은 HP
        uint8                  _bKilled  : 1; ///< 이 피해로 죽었는지
        [[maybe_unused]] uint8 _reserved : 7;

        /** @brief 무효 핸들 · 0 · 살아 있음으로 둡니다. */
        DamageAppliedEvent() noexcept
            : _instigator{}
            , _target{}
            , _amount{ 0 }
            , _remainingHp{ 0 }
            , _bKilled{ SW_FALSE }
            , _reserved{ 0 } {}

        SW_DECLARE_GAMEPLAY_EVENT( DamageAppliedEvent );
    };

    /** @brief 액션 룸을 클리어했음을 알립니다. */
    struct RoomClearedEvent final : IEvent
    {
        string                 _mapPath;           ///< 클리어한 맵
        uint8                  _bBossDefeated : 1; ///< 보스 처치 여부
        [[maybe_unused]] uint8 _reserved      : 7;

        /** @brief 보스 미처치 · 예약 비트를 0 으로 둡니다. */
        RoomClearedEvent() noexcept
            : _bBossDefeated{ SW_FALSE }
            , _reserved{ 0 } {}

        SW_DECLARE_GAMEPLAY_EVENT( RoomClearedEvent );
    };

    /** @brief 액션 룸에서 플레이어가 패배했음을 알립니다. */
    struct PlayerDefeatedInRoomEvent final : IEvent
    {
        string _returnMapPath; ///< 복귀할 오버월드 맵
        SW_DECLARE_GAMEPLAY_EVENT( PlayerDefeatedInRoomEvent );
    };

    /** @brief 클리어 게이트 잠금이 바뀌었음을 알립니다. */
    struct ClearGateStateChangedEvent final : IEvent
    {
        string                 _zoneId;         ///< 존 ID
        uint8                  _bLocked    : 1; ///< 잠김 여부
        uint8                  _bTriggered : 1; ///< 방 진입 시 닫힘 트리거
        [[maybe_unused]] uint8 _reserved   : 6;

        /** @brief 열림 · 비트리거 · 예약 비트를 0 으로 둡니다. */
        ClearGateStateChangedEvent() noexcept
            : _bLocked{ SW_FALSE }
            , _bTriggered{ SW_FALSE }
            , _reserved{ 0 } {}

        SW_DECLARE_GAMEPLAY_EVENT( ClearGateStateChangedEvent );
    };
} // namespace sw
