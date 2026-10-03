/**
 * @file NetTransport.h
 * @brief 전송 — 데이터그램을 주소로 보내고 받는 인터페이스와, 한 프로세스 안의 루프백 망(지연 · 흔들림 · 손실 · 중복 · 순서 바뀜을 씨앗으로 흉내)입니다.
 * @details 실제 UDP 는 `UdpNetTransport`. 루프백은 시험과 "한 프로세스 안의 서버 + 클라이언트"(리슨 서버 · 리플레이 · 봇)에 씁니다. 결정적이라 같은 씨앗이면 같은 손실이 납니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/deque.h"
#include "Core/Container/vector.h"
#include "Core/Network/NetTypes.h"

namespace sw
{
    /**
     * @class INetTransport
     * @brief 데이터그램 한 개를 보내고 받습니다. 받기는 막히지 않습니다(없으면 바로 false) — 기다리려면 `waitForReceive`.
     * @details **스레드 약속**: `send` 는 아무 스레드에서나 동시에 불러도 되고, `receive` · `waitForReceive` · `update` 는 한 번에 한 스레드
     *          (그 호스트를 `update` 하는 스레드)가 부릅니다 — `NetHost` 는 잠금을 풀고 보내고 받는다. 구현은 이 약속을 지켜야 한다.
     */
    class SW_API INetTransport
    {
    public:
        virtual ~INetTransport() = default;

        [[nodiscard]] virtual bool send( const NetAddress& to, const uint8* pData, int32 size ) = 0;
        [[nodiscard]] virtual bool receive( NetAddress& outFrom, vector<uint8>& outBuffer )     = 0;
        virtual NetAddress         getLocalAddress() const                                      = 0;
        /** @brief 시간이 흐릅니다(루프백 지연). UDP 는 하는 일이 없다. */
        virtual void update( float64 time ) { (void)time; }
        /**
         * @brief 받을 것이 생기거나 @p timeoutSeconds 가 지날 때까지 잠듭니다. 받을 것이 있으면 true 입니다(전용 네트워크 스레드가 바쁘게 돌지 않게).
         * @details 기본은 그냥 잔다 — 깨어날 수단이 없는 전송도 제한 시간마다 `update` 가 돈다.
         */
        virtual bool waitForReceive( float64 timeoutSeconds );
    };

    /** @brief 루프백 망의 나쁜 조건입니다. */
    struct LoopbackConditions
    {
        float32 _latency{ 0.0f };       ///< 한쪽 지연(초)
        float32 _jitter{ 0.0f };        ///< ± 흔들림(초) — 순서가 바뀔 수 있다
        float32 _lossRate{ 0.0f };      ///< 0..1
        float32 _duplicateRate{ 0.0f }; ///< 0..1
        float32 _corruptRate{ 0.0f };   ///< 0..1 — 한 바이트를 뒤집는다(체크섬이 걸러야 한다)
    };

    class LoopbackNetwork;

    /** @brief 루프백 망의 끝점 하나입니다. */
    class SW_API LoopbackTransport final : public INetTransport
    {
    public:
        LoopbackTransport( LoopbackNetwork* pNetwork, const NetAddress& address );

        [[nodiscard]] bool send( const NetAddress& to, const uint8* pData, int32 size ) override;
        [[nodiscard]] bool receive( NetAddress& outFrom, vector<uint8>& outBuffer ) override;
        NetAddress         getLocalAddress() const override { return _address; }
        void               update( float64 time ) override;
        bool               waitForReceive( float64 timeoutSeconds ) override;

        // 망이 부릅니다 — 망의 잠금을 잡은 채로.
        void deliver( const NetAddress& from, vector<uint8>&& buffer );
        bool popInbox( NetAddress& outFrom, vector<uint8>& outBuffer );
        bool hasInbox() const { return _listInbox.empty() == false; }

    private:
        struct Datagram
        {
            vector<uint8> _buffer{};
            NetAddress    _from{};
        };

        LoopbackNetwork* _pNetwork;
        deque<Datagram>  _listInbox;
        NetAddress       _address;
    };

    /**
     * @class LoopbackNetwork
     * @brief 끝점들을 들고 패킷을 시각 순으로 배달합니다. 끝점은 망이 소유하고 주소는 `127.0.0.1:포트` 입니다.
     * @details 잠금 하나가 망 전체(날아가는 패킷 · 끝점 받은 편지함 · 난수)를 지킵니다 — 끝점마다 다른 스레드(`NetHostThread`)가 돌아도 된다.
     *          스레드가 여럿이면 배달 순서 · 손실은 스레드 일정에 따라 달라진다(결정적인 것은 한 스레드로 돌 때뿐).
     */
    class SW_API LoopbackNetwork
    {
    public:
        explicit LoopbackNetwork( uint32 seed = 1u );

        LoopbackNetwork( const LoopbackNetwork& )            = delete;
        LoopbackNetwork& operator=( const LoopbackNetwork& ) = delete;

        /** @brief 끝점을 만듭니다. 같은 포트가 있으면 nullptr 입니다. */
        LoopbackTransport* createEndpoint( uint16 port );
        void               setConditions( const LoopbackConditions& conditions );
        /** @brief 이 시각까지 도착한 패킷을 끝점 받은 편지함으로 옮깁니다. */
        void advance( float64 time );

        LoopbackConditions getConditions() const;
        uint64             getDroppedCount() const;
        uint64             getDeliveredCount() const;

        // LoopbackTransport 가 부른다.
        void enqueue( const NetAddress& from, const NetAddress& to, const uint8* pData, int32 size );
        /** @brief @p pEndpoint 의 받은 편지함에서 하나를 꺼냅니다(잠금 안에서). */
        bool takeDatagram( LoopbackTransport* pEndpoint, NetAddress& outFrom, vector<uint8>& outBuffer );
        bool hasDatagram( const LoopbackTransport* pEndpoint ) const;

    private:
        struct InFlight
        {
            vector<uint8> _buffer{};
            NetAddress    _from{};
            NetAddress    _to{};
            float64       _deliverTime{ 0.0 };
            uint64        _order{ 0 };
        };

        float32 nextRandom();

        mutable mutex            _mutex;
        deque<LoopbackTransport> _listEndpoint; ///< deque — 끝점 주소가 움직이지 않는다
        vector<InFlight>         _listInFlight;
        LoopbackConditions       _conditions;
        float64                  _time;
        uint64                   _order;
        uint64                   _droppedCount;
        uint64                   _deliveredCount;
        uint32                   _randomState;
    };

} // namespace sw
