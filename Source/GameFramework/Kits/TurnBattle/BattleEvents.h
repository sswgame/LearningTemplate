/**
 * @file BattleEvents.h
 * @brief 턴제 전투 수명주기 이벤트
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Event/EventType.h"

namespace sw
{
    /** @brief 전투 시작을 요청합니다. */
    struct BattleRequestedEvent final : IEvent
    {
        string _fromMapPath; ///< 전투 직전 오버월드 맵
        SW_DECLARE_GAMEPLAY_EVENT( BattleRequestedEvent );
    };

    /** @brief 전투가 끝났음을 알립니다. */
    struct BattleEndedEvent final : IEvent
    {
        uint8                  _bPlayerWon : 1; ///< 플레이어 승리
        [[maybe_unused]] uint8 _reserved   : 7;

        /** @brief 패배·예약 비트를 0으로 둡니다. */
        BattleEndedEvent() noexcept
            : _bPlayerWon{ SW_FALSE }
            , _reserved{ 0 } {}

        SW_DECLARE_GAMEPLAY_EVENT( BattleEndedEvent );
    };
} // namespace sw
