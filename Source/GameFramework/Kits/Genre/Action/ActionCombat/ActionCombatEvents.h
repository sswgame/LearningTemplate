/**
 * @file ActionCombatEvents.h
 * @brief 액션 룸과 전투 룸의 이벤트입니다. 채널은 GameEvents.h 의 `gameEventChannel()`("game")입니다.
 * @details 네 이벤트 모두 프레임워크가 낸다(`GameEventUtil::send`). 이벤트 버스를 비우는 스레드에서 보내면 그 자리에서 전달하고, 다른 스레드에서 보내면 다음 `processEvents` 에 전달한다.
 *          `DamageAppliedEvent` 는 `UnitStatsComponent` 가 피해를 적용하는 시점에, 룸 이벤트 세 가지는 `ActionRoom` 의 상태가 바뀌는 시점에 낸다.
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
     * @brief 유닛이 피해를 받아 HP 가 깎였음을 알립니다(언리얼 `OnTakeAnyDamage`). HP 바와 피해 숫자가 구독합니다.
     * @details `UnitStatsComponent` 가 HP 를 깎는 **한 곳**에서만 낸다. 투사체, 공격 판정, 게임 코드의 `takeDamage` 가 모두 그곳을 지난다. 무적이나 죽음 때문에 깎이지 않은 피해는 내지 않는다.
     *
     *          게임 스레드(이벤트 버스를 비우는 스레드)에서 적용되면 그 자리에서 낸다(`publish`). 그러면 구독자는 같은 프레임에 받는다. 다른 스레드에서 적용되면 큐에 넣고(`push`) 다음 `processEvents` 에서 받는다.
     *          컴포넌트에 직접 구독하려면 `UnitStatsComponent::registerDamageApplied` 를 쓴다(언리얼 `OnTakeAnyDamage` 에 해당).
     *          대상은 핸들이다. 큐로 받았다면 그 사이 대상이 사라졌을 수 있으므로 핸들로 대상을 찾아 확인한다.
     */
    struct SW_KIT_API DamageAppliedEvent final : IEvent
    {
        GameObjectHandle       _instigator;   ///< 피해를 낸 쪽(쏘거나 휘두른 오브젝트). 모르면 무효 핸들
        GameObjectHandle       _target;       ///< 피해를 받은 유닛
        int32                  _amount;       ///< 들어간 피해. 방어력을 빼고 최소 1을 보장한 값이다. 죽인 피해는 남았던 HP 보다 클 수 있다(넘친 피해)
        int32                  _remainingHp;  ///< 깎인 뒤 남은 HP
        uint8                  _bKilled  : 1; ///< 이 피해로 죽었는지
        [[maybe_unused]] uint8 _reserved : 7;

        /** @brief 무효 핸들, 0, 살아 있음으로 초기화합니다. */
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
    /** @brief 액션 룸을 클리어했음을 알립니다(`ActionRoom` 에서 마지막 적이 쓰러진 프레임에 냅니다). */
    struct SW_KIT_API RoomClearedEvent final : IEvent
    {
        string                 _mapPath;           ///< 클리어한 맵
        uint8                  _bBossDefeated : 1; ///< 보스 처치 여부
        [[maybe_unused]] uint8 _reserved      : 7;

        /** @brief 보스 미처치와 예약 비트를 0으로 초기화합니다. */
        RoomClearedEvent() noexcept
            : _bBossDefeated{ SW_FALSE }
            , _reserved{ 0 } {}

        SW_DECLARE_GAMEPLAY_EVENT( RoomClearedEvent );
    };
} // namespace sw

namespace sw
{
    /** @brief 액션 룸에서 플레이어가 패배했음을 알립니다(`ActionRoom::onPlayerDefeated`). */
    struct SW_KIT_API PlayerDefeatedInRoomEvent final : IEvent
    {
        string _returnMapPath; ///< 복귀할 오버월드 맵
        SW_DECLARE_GAMEPLAY_EVENT( PlayerDefeatedInRoomEvent );
    };
} // namespace sw

namespace sw
{
    /** @brief 클리어 게이트의 잠금이 바뀌었음을 알립니다(`ActionRoom` 에서 전투를 시작할 때 닫히고, 클리어하거나 패배하면 열립니다). */
    struct SW_KIT_API ClearGateStateChangedEvent final : IEvent
    {
        string                 _zoneID;         ///< 존 ID
        uint8                  _bLocked    : 1; ///< 잠김 여부
        uint8                  _bTriggered : 1; ///< 방에 들어서면 닫히는 트리거인지
        [[maybe_unused]] uint8 _reserved   : 6;

        /** @brief 열림, 비트리거, 예약 비트로 초기화합니다. */
        ClearGateStateChangedEvent() noexcept
            : _bLocked{ SW_FALSE }
            , _bTriggered{ SW_FALSE }
            , _reserved{ 0 } {}

        SW_DECLARE_GAMEPLAY_EVENT( ClearGateStateChangedEvent );
    };
} // namespace sw
