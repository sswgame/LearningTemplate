/**
 * @file GameEventUtil.h
 * @brief 게임플레이 이벤트를 "game" 채널에 내는 한 길입니다 — 버스 스레드면 그 자리에서, 아니면 큐로.
 */
#pragma once
#include "Core/Event/EventDispatcher.h"

#include "GameFramework/Base/GameEvents.h"
#include "GameFramework/Base/GameService.h"

namespace sw
{
    /**
     * @struct GameEventUtil
     * @brief 게임플레이 이벤트를 "game" 채널(`gameEventChannel`)에 냅니다.
     * @details 언리얼의 델리게이트(`OnTakeAnyDamage` · GameMode 의 브로드캐스트)처럼 **그 자리에서** 알리는 것이 기본입니다(`publish`) — HP 바 ·
     *          피해 숫자가 같은 프레임에 받습니다. 버스(구독 · 발행)는 큐를 비우는 스레드 전용이라(`EventDispatcher::isBusThread`), 다른 스레드에서
     *          불리면 큐에 싣습니다(`push`, 다음 `processEvents`).
     *          이벤트 버스가 붙지 않은 프로세스(도구 · 시험)에서는 아무것도 하지 않습니다.
     */
    struct GameEventUtil
    {
        template <typename TEvent>
        static void send( const TEvent& event )
        {
            EventDispatcher* pDispatcher = game::getService<EventDispatcher>();
            if ( pDispatcher == nullptr )
                return;
            if ( pDispatcher->isBusThread() )
                pDispatcher->publish( gameEventChannel(), event );
            else
                pDispatcher->push( gameEventChannel(), event );
        }
    };
} // namespace sw
