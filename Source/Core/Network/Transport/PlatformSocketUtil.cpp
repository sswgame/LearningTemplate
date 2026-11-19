#include "pch.h"

#include "Core/Network/Transport/PlatformSocketUtil.h"

#include "Core/Network/Transport/NativeSocketAddress.h"
#include "Core/String/StringUtil.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include <WinSock2.h>
    #include <WS2tcpip.h>
#elif defined( SW_PLATFORM_POSIX )
    #include <arpa/inet.h>
    #include <cerrno>
    #include <fcntl.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <poll.h>
    #include <sys/socket.h>
    #include <sys/uio.h>
    #include <unistd.h>
#else
    #error "Unsupported platform"
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
#elif defined( SW_PLATFORM_POSIX )
            using NativeSocket                           = int32;
            using SocketLength                           = socklen_t;
            static constexpr NativeSocket kNativeInvalid = -1;
            static bool                   isWouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }
#else
    #error "Unsupported platform"
#endif
            static NativeSocket toNative( uint64 socketHandle ) { return static_cast<NativeSocket>( socketHandle ); }
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
#elif defined( SW_PLATFORM_POSIX )
        return true;
#else
    #error "Unsupported platform"
#endif
    }

    uint64 PlatformSocketUtil::openUDPSocket( uint16 port, bool bLoopbackOnly, UDPPortSharing sharing, NetAddressFamily family )
    {
        if ( initialize() == false )
            return kInvalidSocket;
#if defined( SW_PLATFORM_WINDOWS )
        if ( sharing == UDPPortSharing::ReusePort )
            return kInvalidSocket; // Windows 에는 커널이 나눠 주는 포트 공유가 없다(SO_REUSEADDR 는 뜻이 다르다)
#endif
        const PlatformSocketUtilInternal::NativeSocket native = socket( NativeSocketAddress::toNativeFamily( family ), SOCK_DGRAM, IPPROTO_UDP );
        if ( native == PlatformSocketUtilInternal::kNativeInvalid )
            return kInvalidSocket;
#if defined( SW_PLATFORM_POSIX )
        if ( sharing == UDPPortSharing::ReusePort )
        {
            const int32 reuse = 1;
            if ( setsockopt( native, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof( reuse ) ) != 0 )
            {
                closeSocket( static_cast<uint64>( native ) );
                return kInvalidSocket;
            }
        }
#endif
        bool bOk = true;
        if ( family == NetAddressFamily::Ipv6 )
        {
            // 이중 스택 — `::` 에 묶은 소켓이 IPv4 상대도 받는다(언리얼 · 유니티 Transport 의 기본과 같다). Windows 기본값은 켜짐이라 명시해 끈다.
            const int32 v6Only = 0;
            bOk                = setsockopt( native, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const utf8*>( &v6Only ), sizeof( v6Only ) ) == 0;
        }
        const NetAddress          bindAddress = family == NetAddressFamily::Ipv6 ? ( bLoopbackOnly ? NetAddress::makeIpv6Loopback( port ) : NetAddress::makeIpv6AnyInterface( port ) )
                                                                                 : ( bLoopbackOnly ? NetAddress::makeLoopback( port ) : NetAddress::makeAnyInterface( port ) );
        const NativeSocketAddress local       = NativeSocketAddress::toNative( bindAddress );
        bOk                                   = bOk && bind( native, local.get(), static_cast<PlatformSocketUtilInternal::SocketLength>( local._length ) ) == 0;
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
#elif defined( SW_PLATFORM_POSIX )
        const int32 flags = fcntl( native, F_GETFL, 0 );
        bOk               = bOk && flags >= 0 && fcntl( native, F_SETFL, flags | O_NONBLOCK ) == 0;
#else
    #error "Unsupported platform"
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
#elif defined( SW_PLATFORM_POSIX )
        close( PlatformSocketUtilInternal::toNative( socketHandle ) );
#else
    #error "Unsupported platform"
#endif
    }

    uint16 PlatformSocketUtil::getBoundPort( uint64 socketHandle )
    {
        sockaddr_storage                         local{};
        PlatformSocketUtilInternal::SocketLength length = sizeof( local );
        if ( getsockname( PlatformSocketUtilInternal::toNative( socketHandle ), reinterpret_cast<sockaddr*>( &local ), &length ) != 0 )
            return 0;
        return NativeSocketAddress::fromNative( local )._port;
    }

    bool PlatformSocketUtil::sendTo( uint64 socketHandle, const NetAddress& to, const uint8* pData, int32 size )
    {
        const NativeSocketAddress native = NativeSocketAddress::toNative( to );
        const auto                sent   = sendto( PlatformSocketUtilInternal::toNative( socketHandle ), reinterpret_cast<const utf8*>( pData ), size, 0, native.get(),
                                                   static_cast<PlatformSocketUtilInternal::SocketLength>( native._length ) );
        return sent == size;
    }

    SocketReceiveResult PlatformSocketUtil::receiveFrom( uint64 socketHandle, NetAddress& outFrom, uint8* pBuffer, int32 bufferSize, int32& outSize )
    {
        sockaddr_storage                         from{};
        PlatformSocketUtilInternal::SocketLength length   = sizeof( from );
        const auto                               received = recvfrom( PlatformSocketUtilInternal::toNative( socketHandle ), reinterpret_cast<utf8*>( pBuffer ),
                                                                      bufferSize, 0, reinterpret_cast<sockaddr*>( &from ), &length );
        if ( received < 0 )
            return PlatformSocketUtilInternal::isWouldBlock() ? SocketReceiveResult::WouldBlock : SocketReceiveResult::Error;
        outFrom = NativeSocketAddress::fromNative( from );
        outSize = static_cast<int32>( received );
        return SocketReceiveResult::Received;
    }

    int32 PlatformSocketUtil::sendBatch( uint64 socketHandle, const NetDatagramView* pViews, int32 count )
    {
        const int32 batchCount = count < kMaxBatch ? count : kMaxBatch;
#if defined( SW_PLATFORM_WINDOWS ) || defined( SW_PLATFORM_WEB ) // 묶음 시스템 호출(sendmmsg · recvmmsg)이 없다 — 하나씩
        int32 sentCount = 0;
        for ( ; sentCount < batchCount; ++sentCount )
        {
            if ( sendTo( socketHandle, pViews[sentCount]._to, pViews[sentCount]._pData, pViews[sentCount]._size ) == false )
                break;
        }
        return sentCount;
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_ANDROID )
        NativeSocketAddress arrAddress[kMaxBatch]{};
        iovec               arrVector[kMaxBatch]{};
        mmsghdr             arrMessage[kMaxBatch]{};
        for ( int32 index = 0; index < batchCount; ++index )
        {
            arrAddress[index]                     = NativeSocketAddress::toNative( pViews[index]._to );
            arrVector[index].iov_base             = const_cast<uint8*>( pViews[index]._pData );
            arrVector[index].iov_len              = static_cast<size_t>( pViews[index]._size );
            arrMessage[index].msg_hdr.msg_name    = arrAddress[index].get();
            arrMessage[index].msg_hdr.msg_namelen = static_cast<socklen_t>( arrAddress[index]._length );
            arrMessage[index].msg_hdr.msg_iov     = &arrVector[index];
            arrMessage[index].msg_hdr.msg_iovlen  = 1;
        }
        int32 sentCount = 0;
        while ( sentCount < batchCount )
        {
            const int32 sent = sendmmsg( PlatformSocketUtilInternal::toNative( socketHandle ), arrMessage + sentCount, static_cast<uint32>( batchCount - sentCount ), 0 );
            if ( sent <= 0 )
                break;
            sentCount += sent;
        }
        return sentCount;
