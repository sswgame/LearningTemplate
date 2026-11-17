/**
 * @file NativeSocketAddress.h
 * @brief `NetAddress`(주소군 + 16 바이트) ↔ OS 소켓 주소(`sockaddr_in` · `sockaddr_in6`) 변환 하나입니다 — 소켓 구현 파일(`PlatformSocketUtil` · IOCP · epoll)만 include 한다.
 * @details 변환이 구현 파일마다 있으면 바이트 순서 · 주소군을 한쪽만 바꾸는 결함이 난다. OS 헤더를 드러내므로 공개 헤더(`PlatformSocketUtil.h` 등)는 이 파일을 include 하지 않는다.
 *          받는 쪽은 `sockaddr_storage` 로 받아 주소군을 보고 읽는다 — IPv4 사상 IPv6(이중 스택 소켓이 받은 IPv4 상대)는 IPv4 로 되돌린다(같은 상대가 두 주소로 보이지 않게).
 */
#pragma once
#include "Core/Network/NetTypes.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include <WinSock2.h>
    #include <WS2tcpip.h>
#elif defined( SW_PLATFORM_POSIX )
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
#else
    #error "Unsupported platform"
#endif

#include <cstring>

namespace sw
{
    /** @struct NativeSocketAddress @brief OS 소켓 주소 하나입니다(보낼 · 묶을 주소). 파일 머리말 참고. */
    struct NativeSocketAddress
    {
        sockaddr_storage _storage{};
        int32            _length{ 0 }; ///< 쓰인 주소 구조의 바이트 수(`sockaddr_in` 또는 `sockaddr_in6`)

        const sockaddr* get() const { return reinterpret_cast<const sockaddr*>( &_storage ); }
        sockaddr*       get() { return reinterpret_cast<sockaddr*>( &_storage ); }

        /** @brief 주소군의 OS 값(AF_INET · AF_INET6)입니다. */
        static int32 toNativeFamily( NetAddressFamily family ) { return family == NetAddressFamily::Ipv6 ? AF_INET6 : AF_INET; }

        /** @brief @p address 를 OS 주소로 바꿉니다. 주소군은 @p address 의 것이다 — 이중 스택 IPv6 소켓에 IPv4 상대를 보내려면 부르는 쪽이 `NetAddress::toIpv4Mapped` 를 먼저 한다. */
        static NativeSocketAddress toNative( const NetAddress& address )
        {
            NativeSocketAddress native;
            if ( address.isIpv6() )
            {
                sockaddr_in6& ipv6 = reinterpret_cast<sockaddr_in6&>( native._storage );
                ipv6.sin6_family   = AF_INET6;
                ipv6.sin6_port     = htons( address._port );
                std::memcpy( &ipv6.sin6_addr, address._arrByte, NetAddress::kByteCount );
                native._length = static_cast<int32>( sizeof( sockaddr_in6 ) );
                return native;
            }
            sockaddr_in& ipv4 = reinterpret_cast<sockaddr_in&>( native._storage );
            ipv4.sin_family   = AF_INET;
            ipv4.sin_port     = htons( address._port );
            std::memcpy( &ipv4.sin_addr, address._arrByte, 4 ); // 바이트가 이미 네트워크 순서다
            native._length = static_cast<int32>( sizeof( sockaddr_in ) );
            return native;
        }

        /** @brief OS 주소 @p native 를 `NetAddress` 로 바꿉니다. IPv4 사상 IPv6 는 IPv4 로 되돌리고, 모르는 주소군이면 무효 주소(포트 0)입니다. */
        static NetAddress fromNative( const sockaddr_storage& native )
        {
            if ( native.ss_family == AF_INET6 )
            {
                const sockaddr_in6& ipv6 = reinterpret_cast<const sockaddr_in6&>( native );
                uint8               arrByte[NetAddress::kByteCount];
                std::memcpy( arrByte, &ipv6.sin6_addr, NetAddress::kByteCount );
                return NetAddress::makeIpv6( arrByte, ntohs( ipv6.sin6_port ) ).toUnmapped();
            }
            if ( native.ss_family == AF_INET )
            {
                const sockaddr_in& ipv4 = reinterpret_cast<const sockaddr_in&>( native );
                uint8              arrOctet[4];
                std::memcpy( arrOctet, &ipv4.sin_addr, 4 );
                return NetAddress::make( arrOctet[0], arrOctet[1], arrOctet[2], arrOctet[3], ntohs( ipv4.sin_port ) );
            }
            return NetAddress{};
        }
    };
} // namespace sw
