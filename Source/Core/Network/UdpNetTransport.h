/**
 * @file UdpNetTransport.h
 * @brief 실제 UDP 소켓 전송입니다(논블로킹 — 받을 것이 없으면 바로 돌아온다. 기다림은 `waitForReceive` 가 소켓을 `poll` 한다).
 * @details `send` 는 여러 스레드에서 동시에 불러도 된다(운영체제가 데이터그램 단위로 지킨다). `close` 는 기다리는 스레드를 멈춘 뒤에.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Network/NetTransport.h"

namespace sw
{
    class SW_API UdpNetTransport final : public INetTransport
    {
    public:
        UdpNetTransport();
        ~UdpNetTransport() override;

        UdpNetTransport( const UdpNetTransport& )            = delete;
        UdpNetTransport& operator=( const UdpNetTransport& ) = delete;

        /** @brief 포트를 엽니다(0 = 아무 포트 — 클라이언트). */
        [[nodiscard]] bool open( uint16 port );
        void               close();
        bool               isOpen() const;

        [[nodiscard]] bool send( const NetAddress& to, const uint8* pData, int32 size ) override;
        [[nodiscard]] bool receive( NetAddress& outFrom, vector<uint8>& outBuffer ) override;
        NetAddress         getLocalAddress() const override { return _localAddress; }
        bool               waitForReceive( float64 timeoutSeconds ) override;

    private:
        NetAddress _localAddress;
        uint64     _socketHandle;
    };
} // namespace sw
