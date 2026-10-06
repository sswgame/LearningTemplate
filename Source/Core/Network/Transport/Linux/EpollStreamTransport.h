/**
 * @file EpollStreamTransport.h
 * @brief 리눅스 epoll 스트림 전송 — I/O 스레드마다 epoll 하나(에지 트리거)와 eventfd 하나, 연결은 돌림차례로 한 루프가 소유합니다.
 * @details 만드는 곳은 `StreamTransportFactory` 다. OS 타입은 .cpp 의 `State` 에만 있다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Transport/IStreamTransport.h"

namespace sw
{
    class EpollStreamTransport final : public IStreamTransport
    {
    public:
        EpollStreamTransport();
        ~EpollStreamTransport() override;

        [[nodiscard]] bool     initialize( IStreamHandler* pHandler, const StreamTransportSettings& settings ) override;
        void                   shutdown() override;
        [[nodiscard]] bool     listen( const NetAddress& bindAddress ) override;
        uint16                 getListenPort() const override;
        StreamConnectionHandle connect( const NetAddress& remote ) override;
        StreamSendResult       send( StreamConnectionHandle handle, const uint8* pData, int32 size ) override;
        void                   close( StreamConnectionHandle handle, StreamCloseMode mode ) override;
        void                   setReceivePaused( StreamConnectionHandle handle, bool bPaused ) override;
        int32                  pollIo( int32 timeoutMilli ) override;
        NetAddress             getRemoteAddress( StreamConnectionHandle handle ) const override;
        StreamTransportStats   getStats() const override;

        struct State;

    private:
        unique_ptr<State> _state;
    };
} // namespace sw
