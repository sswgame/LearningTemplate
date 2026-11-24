/**
 * @file ActionCombatEvents.h
 * @brief 액션 룸과 전투 룸의 이벤트입니다. 채널은 GameEvents.h 의 `gameEventChannel()`("game")입니다.
 * @details 세 이벤트 모두 프레임워크가 낸다(`GameEventUtil::send`). 이벤트 버스를 비우는 스레드에서 보내면 그 자리에서 전달하고, 다른 스레드에서 보내면 다음 `processEvents` 에 전달한다.
 *          모두 `ActionRoom` 의 상태가 바뀌는 시점에 낸다(유닛의 피해는 `DamageEvents.h`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Event/EventType.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 액션 룸을 클리어했음을 알립니다(`ActionRoom` 에서 마지막 적이 쓰러진 프레임에 냅니다). */
    struct SW_KIT_API RoomClearedEvent final : EventBase
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
    struct SW_KIT_API PlayerDefeatedInRoomEvent final : EventBase
    {
        string _returnMapPath; ///< 복귀할 오버월드 맵
        SW_DECLARE_GAMEPLAY_EVENT( PlayerDefeatedInRoomEvent );
    };
} // namespace sw

namespace sw
{
    /** @brief 클리어 게이트의 잠금이 바뀌었음을 알립니다(`ActionRoom` 에서 전투를 시작할 때 닫히고, 클리어하거나 패배하면 열립니다). */
    struct SW_KIT_API ClearGateStateChangedEvent final : EventBase
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
