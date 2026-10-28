/**
 * @file NetTransport.h
 * @brief 전송 — 데이터그램을 주소로 보내고 받는 인터페이스와, 한 프로세스 안의 루프백 망(보낸 순서대로 배달만 합니다)입니다.
 * @details 실제 UDP 는 `UDPNetTransport`. 루프백은 테스트와 "한 프로세스 안의 서버 + 클라이언트"(리슨 서버 · 리플레이 · 봇)에 씁니다.
 *          회선 나쁨(지연 · 흔들림 · 손실 · 중복 · 깨짐 · 순서 뒤바뀜 · 대역폭)은 엔드포인트에 씌우는 `NetEmulationTransport` 하나가 맡습니다 — UDP 에 씌우는 것과 같은 길입니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/deque.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/NetTypes.h"

namespace sw
{
    /** @brief 받은 데이터그램 하나입니다. 일괄 받기(`receiveBatch`)가 채우며, 버퍼는 부른 쪽이 다시 쓴다(할당을 되풀이하지 않는다). */
    struct NetReceivedDatagram
    {
        vector<uint8> _buffer{};
        NetAddress    _from{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class INetTransport
     * @brief 데이터그램 한 개를 보내고 받습니다. 받기는 막히지 않습니다(없으면 바로 false) — 기다리려면 `waitForReceive`.
     * @details **스레드 약속**: `send` 는 아무 스레드에서나 동시에 불러도 되고, `receive` · `waitForReceive` · `update` 는 한 번에 한 스레드
     *          (그 엔드포인트를 `update` 하는 스레드)가 부릅니다 — `NetEndpoint` 는 잠금을 풀고 보내고 받는다. 구현은 이 약속을 지켜야 한다.
     */
    class SW_API INetTransport
    {
    public:
        INetTransport()          = default;
        virtual ~INetTransport() = default;

        INetTransport( const INetTransport& )            = default;
        INetTransport& operator=( const INetTransport& ) = default;

        [[nodiscard]] virtual bool send( const NetAddress& to, const uint8* pData, int32 size ) = 0;
        [[nodiscard]] virtual bool receive( NetAddress& outFrom, vector<uint8>& outBuffer )     = 0;
        /**
         * @brief @p count 개를 보내고 보낸 개수를 반환합니다(실패는 손실과 같다 — 재전송이 맡는다). 기본은 `send` 를 하나씩 부른다.
         * @details 시스템 호출이 지배적인 UDP 소켓이 한 번에 보내도록(리눅스 `sendmmsg`) 있는 자리다. 같은 스레드 약속을 지킨다.
         */
        virtual int32 sendBatch( const NetDatagramView* pViews, int32 count );
        /**
         * @brief 받은 데이터그램을 @p pOut 의 앞에서부터 최대 @p maxCount 개 채우고 채운 개수를 반환합니다(없으면 0). 기본은 `receive` 를 하나씩 부른다.
         * @details 리눅스 UDP 는 `recvmmsg` 로 한 번에 받는다. 채운 버퍼의 크기가 데이터그램의 크기다.
         */
        virtual int32      receiveBatch( NetReceivedDatagram* pOut, int32 maxCount );
        virtual NetAddress getLocalAddress() const = 0;
        /** @brief 시간이 흐릅니다 — 루프백은 날아가는 패킷을 배달하고, 흉내는 때가 된 패킷을 안쪽 전송에 넘긴다. UDP 는 하는 일이 없다. */
        virtual void update( float64 time ) { (void)time; }
        /**
         * @brief 받을 것이 생기거나 @p timeoutSeconds 가 지날 때까지 잠듭니다. 받을 것이 있으면 true 입니다(전용 네트워크 스레드가 바쁘게 돌지 않게).
         * @details 기본은 그냥 잔다 — 깨어날 수단이 없는 전송도 제한 시간마다 `update` 가 돈다.
         */
        virtual bool waitForReceive( float64 timeoutSeconds );
    };
} // namespace sw

namespace sw
{
    class LoopbackNetwork;

    /** @brief 루프백 망의 엔드포인트 하나입니다. */
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
} // namespace sw

namespace sw
{
    /**
     * @class LoopbackNetwork
     * @brief 엔드포인트들을 들고 보낸 패킷을 보낸 순서대로 배달합니다 — 회선을 나쁘게 하지 않습니다(나쁜 회선은 엔드포인트에 씌운 `NetEmulationTransport`).
     *        엔드포인트는 망이 소유하고 주소는 `127.0.0.1:포트` 입니다.
     * @details 보낸 패킷은 다음 `deliverInFlight`(어느 엔드포인트의 `update` 든)에 받는 엔드포인트의 받은 편지함으로 간다 — 한 프레임에서 앞서 돈 엔드포인트가 보낸 것을
     *          뒤에 도는 엔드포인트가 받는다. 받을 엔드포인트가 없는 패킷은 버리고 센다(UDP 의 닫힌 포트).
     *          잠금 하나가 망 전체(날아가는 패킷 · 엔드포인트 받은 편지함)를 지킵니다 — 엔드포인트마다 다른 스레드(`NetEndpointThread`)가 돌아도 된다.
     */
    class SW_API LoopbackNetwork
    {
    public:
        LoopbackNetwork();

        LoopbackNetwork( const LoopbackNetwork& )            = delete;
        LoopbackNetwork& operator=( const LoopbackNetwork& ) = delete;

        /** @brief 엔드포인트를 만듭니다. 같은 포트가 있으면 nullptr 입니다. */
        LoopbackTransport* createEndpoint( uint16 port );
        /** @brief 날아가는 패킷을 모두 받는 엔드포인트의 받은 편지함으로 옮깁니다(보낸 순서). */
        void deliverInFlight();

        /** @brief 받을 엔드포인트가 없어 버린 패킷 수입니다. */
        uint64 getDroppedCount() const;
        uint64 getDeliveredCount() const;

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
        };

        mutable mutex                          _mutex;
        deque<LoopbackTransport> _listEndpoint SW_GUARDED_BY( _mutex ); ///< deque — 엔드포인트 주소가 움직이지 않는다
        vector<InFlight> _listInFlight         SW_GUARDED_BY( _mutex ); ///< 보낸 순서
        uint64 _droppedCount                   SW_GUARDED_BY( _mutex );
        uint64 _deliveredCount                 SW_GUARDED_BY( _mutex );
    };
} // namespace sw
