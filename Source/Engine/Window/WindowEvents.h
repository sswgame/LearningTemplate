/**
 * @file WindowEvents.h
 * @brief 창 이벤트(크기 · 닫기 · 활성)입니다. 이벤트 버스(`EventDispatcher`)에 싣는 값 타입입니다.
 * @details 예전에는 Core 의 `Core/Event/EventType.h` 에 있었습니다. 창은 Engine 의 Window 층 개념이라 여기로 옮겼습니다. 엔진 예약
 *          ID(`kEventWindow*`)는 번호가 겹치지 않도록 한곳에서 보게 Core 의 표에 남아 있습니다.
 * @note 지금 엔진의 창은 이것을 발행하지 않습니다 — 크기 · 닫기는 `IWindow` 의 델리게이트(`setResizeCallback` ·
 *       `setCloseQueryHandler`)로 알립니다. 이벤트 버스로 창 상태를 받고 싶은 쪽을 위한 타입입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Event/EventType.h"

namespace sw
{
    /** @brief 클라이언트 영역 크기와 리사이즈 · 최대화 · 최소화 플래그입니다. */
    struct SW_API WindowResizeEvent final : IEvent
    {
        int32                  _width;
        int32                  _height;
        uint8                  _bIsResizing   : 1;
        uint8                  _bIsMaximized  : 1;
        uint8                  _bIsMinimized  : 1;
        [[maybe_unused]] uint8 _reservedFlags : 5;

        /** @brief 크기는 0, 플래그는 모두 꺼진 상태로 둡니다. */
        WindowResizeEvent() noexcept;

        SW_REGISTER_ENGINE_EVENT( WindowResize );
    };

    /** @brief 창 닫기 요청입니다. 페이로드는 없습니다. */
    struct SW_API WindowCloseEvent final : IEvent
    {
        SW_REGISTER_ENGINE_EVENT( WindowClose );
    };

    /** @brief 활성 · 비활성 전환입니다. */
    struct SW_API WindowActivateEvent final : IEvent
    {
        uint8                  _bIsActivate   : 1;
        [[maybe_unused]] uint8 _reservedFlags : 7;

        /** @brief 비활성으로 둡니다. */
        WindowActivateEvent() noexcept;

        SW_REGISTER_ENGINE_EVENT( WindowActivate );
    };
} // namespace sw
