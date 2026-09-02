/**
 * @file OverworldEvents.h
 * @brief 오버월드 타일맵 이동 및 워프 이벤트
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Event/EventType.h"

namespace sw
{
    /** @brief 맵 이동을 요청합니다. */
    struct WarpRequestedEvent final : IEvent
    {
        string _mapPath;     ///< 대상 맵 (Resource 상대)
        int32  _spawnX{ 1 }; ///< 스폰 타일 X
        int32  _spawnY{ 1 }; ///< 스폰 타일 Y
        SW_DECLARE_GAMEPLAY_EVENT( WarpRequestedEvent );
    };

    /** @brief 클리어 게이트 등으로 맵 이동이 막혔음을 알립니다. */
    struct WarpBlockedEvent final : IEvent
    {
        string _zoneId; ///< 활성 존 ID
        string _reason; ///< 차단 이유
        SW_DECLARE_GAMEPLAY_EVENT( WarpBlockedEvent );
    };

    /** @brief 맵 이동이 끝났음을 알립니다. */
    struct WarpCompletedEvent final : IEvent
    {
        string _mapPath;     ///< 도착 맵
        int32  _spawnX{ 1 }; ///< 스폰 타일 X
        int32  _spawnY{ 1 }; ///< 스폰 타일 Y
        SW_DECLARE_GAMEPLAY_EVENT( WarpCompletedEvent );
    };
} // namespace sw
