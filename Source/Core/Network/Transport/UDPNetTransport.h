/**
 * @file UDPNetTransport.h
 * @brief 실제 UDP 소켓 전송입니다(논블로킹 — 받을 것이 없으면 바로 돌아온다. 기다림은 `waitForReceive` 가 소켓을 `poll` 한다).
 * @details `send` · `wake` 는 여러 스레드에서 동시에 불러도 된다(운영체제가 데이터그램 단위로 지킨다). `close` 는 기다리는 스레드를 멈춘 뒤에.
 *          깨우기: 이 기계(127.0.0.1)의 빈 포트에 작은 소켓을 하나 더 열어, `wake` 가 거기로 한 바이트를 보내면 `waitForReceive` 의 poll 이 끝난다.
 *          Windows `WSAPoll` 은 파이프 · 이벤트를 기다리지 못해 소켓으로 깨운다(GameNetworkingSockets 는 `WSAEventSelect`, 리눅스 라이브러리는 eventfd 를 쓰는 자리).
 *          받는 소켓 자신에게 보내지 않는 까닭: 포트 샤딩(`SO_REUSEPORT`)에서는 커널이 그 데이터그램을 다른 샤드의 소켓에 줄 수 있다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Transport/NetTransport.h"
#include "Core/Network/Transport/PlatformSocketUtil.h"

namespace sw
{
    class SW_API UDPNetTransport final : public INetTransport
    {
    public:
        UDPNetTransport();
        ~UDPNetTransport() override;

        UDPNetTransport( const UDPNetTransport& )            = delete;
        UDPNetTransport& operator=( const UDPNetTransport& ) = delete;

        /**
         * @brief 포트를 엽니다(0 = 아무 포트 — 클라이언트). @p bLoopbackOnly 가 true 이면 이 기계(127.0.0.1)의 상대만 받는다 — 테스트 · 개발 실행용.
         * @param sharing 포트 샤딩의 한 포트 판(`UDPPortSharing::ReusePort`, 리눅스만). 기본은 배타 소켓.
         */
        [[nodiscard]] bool open( uint16 port, bool bLoopbackOnly, UDPPortSharing sharing = UDPPortSharing::Exclusive );
        void               close();
        bool               isOpen() const;

        [[nodiscard]] bool send( const NetAddress& to, const uint8* pData, int32 size ) override;
        [[nodiscard]] bool receive( NetAddress& outFrom, vector<uint8>& outBuffer ) override;
        /** @brief 리눅스는 `sendmmsg` 로 한 번에 보낸다(`PlatformSocketUtil::sendBatch`). 한도(`kNetMaxPacketSize`)를 넘는 크기가 든 묶음은 그 앞까지만 보낸다. */
        int32 sendBatch( const NetDatagramView* pViews, int32 count ) override;
        /** @brief 리눅스는 `recvmmsg` 로 한 번에 받는다. 한도를 넘는 데이터그램은 걸러 낸다. */
        int32      receiveBatch( NetReceivedDatagram* pOut, int32 maxCount ) override;
        NetAddress getLocalAddress() const override { return _localAddress; }
        bool       waitForReceive( float64 timeoutSeconds ) override;
        void       wake() override;

    private:
        NetAddress _localAddress;
        NetAddress _wakeAddress; ///< 깨우기 소켓의 주소(127.0.0.1:빈 포트)
        uint64     _socketHandle;
        uint64     _wakeSocketHandle; ///< `wake` 가 한 바이트를 보내는 소켓 — `waitForReceive` 가 받는 소켓과 함께 기다리고 비운다
    };
} // namespace sw
