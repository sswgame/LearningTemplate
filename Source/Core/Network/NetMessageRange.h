/**
 * @file NetMessageRange.h
 * @brief 메시지 첫 바이트(종류)의 영역 나눔을 정의합니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @brief 메시지 첫 바이트(종류)의 영역입니다. 네트워크 키트끼리, 그리고 게임 메시지와 섞이지 않게 나눕니다. 한 게임이 키트 둘을 함께 써도 됩니다.
     *        키트의 `handleMessage` 는 자기 영역의 메시지만 처리하고 나머지는 false 를 반환합니다.
     */
    struct NetMessageRange
    {
        static constexpr uint8 kSize      = 0x10; ///< 영역 하나의 메시지 수.
        static constexpr uint8 kEngine    = 0x00; ///< 엔진의 게임 세션(`GameNetSession`)이 쓰는 0x00..0x0F. 종류는 Engine 의 `GameNetSessionMessage` 에 있다.
        static constexpr uint8 kFramework = 0x10; ///< 프레임워크(네트워크 키트)가 쓰는 0x10..0xAF(10 영역). 키트별 영역은 GameFramework 의 `NetKitMessageRange` 에 있다.
        static constexpr uint8 kRuntime   = 0xB0; ///< 키트가 아닌 게임 프레임워크 바탕(`GameFramework/Runtime`)이 쓰는 0xB0..0xBF — 하위 레벨 미니게임의 복제
        static constexpr uint8 kGame      = 0xC0; ///< 게임이 쓰는 메시지 0xC0..0xFF(4 영역). 키트 영역이 0x7F 에서 찼다(하우징이 여덟째) — 게임은 영역 하나 · 둘을 쓰고 키트는 계속 늘어 키트 쪽을 넓혔다.
        /**
         * @brief 라우터(`NetMessageRouter`)가 쓰는 종류 하나(엔진 영역의 마지막 0x0F) — 처리기는 맡지 못한다.
         * @details 영역마다 쓰는 메시지 표의 와이어 판(`INetMessageHandler::getWireVersion`)을 상대와 맞춰 보는 알림이다. 몸: 수(varuint) · (영역 8 비트, 판 32 비트) 들.
         */
        static constexpr uint8 kRouterWireVersionKind = kEngine + kSize - 1;
        /** @brief 라우터가 쓰는 또 한 종류(엔진 영역 0x0D, 서버 → 클라이언트) — 서버의 월드 세대 알림이다(`NetMessageRouter::openWorld`). 몸: 세대 8 비트 · 씬 번호(varuint). 처리기는 맡지 못한다. */
        static constexpr uint8 kRouterWorldKind = kEngine + kSize - 3;

        /** @brief 라우터가 쓰는 또 한 종류(엔진 영역 0x0C, 양방향) — 보류한 메시지를 끝내 전하지 못했다고 보낸 쪽에 알린다. 몸: 종류 8 비트 · 까닭 2 비트 · 보류 열쇠(varuint). */
        static constexpr uint8 kRouterDeliveryFailedKind = kEngine + kSize - 4;

        /** @brief 라우터가 쓰는 또 한 종류(엔진 영역 0x0B, 서버 → 클라이언트, 비신뢰) — 월드의 서버 시계 알림이다(`NetMessageRouter::advanceWorldClock`). 몸: 세대 8 비트 · 서버 틱(varuint). */
        static constexpr uint8 kRouterClockKind = kEngine + kSize - 5;

        /** @brief 라우터가 쓰는 또 한 종류(엔진 영역 0x0A, 클라이언트 → 서버) — 클라이언트가 서버의 월드에 들어왔다(세대) · 나갔다(0)(`NetMessageRouter::setLocalScene`). 몸: 세대 8 비트. */
        static constexpr uint8 kRouterPresenceKind = kEngine + kSize - 6;

        /** @brief 라우터가 스스로 읽는 종류면 true 입니다. 처리기 · 메시지 표가 쓰지 못한다. */
        static constexpr bool isRouterKind( int32 kind ) { return kind == kRouterWireVersionKind || kind == kRouterWorldKind || kind == kRouterDeliveryFailedKind || kind == kRouterClockKind || kind == kRouterPresenceKind; }

        static constexpr bool isInRange( uint8 kind, uint8 rangeBase ) { return kind >= rangeBase && kind < rangeBase + kSize; }
    };
} // namespace sw
