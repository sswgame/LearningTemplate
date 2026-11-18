/**
 * @file StreamReceiveHold.h
 * @brief 읽기를 멈출 수 없는 전송(브라우저 WebSocket)의 받는 쪽 배압 — 멈춘 동안 받은 바이트를 들고 있다가 다시 열면 `pollIO` 가 넘긴다.
 * @details OS 소켓은 읽기를 멈추면 TCP 윈도우가 닫혀 상대가 멈추지만, 브라우저 WebSocket 에는 흐름 제어가 없다(WebSocketStream 은 Chromium 전용이다).
 *          그래서 전송이 위 층 대신 바이트를 들고, 위 층의 `setReceivePaused` 계약(멈춘 동안 `onStreamReceived` 가 오지 않는다)을 지킨다. 들고 있는 양이
 *          상한을 넘으면 `Overflow` 다 — 전송은 그 연결을 닫는다. 잠금은 소유자(연결 칸)가 잡는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief `StreamReceiveHold::accept` 의 결과입니다. */
    enum class StreamReceiveHoldResult : uint8
    {
        Deliver = 0, ///< 멈추지 않았고 든 것이 없다 — 받은 바이트를 바로 위 층에 넘긴다
        Held,        ///< 들었다(멈췄거나, 먼저 든 것이 아직 넘어가지 않아 순서를 지킨다)
        Overflow     ///< 상한을 넘었다 — 들지 않았다, 연결을 닫는다
    };

    class SW_API StreamReceiveHold
    {
    public:
        StreamReceiveHold();

        void setPaused( bool bPaused ) { _bPaused = bPaused ? SW_TRUE : SW_FALSE; }
        bool isPaused() const { return _bPaused == SW_TRUE; }

        /** @brief 받은 바이트를 판정합니다. 들 때는 복사합니다. @p maxHeldBytes 를 넘게 되면 `Overflow` 입니다. */
        [[nodiscard]] StreamReceiveHoldResult accept( const uint8* pData, int32 size, int32 maxHeldBytes );
        /** @brief 멈추지 않았고 든 것이 있으면 그것을 꺼내 돌려줍니다(받은 순서 그대로 이어 붙인 바이트). 아니면 빈 벡터입니다. */
        [[nodiscard]] vector<uint8> takeReleasable();
        void                        clear();

        int32 getHeldBytes() const { return static_cast<int32>( _heldBytes.size() ); }

    private:
        vector<uint8> _heldBytes;
        uint8         _bPaused;
    };
} // namespace sw
