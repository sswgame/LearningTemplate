/**
 * @file IStreamTransport.h
 * @brief 스트림(TCP) 전송의 이벤트 루프 인터페이스 — 수락 · 연결 · 읽기 · 쓰기 완료를 처리기에 알립니다. 구현은 Windows IOCP · 리눅스 epoll · 루프백(시험).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/StreamTypes.h"

namespace sw
{
    /**
     * @class IStreamHandler
     * @brief 전송이 부르는 콜백입니다. **I/O 스레드**(또는 `pollIo` 를 부른 스레드)에서 불립니다.
     * @details 약속: 한 연결의 `onStreamReceived` 는 받은 순서대로 하나씩 온다. `onStreamWritable` 은 다른 I/O 스레드에서 겹칠 수 있다.
     *          `onStreamClosed` 는 그 연결의 **마지막** 콜백이고 정확히 한 번이다(열린 적 없는 연결 — 연결 실패 — 도 한 번). 콜백 안에서 `send` · `close` ·
     *          `setReceivePaused` 를 불러도 된다. 콜백은 오래 막지 않는다 — 무거운 일은 게임 스레드 큐로 넘긴다(`StreamMessageEndpoint`).
     */
    class SW_API IStreamHandler
    {
    public:
        IStreamHandler()          = default;
        virtual ~IStreamHandler() = default;

        IStreamHandler( const IStreamHandler& )            = default;
        IStreamHandler& operator=( const IStreamHandler& ) = default;

        /** @brief 연결이 열렸습니다. @p bAccepted 는 서버가 받은 연결이면 true(내가 `connect` 한 것이면 false). */
        virtual void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) = 0;
        /** @brief 받은 바이트입니다. @p pData 는 콜백 동안만 유효합니다. */
        virtual void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) = 0;
        /** @brief 높은 물금을 넘었던 보낼 줄이 낮은 물금 아래로 내려갔습니다. */
        virtual void onStreamWritable( StreamConnectionHandle handle ) { (void)handle; }
        /** @brief 연결이 닫혔습니다 — 이 핸들의 마지막 콜백입니다. */
        virtual void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class IStreamTransport
     * @brief 스트림 전송 하나 — 리슨 하나와 여러 연결(받은 것 · 건 것)을 한 이벤트 루프(들)에서 돕니다.
     * @details `send` · `close` · `setReceivePaused` · `connect` 는 아무 스레드에서나. `initialize` · `listen` · `shutdown` 은 한 스레드(소유자)에서.
     *          `shutdown` 이 돌아오면 콜백은 더 오지 않는다(열린 연결은 모두 `Shutdown` 으로 닫히며 `onStreamClosed` 를 받는다).
     */
    class SW_API IStreamTransport
    {
    public:
        IStreamTransport()          = default;
        virtual ~IStreamTransport() = default;

        IStreamTransport( const IStreamTransport& )            = delete;
        IStreamTransport& operator=( const IStreamTransport& ) = delete;

        /** @brief 처리기와 설정을 정하고 I/O 스레드를 띄웁니다. */
        [[nodiscard]] virtual bool initialize( IStreamHandler* pHandler, const StreamTransportSettings& settings ) = 0;
        /** @brief 모든 연결을 끊고(Shutdown) I/O 스레드를 합류합니다. 두 번 불러도 됩니다. */
        virtual void shutdown() = 0;

        /**
         * @brief @p bindAddress 에서 받기 시작합니다. 포트 0 = 아무 포트(`getListenPort` 로 읽는다).
         * @details 주소가 묶을 인터페이스다 — `NetAddress::makeLoopback( port )` 은 이 기계에서만(운영 끝점의 기본),
         *          `NetAddress::makeAnyInterface( port )` 는 모든 인터페이스(게임 · 서비스 서버). 루프백 망은 포트만 본다.
         */
        [[nodiscard]] virtual bool listen( const NetAddress& bindAddress ) = 0;
        virtual uint16             getListenPort() const                   = 0;
        /** @brief 연결을 겁니다. 시작도 못 하면 무효 핸들, 아니면 결과는 `onStreamOpened` 또는 `onStreamClosed( ConnectFailed )` 입니다. */
        virtual StreamConnectionHandle connect( const NetAddress& remote ) = 0;

        /** @brief 바이트를 보낼 줄에 쌓습니다(복사). 순서는 부른 순서 — 같은 연결을 두 스레드가 동시에 보내면 두 덩어리가 섞이지는 않지만 순서는 정해지지 않는다. */
        virtual StreamSendResult send( StreamConnectionHandle handle, const uint8* pData, int32 size ) = 0;
        virtual void             close( StreamConnectionHandle handle, StreamCloseMode mode )          = 0;
        /** @brief 읽기를 멈추고 · 다시 합니다(위 층의 받은 줄이 찼을 때 — 커널 버퍼가 차면 TCP 창이 닫혀 상대가 멈춘다). */
        virtual void setReceivePaused( StreamConnectionHandle handle, bool bPaused ) = 0;

        /**
         * @brief `_ioThreadCount == 0` 일 때 부르는 쪽이 이벤트 루프를 한 번 돕니다(@p timeoutMilli 까지 기다림, 0 = 기다리지 않음). 처리한 완료 수입니다.
         *        I/O 스레드가 있으면 아무것도 하지 않고 0 입니다.
         */
        virtual int32 pollIo( int32 timeoutMilli ) = 0;

        virtual NetAddress           getRemoteAddress( StreamConnectionHandle handle ) const = 0;
        virtual StreamTransportStats getStats() const                                        = 0;
    };
} // namespace sw