#else
    #error "Unsupported platform"
#endif
    }

    int32 PlatformSocketUtil::receiveBatch( uint64 socketHandle, SocketReceiveEntry* pEntries, int32 count )
    {
        const int32 batchCount = count < kMaxBatch ? count : kMaxBatch;
#if defined( SW_PLATFORM_WINDOWS ) || defined( SW_PLATFORM_WEB ) // 묶음 시스템 호출(sendmmsg · recvmmsg)이 없다 — 하나씩
        int32 receivedCount = 0;
        for ( int32 attempt = 0; receivedCount < batchCount && attempt < batchCount + kMaxBatch; ++attempt ) // 오류가 이어져도 끝난다(단건 받기의 시도 한도와 같다)
        {
            SocketReceiveEntry&       entry  = pEntries[receivedCount];
            const SocketReceiveResult result = receiveFrom( socketHandle, entry._from, entry._pBuffer, entry._bufferSize, entry._size );
            if ( result == SocketReceiveResult::WouldBlock )
                break;
            if ( result == SocketReceiveResult::Received )
                ++receivedCount; // 오류는 건너뛰고 다음 데이터그램을 본다(단건 받기와 같다)
        }
        return receivedCount;
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_ANDROID )
        sockaddr_storage arrAddress[kMaxBatch]{};
        iovec            arrVector[kMaxBatch]{};
        mmsghdr          arrMessage[kMaxBatch]{};
        for ( int32 index = 0; index < batchCount; ++index )
        {
            arrVector[index].iov_base             = pEntries[index]._pBuffer;
            arrVector[index].iov_len              = static_cast<size_t>( pEntries[index]._bufferSize );
            arrMessage[index].msg_hdr.msg_name    = &arrAddress[index];
            arrMessage[index].msg_hdr.msg_namelen = sizeof( sockaddr_storage );
            arrMessage[index].msg_hdr.msg_iov     = &arrVector[index];
            arrMessage[index].msg_hdr.msg_iovlen  = 1;
        }
        const int32 received = recvmmsg( PlatformSocketUtilInternal::toNative( socketHandle ), arrMessage, static_cast<uint32>( batchCount ), MSG_DONTWAIT, nullptr );
        if ( received <= 0 )
            return 0;
        for ( int32 index = 0; index < received; ++index )
        {
            pEntries[index]._from = NativeSocketAddress::fromNative( arrAddress[index] );
            pEntries[index]._size = static_cast<int32>( arrMessage[index].msg_len );
        }
        return received;
