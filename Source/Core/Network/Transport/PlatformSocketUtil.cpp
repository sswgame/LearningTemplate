#include "pch.h"

#include "Core/Network/Transport/PlatformSocketUtil.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include <WinSock2.h>
    #include <WS2tcpip.h>
#else
    #include <arpa/inet.h>
    #include <cerrno>
    #include <fcntl.h>
    #include <netinet/in.h>
    #include <poll.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace sw
{
    namespace
    {
        struct PlatformSocketUtilInternal
        {
#if defined( SW_PLATFORM_WINDOWS )
            using NativeSocket                           = SOCKET;
            using SocketLength                           = int32;
            static constexpr NativeSocket kNativeInvalid = INVALID_SOCKET;
            static bool                   isWouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; } // WSAECONNRESET(ICMP 도달 불가)은 오류로 — 다음 데이터그램을 계속 본다
#else
            using NativeSocket                           = int32;
            using SocketLength                           = socklen_t;
            static constexpr NativeSocket kNativeInvalid = -1;
            static bool                   isWouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }
#endif
            static NativeSocket toNative( uint64 socketHandle ) { return static_cast<NativeSocket>( socketHandle ); }

            static sockaddr_in makeAddress( const NetAddress& address )
            {
                sockaddr_in native{};
                native.sin_family      = AF_INET;
                native.sin_port        = htons( address._port );
                native.sin_addr.s_addr = htonl( address._ipv4 );
                return native;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool PlatformSocketUtil::initialize()
    {
#if defined( SW_PLATFORM_WINDOWS )
        static const bool kbInitialized = []()
        {
            WSADATA data{};
            return WSAStartup( MAKEWORD( 2, 2 ), &data ) == 0;
        }();
        return kbInitialized;
#else
        return true;
#endif
    }

    uint64 PlatformSocketUtil::openUdpSocket( uint16 port )
    {
        if ( initialize() == false )
            return kInvalidSocket;
        const PlatformSocketUtilInternal::NativeSocket native = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
        if ( native == PlatformSocketUtilInternal::kNativeInvalid )
            return kInvalidSocket;
        sockaddr_in local{};
        local.sin_family      = AF_INET;
        local.sin_port        = htons( port );
        local.sin_addr.s_addr = htonl( INADDR_ANY );
        bool bOk              = bind( native, reinterpret_cast<const sockaddr*>( &local ), sizeof( local ) ) == 0;
        // 서버는 한 틱에 여러 연결의 패킷이 몰린다 — 기본 버퍼(리눅스 208 KB · Windows 64 KB)가 넘치면 커널이 조용히 버린다. 실패해도 계속한다.
        const int32 bufferSize = 1 << 20;
        (void)setsockopt( native, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const utf8*>( &bufferSize ), sizeof( bufferSize ) );
        (void)setsockopt( native, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const utf8*>( &bufferSize ), sizeof( bufferSize ) );
#if defined( SW_PLATFORM_WINDOWS )
        u_long nonBlocking = 1;
        bOk                = bOk && ioctlsocket( native, static_cast<int32>( FIONBIO ), &nonBlocking ) == 0;
        // Windows 는 저쪽 포트가 닫혀 ICMP 가 오면 다음 recvfrom 을 WSAECONNRESET 으로 실패시킨다 — UDP 서버는 그것을 끈다.
        BOOL  bReportReset  = FALSE;
        DWORD bytesReturned = 0;
        (void)WSAIoctl( native, _WSAIOW( IOC_VENDOR, 12 ), &bReportReset, sizeof( bReportReset ), nullptr, 0, &bytesReturned, nullptr, nullptr );
#else
        const int32 flags = fcntl( native, F_GETFL, 0 );
        bOk               = bOk && flags >= 0 && fcntl( native, F_SETFL, flags | O_NONBLOCK ) == 0;
#endif
        if ( bOk == false )
        {
            closeSocket( static_cast<uint64>( native ) );
            return kInvalidSocket;
        }
        return static_cast<uint64>( native );
    }

    void PlatformSocketUtil::closeSocket( uint64 socketHandle )
    {
        if ( socketHandle == kInvalidSocket )
            return;
#if defined( SW_PLATFORM_WINDOWS )
        closesocket( PlatformSocketUtilInternal::toNative( socketHandle ) );
#else
        close( PlatformSocketUtilInternal::toNative( socketHandle ) );
#endif
    }

    uint16 PlatformSocketUtil::getBoundPort( uint64 socketHandle )
    {
        sockaddr_in                              local{};
        PlatformSocketUtilInternal::SocketLength length = sizeof( local );
        if ( getsockname( PlatformSocketUtilInternal::toNative( socketHandle ), reinterpret_cast<sockaddr*>( &local ), &length ) != 0 )
            return 0;
        return ntohs( local.sin_port );
    }

    bool PlatformSocketUtil::sendTo( uint64 socketHandle, const NetAddress& to, const uint8* pData, int32 size )
    {
        const sockaddr_in native = PlatformSocketUtilInternal::makeAddress( to );
        const auto        sent   = sendto( PlatformSocketUtilInternal::toNative( socketHandle ), reinterpret_cast<const utf8*>( pData ), size, 0,
                                           reinterpret_cast<const sockaddr*>( &native ), sizeof( native ) );
        return sent == size;
    }

    SocketReceiveResult PlatformSocketUtil::receiveFrom( uint64 socketHandle, NetAddress& outFrom, uint8* pBuffer, int32 bufferSize, int32& outSize )
    {
        sockaddr_in                              from{};
        PlatformSocketUtilInternal::SocketLength length   = sizeof( from );
        const auto                               received = recvfrom( PlatformSocketUtilInternal::toNative( socketHandle ), reinterpret_cast<utf8*>( pBuffer ),
                                                                      bufferSize, 0, reinterpret_cast<sockaddr*>( &from ), &length );
        if ( received < 0 )
            return PlatformSocketUtilInternal::isWouldBlock() ? SocketReceiveResult::WouldBlock : SocketReceiveResult::Error;
        outFrom._ipv4 = ntohl( from.sin_addr.s_addr );
        outFrom._port = ntohs( from.sin_port );
        outSize       = static_cast<int32>( received );
        return SocketReceiveResult::Received;
    }

    bool PlatformSocketUtil::waitReadable( uint64 socketHandle, int32 timeoutMilli )
    {
        if ( socketHandle == kInvalidSocket )
            return false;
#if defined( SW_PLATFORM_WINDOWS )
        WSAPOLLFD pollEntry{};
        pollEntry.fd     = PlatformSocketUtilInternal::toNative( socketHandle );
        pollEntry.events = POLLRDNORM;
        return WSAPoll( &pollEntry, 1, timeoutMilli ) > 0 && ( pollEntry.revents & ( POLLRDNORM | POLLERR | POLLHUP ) ) != 0;
#else
        pollfd pollEntry{};
        pollEntry.fd     = PlatformSocketUtilInternal::toNative( socketHandle );
        pollEntry.events = POLLIN;
        return poll( &pollEntry, 1, timeoutMilli ) > 0 && ( pollEntry.revents & ( POLLIN | POLLERR | POLLHUP ) ) != 0;
#endif
    }
} // namespace sw
