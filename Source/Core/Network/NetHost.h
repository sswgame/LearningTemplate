/**
 * @file NetHost.h
 * @brief 서버 · 클라이언트 끝점 — 연결 핸드셰이크(요청 → 도전 → 응답 → 수락), 체크섬 · 프로토콜 id 로 남의 패킷 거르기, 연결 유지 · 타임아웃 · 끊기, 연결마다의 신뢰성 계층입니다.
 * @details 장르별 네트워크 방식(클라이언트-서버 복제 · 락스텝 · 턴 중계 · MMO)은 이 위에 키트로 얹습니다. 여기는 "누가 연결됐고 어느 채널로 무엇이 왔는가" 까지입니다.
 *          도전(challenge) 단계는 위조한 주소로 서버에 연결 자리를 잡는 것을 막습니다 — 도전 값은 실제 그 주소로 간 패킷에만 들어 있습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Network/NetConnection.h"
#include "Core/Network/NetTypes.h"

namespace sw
{
    class INetTransport;

    /** @brief 호스트 설정입니다. 시간은 초입니다. */
    struct NetHostSettings
    {
        uint32  _protocolId{ 0x53574E31u }; ///< 게임 · 버전마다 다르게(다르면 서로의 패킷을 버린다)
        uint64  _saltSeed{ 0x1234ABCDull }; ///< 도전 값 씨앗(시험에서 고정)
        float32 _timeout{ 5.0f };
        float32 _connectTimeout{ 5.0f };
        float32 _connectRetryInterval{ 0.2f };
        float32 _sendInterval{ 1.0f / 30.0f }; ///< 연결마다 패킷을 보내는 간격(보낼 것이 없어도 확인 · 유지용으로 보낸다)
        int32   _maxConnections{ 16 };
    };

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

    /**
     * @class NetHost
     * @brief 서버는 `listen`, 클라이언트는 `connect` 로 시작해 매 프레임 `update( 시각 )` 합니다. 클라이언트의 서버 연결은 id 0 입니다.
     * @code
     *     host.initialize( &transport, settings );
     *     (void)host.listen();
     *     host.update( time );
     *     while ( host.receiveMessage( connectionId, channel, buffer ) ) { ... }
     * @endcode
     */
    class SW_API NetHost
    {
    public:
        NetHost();

        void               initialize( INetTransport* pTransport, const NetHostSettings& settings );
        [[nodiscard]] bool listen();
        [[nodiscard]] bool connect( const NetAddress& serverAddress );
        /** @brief 받기 → 처리 → 타임아웃 → 보내기입니다. */
        void update( float64 time );

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

        bool                 isServer() const { return _bServer != SW_FALSE; }
        NetConnectionState   getConnectionState( int32 connectionId ) const;
        const NetConnection* findConnection( int32 connectionId ) const;
        NetAddress           getConnectionAddress( int32 connectionId ) const;
        int32                getConnectedCount() const;
        /** @brief 클라이언트 — 서버가 준 자기 번호(서버의 연결 id)입니다. 연결 전이면 −1 입니다. */
        int32  getClientIndex() const { return _clientIndex; }
        void   collectConnected( vector<int32>& outListConnection ) const;
        uint64 getRejectedPacketCount() const { return _rejectedPacketCount; }

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

        void   receivePackets( float64 time );
        void   handlePacket( float64 time, const NetAddress& from, const vector<uint8>& buffer );
        void   sendControl( const NetAddress& to, PacketType type, uint64 valueA, uint64 valueB );
        void   sendPayload( float64 time, Slot& slot );
        void   closeSlot( int32 slotIndex, NetDisconnectReason reason, bool bNotifyRemote );
        int32  findSlotByAddress( const NetAddress& address ) const;
        int32  findFreeSlot() const;
        uint64 nextSalt();
        bool   isValidSlot( int32 slotIndex ) const { return slotIndex >= 0 && slotIndex < static_cast<int32>( _listSlot.size() ); }

        vector<Slot>         _listSlot;
        vector<NetHostEvent> _listEvent;
        vector<uint8>        _receiveBuffer;
        NetHostSettings      _settings;
        INetTransport*       _pTransport;
        uint64               _saltState;
        uint64               _rejectedPacketCount;
        int32                _clientIndex;
        int32                _receiveCursor; ///< 받기를 연결마다 고르게 돌리는 자리
        uint8                _bServer;
    };
} // namespace sw
