/**
 * @file PlatformSocketUtil.h
 * @brief UDP 소켓 원시 연산 — 이름만 다른 플랫폼 함수(WSAStartup · closesocket · ioctlsocket ↔ close · fcntl)를 이 한 곳에서 가립니다.
 * @details Core README 의 규칙("함수 이름만 다르면 *Util 한 곳")을 따릅니다. 소켓 핸들은 `uint64` 로 들고 다닙니다(Windows SOCKET 은 포인터 크기).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Network/NetTypes.h"

namespace sw
{
    /** @brief 수신 결과입니다. */
    enum class SocketReceiveResult : uint8
    {
        Received = 0,
        WouldBlock, ///< 받을 것이 없다
        Error
    };

    struct SW_API PlatformSocketUtil
    {
        static constexpr uint64 kInvalidSocket = ~0ull;

        /** @brief 소켓 라이브러리를 엽니다(Windows 만 실제로 한다). 여러 번 불러도 됩니다. */
        [[nodiscard]] static bool initialize();
        /** @brief 논블로킹 UDP 소켓을 @p port 에 엽니다(0 = 아무 포트). 실패하면 `kInvalidSocket` 입니다. */
        static uint64 openUdpSocket( uint16 port );
        static void   closeSocket( uint64 socketHandle );
        /** @brief 실제로 묶인 포트입니다(0 으로 열었을 때). */
        static uint16              getBoundPort( uint64 socketHandle );
        [[nodiscard]] static bool  sendTo( uint64 socketHandle, const NetAddress& to, const uint8* pData, int32 size );
        static SocketReceiveResult receiveFrom( uint64 socketHandle, NetAddress& outFrom, uint8* pBuffer, int32 bufferSize, int32& outSize );
    };
} // namespace sw
