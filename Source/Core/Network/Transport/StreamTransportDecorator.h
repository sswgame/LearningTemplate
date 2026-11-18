/**
 * @file StreamTransportDecorator.h
 * @brief 스트림 전송 하나를 감싸는 장식자의 공통 기반입니다 — 호출을 안쪽 전송에 그대로 넘기고, 장식자는 자기가 바꾸는 함수만 덮어씁니다.
 * @details 예전에는 장식자마다(`ProxyProtocolAcceptTransport` · `WebSocketAcceptTransport` · `DelayedStreamTransport`) 같은 통과 함수 여섯~아홉 줄을 따로 적어,
 *          `IStreamTransport` 에 함수가 늘면 세 곳을 함께 고쳐야 했다. `initialize` 는 넘기지 않는다 — 장식자마다 처리기를 끼우는 법이 달라 각자 구현한다.
 *          안쪽 전송은 맨 먼저 만들어지고 맨 나중에 사라진다(기반의 멤버) — 장식자의 소멸자가 안쪽을 내린 뒤 자기 멤버를 지우는 순서가 그대로다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Transport/IStreamTransport.h"

namespace sw
{
    class SW_API StreamTransportDecorator : public IStreamTransport
    {
    public:
        void                   shutdown() override { _pInner->shutdown(); }
        [[nodiscard]] bool     listen( const NetAddress& bindAddress ) override { return _pInner->listen( bindAddress ); }
        uint16                 getListenPort() const override { return _pInner->getListenPort(); }
        StreamConnectionHandle connect( const NetAddress& remote ) override { return _pInner->connect( remote ); }
        StreamSendResult       send( StreamConnectionHandle handle, const uint8* pData, int32 size ) override { return _pInner->send( handle, pData, size ); }
        void                   close( StreamConnectionHandle handle, StreamCloseMode mode ) override { _pInner->close( handle, mode ); }
        void                   setReceivePaused( StreamConnectionHandle handle, bool bPaused ) override { _pInner->setReceivePaused( handle, bPaused ); }
        int32                  pollIO( int32 timeoutMilli ) override { return _pInner->pollIO( timeoutMilli ); }
        NetAddress             getRemoteAddress( StreamConnectionHandle handle ) const override { return _pInner->getRemoteAddress( handle ); }
        StreamTransportStats   getStats() const override { return _pInner->getStats(); }

    protected:
        /** @param pInner 감쌀 전송(소유권을 받는다). */
        explicit StreamTransportDecorator( unique_ptr<IStreamTransport> pInner )
            : _pInner{ std::move( pInner ) }
        {
        }

    protected:
        unique_ptr<IStreamTransport> _pInner;
    };
} // namespace sw
