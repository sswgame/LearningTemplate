/**
 * @file NetConnectionState.h
 * @brief 연결 상태 · 닫기 전 비우는 시간 · 끊긴 까닭을 정의합니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 연결 상태입니다. */
    enum class NetConnectionState : uint8
    {
        Disconnected = 0,
        Connecting, ///< 클라이언트가 요청과 챌린지 응답을 보내는 중이다.
        Connected,
        Disconnecting ///< 닫는 중이다 — 확인되지 않은 신뢰 메시지만 비운다(`NetLinger`). 새로 보내지도 받지도 않는다
    };

    /**
     * @class NetLinger
     * @brief 연결을 닫기 전에 확인되지 않은 신뢰 메시지를 비울 시간입니다. 닫는 쪽이 늘 고릅니다(기본값이 없다).
     * @details 바로 닫으면 방금 보낸 신뢰 메시지(거절 까닭 · 저장 결과)가 손실 하나로 사라진다. Valve GameNetworkingSockets `CloseConnection` 의 `bEnableLinger` · 소켓 `SO_LINGER` 자리.
     */
    class NetLinger
    {
    public:
        /** @brief 비우지 않고 바로 닫습니다(상대가 프로토콜을 어겼다 · 보낼 것이 없다). */
        static constexpr NetLinger none() { return NetLinger{ 0.0 }; }
        /** @brief 확인되지 않은 신뢰 메시지가 모두 확인되거나 @p seconds 가 지나면 닫습니다. 그동안 새로 보내지도 받지도 않는다. */
        static constexpr NetLinger waitUpTo( float64 seconds ) { return NetLinger{ seconds }; }

        constexpr float64 getSeconds() const { return _seconds; }

    private:
        constexpr explicit NetLinger( float64 seconds )
            : _seconds{ seconds }
        {
        }

    private:
        float64 _seconds;
    };

    /** @brief 연결이 끊긴 까닭입니다. */
    enum class NetDisconnectReason : uint8
    {
#define SW_NET_DISCONNECT_REASON( Name ) Name,
#include "Core/Network/NetDisconnectReasonList.xxx"
#undef SW_NET_DISCONNECT_REASON
        Count ///< 사유가 아니라 개수다 — 서버 지표가 사유마다 시리즈를 이 값까지 만든다(`GameNetSession`). 사유는 표(`NetDisconnectReasonList.xxx`)에 줄로 더하고, 이름 표도 그 줄에서 나온다.
    };

    SW_API const utf8* toString( NetDisconnectReason reason );
} // namespace sw
