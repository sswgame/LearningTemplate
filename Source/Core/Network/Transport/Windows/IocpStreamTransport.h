/**
 * @file IocpStreamTransport.h
 * @brief Windows IOCP 스트림 전송 — 완료 포트 하나를 I/O 스레드 N 개가 나눠 기다립니다. 연결마다 읽기 하나 · 쓰기 하나만 걸어 둡니다.
 * @details 만드는 곳은 `StreamTransportFactory` 다. OS 타입(SOCKET · OVERLAPPED)은 .cpp 의 `State` 에만 있다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Transport/IStreamTransport.h"

namespace sw
{
    class IocpStreamTransport final : public IStreamTransport
    {
    public:
        IocpStreamTransport();
        ~IocpStreamTransport() override;

        [[nodiscard]] bool     initialize( IStreamHandler* pHandler, const StreamTransportSettings& settings ) override;
        void                   shutdown() override;
        [[nodiscard]] bool     listen( const NetAddress& bindAddress ) override;
        uint16                 getListenPort() const override;
        StreamConnectionHandle connect( const NetAddress& remote ) override;
        StreamSendResult       send( StreamConnectionHandle handle, const uint8* pData, int32 size ) override;
        void                   close( StreamConnectionHandle handle, StreamCloseMode mode ) override;
        void                   setReceivePaused( StreamConnectionHandle handle, bool bPaused ) override;
        int32                  pollIO( int32 timeoutMilli ) override;
        NetAddress             getRemoteAddress( StreamConnectionHandle handle ) const override;
        StreamTransportStats   getStats() const override;

        struct State;

    private:
        unique_ptr<State> _state;
    };
} // namespace sw
