/**
 * @file NativeSocketAddress.h
 * @brief `NetAddress`(호스트 바이트 순서) ↔ OS `sockaddr_in`(네트워크 바이트 순서) 변환 하나입니다 — 소켓 구현 파일(`PlatformSocketUtil` · IOCP · epoll)만 include 한다.
 * @details 변환이 구현 파일마다 있으면 바이트 순서를 한쪽만 바꾸는 결함이 난다. OS 헤더를 드러내므로 공개 헤더(`PlatformSocketUtil.h` 등)는 이 파일을 include 하지 않는다.
 */
#pragma once
#include "Core/Network/NetTypes.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include <WinSock2.h>
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
#endif

namespace sw
{
    /** @struct NativeSocketAddress @brief IPv4 주소 변환(전부 static). 파일 머리말 참고. */
    struct NativeSocketAddress
    {
        /** @brief @p address 를 OS 주소로 바꿉니다. */
        static sockaddr_in toNative( const NetAddress& address )
        {
            sockaddr_in native{};
            native.sin_family      = AF_INET;
            native.sin_port        = htons( address._port );
            native.sin_addr.s_addr = htonl( address._ipv4 );
            return native;
        }

        /** @brief OS 주소 @p native 를 `NetAddress` 로 바꿉니다. */
        static NetAddress fromNative( const sockaddr_in& native ) { return NetAddress{ ntohl( native.sin_addr.s_addr ), ntohs( native.sin_port ) }; }
    };
} // namespace sw
