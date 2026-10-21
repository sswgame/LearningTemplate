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

    /** @brief UDP 포트를 다른 소켓과 나누는가입니다. */
    enum class UDPPortSharing : uint8
    {
        Exclusive = 0, ///< 이 소켓만 그 포트를 쓴다
        ReusePort      ///< 리눅스 `SO_REUSEPORT` — 같은 포트의 소켓 여럿에 커널이 4-튜플로 나눠 준다(포트 샤딩의 한 포트 판). 다른 플랫폼은 열지 못한다
    };
} // namespace sw

namespace sw
{
    struct SW_API PlatformSocketUtil
    {
        static constexpr uint64 kInvalidSocket = ~0ull;

        /** @brief 소켓 라이브러리를 엽니다(Windows 만 실제로 한다). 여러 번 불러도 됩니다. */
        [[nodiscard]] static bool initialize();
        /**
         * @brief 논블로킹 UDP 소켓을 @p port 에 엽니다(0 = 아무 포트). 실패하면 `kInvalidSocket` 입니다.
         * @param bLoopbackOnly true 이면 127.0.0.1 에만 묶는다 — 모든 인터페이스에 묶으면 Windows 방화벽이 실행 파일마다 허용 창을 띄운다.
         * @param sharing `ReusePort` 는 리눅스에서만 열린다 — 다른 플랫폼은 `kInvalidSocket` 이다(배타 소켓으로 대신 열지 않는다).
         */
        static uint64 openUDPSocket( uint16 port, bool bLoopbackOnly, UDPPortSharing sharing );
        static void   closeSocket( uint64 socketHandle );
        /** @brief 실제로 묶인 포트입니다(0 으로 열었을 때). */
        static uint16              getBoundPort( uint64 socketHandle );
        [[nodiscard]] static bool  sendTo( uint64 socketHandle, const NetAddress& to, const uint8* pData, int32 size );
        static SocketReceiveResult receiveFrom( uint64 socketHandle, NetAddress& outFrom, uint8* pBuffer, int32 bufferSize, int32& outSize );
        /**
         * @brief 받을 데이터그램이 생기거나 @p timeoutMilli 가 지날 때까지 잠듭니다(POSIX `poll` · Windows `WSAPoll` — `select` 의 FD_SETSIZE 한도가 없다).
         *        받을 것이 있으면 true 입니다. 기다리는 동안 소켓을 닫으면 안 된다(기다리는 스레드를 먼저 멈춘다).
         */
        static bool waitReadable( uint64 socketHandle, int32 timeoutMilli );
        /**
         * @brief "호스트:포트" 를 IPv4 주소로 풉니다. 호스트가 점 넷 숫자면 그대로 읽고, 이름이면 운영체제 해석기(`getaddrinfo`, A 레코드 첫 값)에 묻습니다.
         *        포트가 없으면 @p defaultPort 입니다. 풀지 못하면 false 이며 다른 주소로 대신하지 않습니다.
         * @details 막는 호출입니다 — 이름 해석은 DNS 왕복(수 ms ~ 해석기 시한 몇 초)이라 기동이나 전용 워커에서만 부르고 틱 · 요청 경로에서 부르지 않습니다.
         *          언리얼 `ISocketSubsystem::GetAddressInfo` 의 블로킹 버전에 해당하고, `localhost` 같은 이름도 hosts 파일을 거쳐 이 경로로 해석합니다.
         */
        [[nodiscard]] static bool resolveAddress( string_view hostAndPort, uint16 defaultPort, NetAddress& outAddress );
    };
} // namespace sw