#else
    #error "Unsupported platform"
#endif
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
#elif defined( SW_PLATFORM_POSIX )
        pollfd pollEntry{};
        pollEntry.fd     = PlatformSocketUtilInternal::toNative( socketHandle );
        pollEntry.events = POLLIN;
        return poll( &pollEntry, 1, timeoutMilli ) > 0 && ( pollEntry.revents & ( POLLIN | POLLERR | POLLHUP ) ) != 0;
#else
    #error "Unsupported platform"
#endif
    }

    bool PlatformSocketUtil::waitReadable( const uint64* pSocketHandles, int32 count, int32 timeoutMilli )
    {
        if ( pSocketHandles == nullptr || count <= 0 || count > kMaxWaitSockets )
            return false;
#if defined( SW_PLATFORM_WINDOWS )
        WSAPOLLFD arrEntry[kMaxWaitSockets]{};
        for ( int32 index = 0; index < count; ++index )
        {
            if ( pSocketHandles[index] == kInvalidSocket )
                return false;
            arrEntry[index].fd     = PlatformSocketUtilInternal::toNative( pSocketHandles[index] );
            arrEntry[index].events = POLLRDNORM;
        }
        if ( WSAPoll( arrEntry, static_cast<ULONG>( count ), timeoutMilli ) <= 0 )
            return false;
        for ( int32 index = 0; index < count; ++index )
        {
            if ( ( arrEntry[index].revents & ( POLLRDNORM | POLLERR | POLLHUP ) ) != 0 )
                return true;
        }
        return false;
#elif defined( SW_PLATFORM_POSIX )
        pollfd arrEntry[kMaxWaitSockets]{};
        for ( int32 index = 0; index < count; ++index )
        {
            if ( pSocketHandles[index] == kInvalidSocket )
                return false;
            arrEntry[index].fd     = PlatformSocketUtilInternal::toNative( pSocketHandles[index] );
            arrEntry[index].events = POLLIN;
        }
        if ( poll( arrEntry, static_cast<nfds_t>( count ), timeoutMilli ) <= 0 )
            return false;
        for ( int32 index = 0; index < count; ++index )
        {
            if ( ( arrEntry[index].revents & ( POLLIN | POLLERR | POLLHUP ) ) != 0 )
                return true;
        }
        return false;
#else
    #error "Unsupported platform"
#endif
    }

    uint64 PlatformSocketUtil::connectTCPSocket( const NetAddress& to, int32 timeoutMilli )
    {
        if ( initialize() == false )
            return kInvalidSocket;
        const PlatformSocketUtilInternal::NativeSocket native = socket( NativeSocketAddress::toNativeFamily( to._family ), SOCK_STREAM, IPPROTO_TCP );
        if ( native == PlatformSocketUtilInternal::kNativeInvalid )
            return kInvalidSocket;
        const uint64 handle = static_cast<uint64>( native );
#if defined( SW_PLATFORM_WINDOWS )
        u_long     nonBlocking = 1;
        const bool bOk         = ioctlsocket( native, static_cast<int32>( FIONBIO ), &nonBlocking ) == 0;
        const auto isPending   = []()
        { return WSAGetLastError() == WSAEWOULDBLOCK; };
#elif defined( SW_PLATFORM_POSIX )
        const int32 flags     = fcntl( native, F_GETFL, 0 );
        const bool  bOk       = flags >= 0 && fcntl( native, F_SETFL, flags | O_NONBLOCK ) == 0;
        const auto  isPending = []()
        { return errno == EINPROGRESS; };
#else
    #error "Unsupported platform"
#endif
        const NativeSocketAddress remote = NativeSocketAddress::toNative( to );
        if ( bOk == false )
        {
            closeSocket( handle );
            return kInvalidSocket;
        }
        if ( connect( native, remote.get(), static_cast<PlatformSocketUtilInternal::SocketLength>( remote._length ) ) != 0 )
        {
            int32                                    socketError = 0;
            PlatformSocketUtilInternal::SocketLength errorLength = sizeof( socketError );
            const bool                               bConnected  = isPending() && waitWritable( handle, timeoutMilli ) &&
                                    getsockopt( native, SOL_SOCKET, SO_ERROR, reinterpret_cast<utf8*>( &socketError ), &errorLength ) == 0 && socketError == 0;
            if ( bConnected == false )
            {
                closeSocket( handle );
                return kInvalidSocket;
            }
        }
        return handle;
    }

    int32 PlatformSocketUtil::sendStream( uint64 socketHandle, const uint8* pData, int32 size )
    {
#if defined( SW_PLATFORM_WINDOWS )
        constexpr int32 kSendFlags = 0;
#elif defined( SW_PLATFORM_POSIX )
        constexpr int32 kSendFlags = MSG_NOSIGNAL; // 저쪽이 닫은 소켓에 쓰면 SIGPIPE 로 프로세스가 죽는다 — 오류로 받는다
#else
    #error "Unsupported platform"
#endif
        const auto sent = send( PlatformSocketUtilInternal::toNative( socketHandle ), reinterpret_cast<const utf8*>( pData ), size, kSendFlags );
        if ( sent < 0 )
            return PlatformSocketUtilInternal::isWouldBlock() ? 0 : kStreamSendFailed;
        return static_cast<int32>( sent );
    }

    SocketReceiveResult PlatformSocketUtil::receiveStream( uint64 socketHandle, uint8* pBuffer, int32 bufferSize, int32& outSize )
    {
        outSize             = 0;
        const auto received = recv( PlatformSocketUtilInternal::toNative( socketHandle ), reinterpret_cast<utf8*>( pBuffer ), bufferSize, 0 );
        if ( received < 0 )
            return PlatformSocketUtilInternal::isWouldBlock() ? SocketReceiveResult::WouldBlock : SocketReceiveResult::Error;
        outSize = static_cast<int32>( received );
        return SocketReceiveResult::Received;
    }

    bool PlatformSocketUtil::waitWritable( uint64 socketHandle, int32 timeoutMilli )
    {
        if ( socketHandle == kInvalidSocket )
            return false;
#if defined( SW_PLATFORM_WINDOWS )
        WSAPOLLFD pollEntry{};
        pollEntry.fd     = PlatformSocketUtilInternal::toNative( socketHandle );
        pollEntry.events = POLLWRNORM;
        return WSAPoll( &pollEntry, 1, timeoutMilli ) > 0 && ( pollEntry.revents & ( POLLWRNORM | POLLERR | POLLHUP ) ) != 0;
#elif defined( SW_PLATFORM_POSIX )
        pollfd pollEntry{};
        pollEntry.fd     = PlatformSocketUtilInternal::toNative( socketHandle );
        pollEntry.events = POLLOUT;
        return poll( &pollEntry, 1, timeoutMilli ) > 0 && ( pollEntry.revents & ( POLLOUT | POLLERR | POLLHUP ) ) != 0;
#else
    #error "Unsupported platform"
#endif
    }

    bool PlatformSocketUtil::resolveAddress( string_view hostAndPort, uint16 defaultPort, NetAddress& outAddress )
    {
        if ( NetAddress::parse( hostAndPort, defaultPort, outAddress ) )
            return true;
        const size_t      colon = hostAndPort.rfind( ':' );
        const string_view host  = hostAndPort.substr( 0, colon );
        int32             port  = defaultPort;
        if ( host.empty() )
            return false;
        const bool bBadPort = colon != string_view::npos && ( StringUtil::parseInt( hostAndPort.substr( colon + 1 ), port ) == false || port <= 0 || port > 65535 );
        if ( bBadPort || initialize() == false )
            return false;
        addrinfo hints{};
        hints.ai_family       = AF_UNSPEC;     // A · AAAA 둘 다 — IPv6 만 있는 망(NAT64)에서도 풀린다
        hints.ai_socktype     = SOCK_STREAM;   // 주소 하나에 소켓 종류마다 줄이 생기지 않게
        hints.ai_flags        = AI_ADDRCONFIG; // 이 기계에 그 주소군의 주소가 없으면 그 군을 돌려주지 않는다(IPv6 를 끈 기계가 AAAA 로 가지 않게)
        addrinfo*    pResult  = nullptr;
        const string hostText = string( host );
        if ( getaddrinfo( hostText.c_str(), nullptr, &hints, &pResult ) != 0 || pResult == nullptr )
            return false;
        // IPv4 가 있으면 IPv4 를 고른다 — 우리 서버의 기본 바인드가 127.0.0.1 · 0.0.0.0 이라 `localhost` 가 `::1` 로 풀리면(RFC 6724 순서) 붙지 못한다. 없으면 첫 IPv6 다.
        const addrinfo* pChosen = nullptr;
        for ( const addrinfo* pEntry = pResult; pEntry != nullptr; pEntry = pEntry->ai_next )
        {
            const bool bKnownFamily = pEntry->ai_family == AF_INET || pEntry->ai_family == AF_INET6;
            if ( bKnownFamily && ( pChosen == nullptr || ( pChosen->ai_family != AF_INET && pEntry->ai_family == AF_INET ) ) )
                pChosen = pEntry;
        }
        sockaddr_storage native{};
        if ( pChosen != nullptr )
            std::memcpy( &native, pChosen->ai_addr, static_cast<size_t>( pChosen->ai_addrlen ) < sizeof( native ) ? static_cast<size_t>( pChosen->ai_addrlen ) : sizeof( native ) );
        freeaddrinfo( pResult );
        if ( pChosen == nullptr )
            return false;
        outAddress       = NativeSocketAddress::fromNative( native );
        outAddress._port = static_cast<uint16>( port );
        return true;
    }
} // namespace sw
