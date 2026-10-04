/**
 * @file NetHost.h
 * @brief 서버 · 클라이언트 끝점 — 연결 핸드셰이크(요청 → 도전 → 응답 → 수락), 체크섬 · 프로토콜 id 로 남의 패킷 거르기, 연결 유지 · 타임아웃 · 끊기, 연결마다의 신뢰성 계층입니다.
 * @details 장르별 네트워크 방식(클라이언트-서버 복제 · 락스텝 · 턴 중계 · MMO)은 이 위에 키트로 얹습니다. 여기는 "누가 연결됐고 어느 채널로 무엇이 왔는가" 까지입니다.
 *          도전(challenge) 단계는 위조한 주소로 서버에 연결 자리를 잡는 것을 막습니다 — 도전 값은 실제 그 주소로 간 패킷에만 들어 있습니다.
 *
 *          **스레드**: 공개 함수는 모두 잠금 하나로 지켜져 아무 스레드에서나 부를 수 있습니다(게임 스레드가 보내고, 작업 스레드가 꺼내고, 네트워크
 *          스레드가 `update` 한다). `update` 는 소켓 받기 · 보내기를 **잠금 밖에서** 묶어 하고 잠금 안에서는 패킷 처리만 하므로, 시스템 호출이
 *          보내는 쪽 스레드를 막지 않습니다. `update` 를 부르는 스레드는 한 번에 하나입니다 — 보통 `NetHostThread` 가 맡아 게임 프레임과
 *          상관없이 받기 · 확인 · 재전송 · 유지를 돌립니다(로딩 · 멈춘 프레임에 연결이 끊기지 않는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/NetConnection.h"
#include "Core/Network/NetTypes.h"
#include "Core/Task/TaskFuture.h"

namespace sw
{
    class INetTransport;

    /** @brief 호스트 설정입니다. 시간은 초입니다. */
    struct NetHostSettings
    {
        uint32  _protocolId{ 0x53574E31u }; ///< 게임 · 버전마다 다르게(다르면 서로의 패킷을 버린다)
        uint64  _saltSeed{ 0 };             ///< 도전 값 씨앗 — 0 이면 운영체제 난수(위조 연결을 막는다). 시험은 고정해도 된다
        float64 _timeout{ 5.0 };
        float64 _connectTimeout{ 5.0 };
        float64 _connectRetryInterval{ 0.2 };
        float64 _sendInterval{ 1.0 / 30.0 }; ///< 연결마다 패킷을 보내는 가장 짧은 간격
        float64 _keepAliveInterval{ 0.25 };  ///< 보낼 것(메시지 · 재전송 · 확인)이 없으면 이 간격으로만 보낸다(유지 · RTT)
        int32   _maxConnections{ 16 };
    };
} // namespace sw

namespace sw
{
    /** @brief 호스트에서 생긴 일입니다. */
    struct NetHostEvent
    {
        enum class Kind : uint8
        {
            Connected = 0,
            Disconnected
        };
        int32               _connectionId{ -1 };
        NetDisconnectReason _reason{ NetDisconnectReason::None };
        Kind                _kind{ Kind::Connected };
    };
} // namespace sw

namespace sw
{
    /** @brief `connectAsync` 의 결과입니다. */
    struct NetConnectResult
    {
        NetDisconnectReason _reason{ NetDisconnectReason::None }; ///< None 이면 연결됐다
        int32               _clientIndex{ -1 };                   ///< 서버가 준 자기 번호(연결됐을 때)

