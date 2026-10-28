/**
 * @file DelayedStreamTransport.h
 * @brief 다른 스트림 전송을 감싸 보내는 바이트에 고정 지연을 겁니다 — 한 기계에서 "먼 망"의 왕복을 흉내 내는 측정용 장식자입니다.
 * @details `OnlineMultiServerLatency` 후보의 한 PC 판이다. 서버 둘(또는 서버와 클라이언트)이 각자 전송을 이것으로 감싸면 왕복에 지연의 두 배가 더해진다.
 *          UDP 쪽 흉내(`NetEmulationTransport`)는 데이터그램용이라 TCP 스트림에는 이것이 같은 일을 한다. 지연은 보내는 쪽에서만 걸고 받는 쪽은 안쪽 전송의 콜백 그대로다.
 *          **한계**: `send` 는 안쪽 전송에 아직 쌓지 않은 채 `Queued` 를 반환한다 — 보낼 줄 물금(`QueuedAboveHighWatermark`) · `QueueFull` 은 이 층에서 알 수 없다.
 *          `_ioThreadCount == 0`(루프백처럼 부르는 쪽이 `pollIO` 를 도는 전송)이면 타이머 스레드를 두지 않고 `pollIO` 가 때가 된 것을 안쪽에 넘긴다 — 안쪽이 한 스레드 전용이라서다.
 *          그래서 처리량이 아니라 지연(왕복 시간) 측정에만 쓴다. 한 연결의 순서는 지켜진다(지연이 고정이고 보낸 순서대로 안쪽에 넘긴다).
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/deque.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Transport/IStreamTransport.h"

namespace sw
{
    class SW_API DelayedStreamTransport final : public IStreamTransport
    {
    public:
        /** @param pInner 감쌀 전송(소유권을 받는다). @param sendDelaySeconds 보내는 바이트마다 거는 지연(0 이상). */
        DelayedStreamTransport( unique_ptr<IStreamTransport> pInner, float64 sendDelaySeconds );
        ~DelayedStreamTransport() override;

        [[nodiscard]] bool     initialize( IStreamHandler* pHandler, const StreamTransportConfig& settings ) override;
        void                   shutdown() override;
        [[nodiscard]] bool     listen( const NetAddress& bindAddress ) override { return _pInner->listen( bindAddress ); }
        uint16                 getListenPort() const override { return _pInner->getListenPort(); }
        StreamConnectionHandle connect( const NetAddress& remote ) override { return _pInner->connect( remote ); }
        StreamSendResult       send( StreamConnectionHandle handle, const uint8* pData, int32 size ) override;
        void                   close( StreamConnectionHandle handle, StreamCloseMode mode ) override;
        void                   setReceivePaused( StreamConnectionHandle handle, bool bPaused ) override { _pInner->setReceivePaused( handle, bPaused ); }
        int32                  pollIO( int32 timeoutMilli ) override;
        NetAddress             getRemoteAddress( StreamConnectionHandle handle ) const override { return _pInner->getRemoteAddress( handle ); }
        StreamTransportStats   getStats() const override { return _pInner->getStats(); }

    private:
        /** @brief 지연을 기다리는 보내기 하나입니다. `_bClose` 이면 바이트 대신 닫기입니다(보낸 바이트보다 먼저 닫지 않게 같은 줄을 지난다). */
        struct DelayedItem
        {
            int64                  _dueNanoseconds{ 0 };
            StreamConnectionHandle _handle{};
            vector<uint8>          _bytes{};
            StreamCloseMode        _closeMode{ StreamCloseMode::Graceful };
            uint8                  _bClose{ SW_FALSE };
        };

        void  runTimer();
        int32 flushDue();

        unique_ptr<IStreamTransport> _pInner;
        int64                        _delayNanoseconds;
        mutex                        _mutex;
        deque<DelayedItem>           _listItem; ///< 지연 시각 순서(고정 지연이라 보낸 순서와 같다)
        atomic<uint8>                _bStopping;
        uint8                        _bManualPoll;
        sw::thread                   _timerThread;
    };
} // namespace sw
