/**
 * @file ActionCombatEvents.h
 * @brief 액션 룸 · 전투 룸 이벤트입니다. 채널은 GameEvents.h 의 `gameEventChannel()`("game") 입니다.
 * @details 넷 모두 프레임워크가 냅니다(`GameEventUtil::send` — 버스 스레드면 그 자리에서, 아니면 다음 `processEvents`):
 *          `DamageAppliedEvent` 는 `UnitStatsComponent` 가 피해를 적용한 자리에서, 룸 이벤트 셋은 `ActionRoom` 의 상태가 바뀌는 자리에서.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Event/EventType.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @brief 유닛이 피해를 받아 HP 가 깎였음을 알립니다(언리얼 `OnTakeAnyDamage`). HP 바 · 피해 숫자가 구독합니다.
     * @details `UnitStatsComponent` 가 HP 를 깎은 **그 자리 하나**에서 냅니다 — 투사체 · 공격 판정 · 게임 코드의 `takeDamage` 가 모두 거기를
     *          지납니다. 무적 · 죽음으로 깎이지 않은 피해는 내지 않습니다.
     *
     *          게임 스레드(이벤트 버스를 비우는 스레드)에서 적용되면 그 자리에서 냅니다(`publish`) — 구독자는 같은 프레임에 받습니다. 다른
     *          스레드라면 큐에 싣고(`push`) 다음 `processEvents` 에 받습니다. 컴포넌트에 직접 거는 구독은 `UnitStatsComponent::registerDamageApplied`
     *          입니다(언리얼 `OnTakeAnyDamage`). 대상은 핸들입니다 — 큐로 받았으면 그때 이미 사라졌을 수 있으니 풀어 확인합니다.
     */
    struct SW_GF_API DamageAppliedEvent final : IEvent
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
} // namespace sw

namespace sw
{
    /** @brief 액션 룸을 클리어했음을 알립니다(`ActionRoom` — 마지막 적이 쓰러진 프레임). */
    struct SW_GF_API RoomClearedEvent final : IEvent
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
} // namespace sw

namespace sw
{
    /** @brief 액션 룸에서 플레이어가 패배했음을 알립니다(`ActionRoom::onPlayerDefeated`). */
    struct SW_GF_API PlayerDefeatedInRoomEvent final : IEvent
    {
        string _returnMapPath; ///< 복귀할 오버월드 맵
        SW_DECLARE_GAMEPLAY_EVENT( PlayerDefeatedInRoomEvent );
    };
} // namespace sw

namespace sw
{
    /** @brief 클리어 게이트 잠금이 바뀌었음을 알립니다(`ActionRoom` — 전투 시작에 닫히고, 클리어 · 패배에 열린다). */
    struct SW_GF_API ClearGateStateChangedEvent final : IEvent
    {
        string                 _zoneID;         ///< 존 ID
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