        bool isConnected() const { return _reason == NetDisconnectReason::None; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetHost
     * @brief 서버는 `listen`, 클라이언트는 `connect` 로 시작해 `update( 시각 )` 합니다(직접 매 프레임, 또는 `NetHostThread`). 클라이언트의 서버 연결은 id 0 입니다.
     * @code
     *     host.initialize( &transport, settings );
     *     (void)host.listen();
     *     host.update( time );                                    // 또는 netThread.start( &host ) — 그러면 update 를 부르지 않는다
     *     while ( host.receiveMessage( connectionId, channel, buffer ) ) { ... }
     *
     *     TaskFuture<NetConnectResult> future = client.connectAsync( serverAddress );
     *     future.then( []( const NetConnectResult& result ) { ... } ); // 결과는 update 를 돈 스레드에서 — 잠금 밖
     * @endcode
     */
    class SW_API NetHost
    {
    public:
        static constexpr int32 kMaxDatagramPerUpdate = 512; ///< `update` 한 번에 받는 데이터그램 한도 — 나머지는 다음 번에(소켓 버퍼에 남는다)

        NetHost();

        NetHost( const NetHost& )            = delete;
        NetHost& operator=( const NetHost& ) = delete;

        /** @brief 전송과 설정을 정합니다. `update` 를 도는 스레드가 없을 때 부른다. */
        void               initialize( INetTransport* pTransport, const NetHostSettings& settings );
        [[nodiscard]] bool listen();
        [[nodiscard]] bool connect( const NetAddress& serverAddress );
        /**
         * @brief 비동기 연결 — 연결되거나 실패(거절 · 가득 참 · 타임아웃 · 끊음)하면 채워지는 future 를 돌려줍니다. 시작부터 못 하면 이미 채워진 future(Rejected)입니다.
         * @details 결과(와 `then` 후속 작업)는 그 일을 처리한 스레드(`update` · `disconnect` 를 부른 쪽)에서 잠금을 푼 뒤에 채워진다. 다시 `connect` 하면
         *          앞의 future 는 Requested 로 끝난다.
         */
        TaskFuture<NetConnectResult> connectAsync( const NetAddress& serverAddress );
        /** @brief 받기(잠금 밖) → 처리 · 타임아웃 · 보낼 패킷 만들기(잠금 안) → 보내기(잠금 밖)입니다. 한 번에 한 스레드만 부른다. */
        void update( float64 time );
        /** @brief 받을 데이터그램이 생기거나 @p timeoutSeconds 가 지날 때까지 잠듭니다(`update` 를 도는 스레드가 부른다). */
        bool waitForReceive( float64 timeoutSeconds );

        [[nodiscard]] bool sendMessage( int32 connectionId, NetChannelType channel, const uint8* pData, int32 size );
        [[nodiscard]] bool sendMessage( int32 connectionId, NetChannelType channel, const vector<uint8>& buffer )
        {
            return sendMessage( connectionId, channel, buffer.data(), static_cast<int32>( buffer.size() ) );
        }
        /** @brief 연결된 모두에게 보냅니다(@p exceptId 는 빼고). 보낸 수입니다. */
        int32 broadcast( NetChannelType channel, const uint8* pData, int32 size, int32 exceptId = -1 );
        /** @brief 받은 메시지 하나를 꺼냅니다 — 연결 순, 채널은 신뢰 → 순서 → 비신뢰 순. */
        [[nodiscard]] bool receiveMessage( int32& outConnectionId, NetChannelType& outChannel, vector<uint8>& outBuffer );
        void               disconnect( int32 connectionId );
        void               disconnectAll();
        void               drainEvents( vector<NetHostEvent>& outListEvent );

        bool               isServer() const;
        NetConnectionState getConnectionState( int32 connectionId ) const;
        /** @brief 연결 통계의 사본입니다(연결돼 있지 않으면 false). 다른 스레드가 `update` 하는 중에도 안전합니다. */
        [[nodiscard]] bool getConnectionStats( int32 connectionId, NetConnectionStats& outStats ) const;
        /**
         * @brief 연결 객체를 그대로 빌려줍니다.
         * @warning 잠금 밖으로 나가는 포인터입니다 — `update` 를 도는 다른 스레드가 없을 때만(한 스레드로 쓸 때 · 시험). 스레드가 돌면 `getConnectionStats`.
         */
        const NetConnection* findConnection( int32 connectionId ) const;
        NetAddress           getConnectionAddress( int32 connectionId ) const;
        int32                getConnectedCount() const;
        /** @brief 클라이언트 — 서버가 준 자기 번호(서버의 연결 id)입니다. 연결 전이면 −1 입니다. */
        int32  getClientIndex() const;
        void   collectConnected( vector<int32>& outListConnection ) const;
        uint64 getRejectedPacketCount() const;

    private:
        enum class PacketType : uint8
        {
            ConnectRequest = 0,
            Challenge,
            ChallengeResponse,
            Accepted,
            Denied,
            Payload,
            Disconnect,
            Count
        };

        struct Slot
        {
            NetConnection      _connection{};
            NetAddress         _address{};
            uint64             _clientSalt{ 0 };
            uint64             _serverSalt{ 0 };
            float64            _lastReceiveTime{ 0.0 };
            float64            _lastSendTime{ -1.0 };
            float64            _connectStartTime{ 0.0 };
            NetConnectionState _state{ NetConnectionState::Disconnected };
            uint8              _bPendingChallenge{ SW_FALSE }; ///< 서버 — 도전을 보냈고 응답을 기다린다
        };

        /** @brief 보낼 데이터그램 하나 — 바이트는 `OutgoingBatch::_bytes` 의 [_offset, _offset + _size) 입니다. */
        struct OutgoingDatagram
        {
            NetAddress _to{};
            int32      _offset{ 0 };
            int32      _size{ 0 };
        };

        /** @brief 잠금 안에서 쌓고 잠금 밖에서 보내는 묶음입니다. 둘을 맞바꿔(swap) 할당 없이 넘긴다. */
        struct OutgoingBatch
        {
            vector<OutgoingDatagram> _listDatagram{};
            vector<uint8>            _bytes{};

            void clear()
            {
                _listDatagram.clear();
                _bytes.clear();
            }
        };

        struct ReceivedDatagram
        {
            vector<uint8> _buffer{};
            NetAddress    _from{};
        };

        /** @brief 끝난 비동기 연결 — 잠금 밖에서 채운다. */
        struct FinishedConnect
        {
            TaskPromise<NetConnectResult> _promise{};
            NetConnectResult              _result{};
        };

        // 아래는 모두 잠금을 잡은 채로 부른다.
        void handlePacket( float64 time, const NetAddress& from, const uint8* pData, int32 size );
        void updateSlots( float64 time );
        void sendControl( const NetAddress& to, PacketType type, uint64 valueA, uint64 valueB );
        /** @brief `_packetWriter` 의 몸에 헤더(프로토콜 · 체크섬)를 붙여 보낼 묶음에 넣습니다. */
        void sendFramed( const NetAddress& to );
        void sendPayload( float64 time, Slot& slot );
        void closeSlot( int32 slotIndex, NetDisconnectReason reason, bool bNotifyRemote );
        bool startConnect( const NetAddress& serverAddress );
        void pushEvent( const NetHostEvent& event );
        void finishConnect( NetDisconnectReason reason );
        bool sendMessageLocked( int32 connectionId, NetChannelType channel, const uint8* pData, int32 size );
        void takePending( OutgoingBatch& outBatch, vector<FinishedConnect>& outListFinished );

        // 잠금 밖에서.
        void        sendBatch( const OutgoingBatch& batch );
        static void deliverFinished( vector<FinishedConnect>& listFinished );

        int32  findSlotByAddress( const NetAddress& address ) const;
        void   bindAddress( int32 slotIndex, const NetAddress& address );
        void   unbindAddress( int32 slotIndex );
        int32  findFreeSlot() const;
        uint64 nextSalt();
        bool   isValidSlot( int32 slotIndex ) const { return slotIndex >= 0 && slotIndex < static_cast<int32>( _listSlot.size() ); }

        mutable mutex                 _mutex; ///< 아래 상태 모두(잠금 밖 전용이라고 적은 것은 빼고)
        vector<Slot>                  _listSlot;
        vector<NetHostEvent>          _listEvent;
        OutgoingBatch                 _pendingBatch;     ///< 잠금 안에서 쌓인 보낼 데이터그램
        BitWriter                     _packetWriter;     ///< 보낼 패킷의 몸
        unordered_map<uint64, int32>  _mapSlotByAddress; ///< 주소 → 자리(연결이 많아도 패킷마다 선형 탐색하지 않게)
        TaskPromise<NetConnectResult> _connectPromise;   ///< 기다리는 비동기 연결(`_bConnectPending`)
        vector<FinishedConnect>       _listFinished;     ///< 끝난 비동기 연결 — 잠금을 풀고 채운다
        vector<ReceivedDatagram>      _listReceived;     ///< `update` 스레드 전용 — 잠금 밖에서 받은 묶음(버퍼를 다시 쓴다)
        OutgoingBatch                 _flushBatch;       ///< `update` 스레드 전용 — 잠금 밖에서 보내는 묶음
        vector<FinishedConnect>       _listDeliver;      ///< `update` 스레드 전용
        NetHostSettings               _settings;
        INetTransport*                _pTransport;
        uint64                        _saltState;
        uint64                        _rejectedPacketCount;
        atomic<uint32>                _updateDepth; ///< `update` 를 동시에 두 스레드가 부르는 실수를 잡는다
        int32                         _clientIndex;
        int32                         _receiveCursor; ///< 받기를 연결마다 고르게 돌리는 자리
        uint8                         _bServer;
        uint8                         _bConnectPending;
    };
} // namespace sw
