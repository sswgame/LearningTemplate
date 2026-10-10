/**
 * @file NetHost.h
 * @brief 서버 · 클라이언트 끝점 — 연결 핸드셰이크(요청 → 도전 → 응답 → 수락), 체크섬 · 프로토콜 id 로 남의 패킷 거르기, 연결 유지 · 타임아웃 · 끊기, 연결마다의 신뢰성 계층입니다.
 * @details 연결 요청에는 게임 id 와 프로토콜 id(게임 id + 와이어 판, `NetProtocol`)가 실린다. 서버는 프로토콜 id 가 다르면 이유를 붙여 거절한다 — 같은 게임의
 *          다른 판은 `VersionMismatch`, 다른 게임은 `Rejected`(양쪽 로그에 두 값을 남긴다). 요청 · 거절만 판과 상관없는 고정 머리(`NetProtocol::kHandshakeID`)로
 *          싸고, 나머지 패킷은 프로토콜 id 로 싸서 다른 판의 패킷은 체크섬부터 틀린다.
 *          장르별 네트워크 방식(클라이언트-서버 복제 · 락스텝 · 턴 중계 · MMO)은 이 위에 키트로 얹습니다. 여기는 "누가 연결됐고 어느 채널로 무엇이 왔는가" 까지입니다.
 *          도전(challenge) 단계는 위조한 주소로 서버에 연결 자리를 잡는 것을 막습니다 — 도전 값은 실제 그 주소로 간 패킷에만 들어 있습니다.
 *          서버는 요청에 상태를 남기지 않습니다(netcode.io connect token · QUIC Retry · 언리얼 StatelessConnectHandlerComponent): 도전 값은
 *          비밀 키 · 주소 · 클라이언트 소금 · 시간 칸에서 만들어 돌려주기만 하고, 그 값을 되돌려 준 응답이 와야 자리를 잡는다. 응답은 값을 만든 칸과
 *          다음 칸 안(`kChallengeWindowSeconds` 의 1~2 배)에만 통한다. 값은 키 섞기(splitmix)라 암호학적 MAC 은 아니다 — 위조 주소의 자리 채우기를 막는 데까지다.
 *
 *          **대역폭 상한**: 연결마다 토큰 버킷(`NetHostSettings::_maxBytesPerSecond`, 언리얼 `NetSpeed`)이다. 몫이 남으면 보내기 차례 하나에 패킷을 여럿
 *          (`kMaxPacketsPerSend` 까지) 보내고, 다 쓰면 메시지 없이 머리(확인 · 유지)만 보낸다 — 포화돼도 연결은 끊기지 않는다. 키트의 틱 예산은
 *          `NetSendBudget::computeTickBudget` 으로 이 값에서 몫을 받는다.
 *
 *          **스레드**: 공개 함수는 모두 잠금 하나로 지켜져 아무 스레드에서나 부를 수 있습니다(게임 스레드가 보내고, 작업 스레드가 꺼내고, 네트워크
 *          스레드가 `update` 한다). `update` 는 소켓 받기 · 보내기를 **잠금 밖에서** 묶어 하고 잠금 안에서는 패킷 처리만 하므로, 시스템 호출이
 *          보내는 쪽 스레드를 막지 않습니다. `update` 를 부르는 스레드는 한 번에 하나입니다 — 보통 `NetHostThread` 가 맡아 게임 프레임과
 *          상관없이 받기 · 확인 · 재전송 · 유지를 돌립니다(로딩 · 멈춘 프레임에 연결이 끊기지 않는다).
 *
 *          **암호화**(`NetHostSettings::_security`, 기본 꺼짐 — `NetHostSecurity.h`): 응답 · 수락에 일회 X25519 공개 키를 싣고, 서버는 상태 없는 도전을 통과한
 *          응답에만 키를 계산한다. 키 = HKDF(공유 비밀, 소금 = 세션 비밀), 데이터 · 끊기 패킷은 `[종류][연결 값][64 비트 번호][AEAD + 태그]`
 *          (AAD = 프로토콜 id ‖ 그 머리). 받는 쪽은 1024 재전송 창으로 거르고 **복호가 통과한 뒤에** 번호를 표시한다(먼저 표시하면 위조 패킷이 진짜 번호를 태운다).
 *          토큰 결속(서버 `INetConnectAuthenticator` · 클라이언트 `NetConnectCredentials`)이면 응답의 세션 비밀 증명 태그가 맞아야 자리를 잡고, 클라이언트는
 *          수락의 키 확인 태그로 서버가 같은 키를 가졌는지 본다. 방식 · 결속 여부는 프로토콜 id 에 섞여 협상하지 않는다 — 한쪽만이면 `SecurityMismatch`.
 *          인증기 없는 암호화(일회 키 — 중간자는 못 막는다)는 개발 빌드만: Shipping 서버는 인증기 없이 암호화로 `listen` 하지 못한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Connection/NetHostSecurity.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Security/NetReplayWindow.h"
#include "Core/Network/Security/NetSessionKeyUtil.h"
#include "Core/Task/TaskFuture.h"

namespace sw
{
    class INetAead;
    class INetTransport;

    /** @brief 호스트 설정입니다. 시간은 초입니다. */
    struct NetHostSettings
    {
        uint32              _gameID{ NetProtocol::kDefaultGameID }; ///< 게임마다 다르게 — 다르면 연결을 Rejected 로 거절한다
        uint32              _wireVersion{ 0 };                      ///< 게임 · 키트 층의 판(`NetWireVersion::combine`) — 다르면 VersionMismatch. Core 판은 저절로 섞인다
        uint64              _saltSeed{ 0 };                         ///< 도전 값 씨앗 — 0 이면 운영체제 난수(위조 연결을 막는다). 시험은 고정해도 된다
        float64             _timeoutSeconds{ 5.0 };
        float64             _connectTimeout{ 5.0 };
        float64             _connectRetryInterval{ 0.2 };
        float64             _sendInterval{ 1.0 / 30.0 }; ///< 연결마다 패킷을 보내는 가장 짧은 간격
        float64             _keepAliveInterval{ 0.25 };  ///< 보낼 것(메시지 · 재전송 · 확인)이 없으면 이 간격으로만 보낸다(유지 · RTT)
        int32               _maxConnections{ 16 };
        int32               _maxBytesPerSecond{ 100000 }; ///< 연결마다 보내는 바이트 상한(토큰 버킷 — 언리얼 `NetSpeed` 기본과 같다). 다 쓰면 메시지는 기다리고 확인 · 유지만 간다. 패킷 하나(1200) 아래는 그 값으로
        NetSecuritySettings _security{};                  ///< 암호화 · 토큰 결속(기본 꺼짐) — 양쪽이 같아야 연결된다
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
        int32               _connectionID{ -1 };
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
    /** @brief `NetInbound` 의 메시지 하나 — 바이트는 `NetInbound::_bytes` 의 [_offset, _offset + _size) 입니다(첫 바이트 = 종류). */
    struct NetInboundMessage
    {
        int32          _connectionID{ -1 };
        int32          _offset{ 0 };
        int32          _size{ 0 };
        NetChannelType _channel{ NetChannelType::ReliableOrdered };
    };
} // namespace sw

namespace sw
{
    /** @brief `NetHost::drainInbound` 가 잠금 한 번에 꺼낸 연결 사건과 받은 메시지입니다. 다시 쓰면 할당하지 않는다(바이트는 한 아레나에). */
    struct NetInbound
    {
        vector<NetHostEvent>      _listEvent{};
        vector<NetInboundMessage> _listMessage{};
        vector<uint8>             _bytes{};

        void clear()
        {
            _listEvent.clear();
            _listMessage.clear();
            _bytes.clear();
        }
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
     *     host.update( nowSeconds );                                    // 또는 netThread.start( &host ) — 그러면 update 를 부르지 않는다
     *     while ( host.receiveMessage( connectionID, channel, buffer ) ) { ... }
     *
     *     TaskFuture<NetConnectResult> future = client.connectAsync( serverAddress );
     *     future.then( []( const NetConnectResult& result ) { ... } ); // 결과는 update 를 돈 스레드에서 — 잠금 밖
     * @endcode
     */
    class SW_API NetHost
    {
    public:
        static constexpr int32   kMaxDatagramPerUpdate   = 512; ///< `update` 한 번에 받는 데이터그램 한도 — 나머지는 다음 번에(소켓 버퍼에 남는다)
        static constexpr int32   kMaxPacketsPerSend      = 8;   ///< 보내기 차례 하나에 연결마다 보내는 패킷 상한 — 대역폭 몫이 남고 실을 것이 있으면 여럿
        static constexpr float64 kChallengeWindowSeconds = 5.0; ///< 도전 값의 시간 칸 — 만든 칸과 다음 칸 동안 통한다

        NetHost();
        ~NetHost();

        NetHost( const NetHost& )            = delete;
        NetHost& operator=( const NetHost& ) = delete;

        /** @brief 전송과 설정을 정합니다. `update` 를 도는 스레드가 없을 때 부른다. */
        void               initialize( INetTransport* pTransport, const NetHostSettings& settings );
        [[nodiscard]] bool listen();
        [[nodiscard]] bool connect( const NetAddress& serverAddress );
        /** @brief 세션 토큰을 내밀며 연결합니다(토큰 결속 서버). 토큰 · 비밀은 로그인 키트가 TLS 로 받은 것입니다. */
        [[nodiscard]] bool connect( const NetAddress& serverAddress, const NetConnectCredentials& credentials );
        /**
         * @brief 비동기 연결 — 연결되거나 실패(거절 · 가득 참 · 타임아웃 · 끊음)하면 채워지는 future 를 돌려줍니다. 시작부터 못 하면 이미 채워진 future(Rejected)입니다.
         * @details 결과(와 `then` 후속 작업)는 그 일을 처리한 스레드(`update` · `disconnect` 를 부른 쪽)에서 잠금을 푼 뒤에 채워진다. 다시 `connect` 하면
         *          앞의 future 는 Requested 로 끝난다.
         */
        TaskFuture<NetConnectResult> connectAsync( const NetAddress& serverAddress );
        TaskFuture<NetConnectResult> connectAsync( const NetAddress& serverAddress, const NetConnectCredentials& credentials );
        /** @brief 받기(잠금 밖) → 처리 · 타임아웃 · 보낼 패킷 만들기(잠금 안) → 보내기(잠금 밖)입니다. 한 번에 한 스레드만 부른다. */
        void update( float64 nowSeconds );
        /** @brief 받을 데이터그램이 생기거나 @p timeoutSeconds 가 지날 때까지 잠듭니다(`update` 를 도는 스레드가 부른다). */
        bool waitForReceive( float64 timeoutSeconds );

        [[nodiscard]] bool sendMessage( int32 connectionID, NetChannelType channel, const uint8* pData, int32 size );
        [[nodiscard]] bool sendMessage( int32 connectionID, NetChannelType channel, const vector<uint8>& buffer )
        {
            return sendMessage( connectionID, channel, buffer.data(), static_cast<int32>( buffer.size() ) );
        }
        /** @brief 연결된 모두에게 보냅니다(@p exceptID 는 빼고). 보낸 수입니다. */
        int32 broadcast( NetChannelType channel, const uint8* pData, int32 size, int32 exceptID = -1 );
        /** @brief 받은 메시지 하나를 꺼냅니다 — 연결 순, 채널은 신뢰 순서 → 신뢰 순서 없음 → 순서만 → 비신뢰 순(채널 값 순). */
        [[nodiscard]] bool receiveMessage( int32& outConnectionID, NetChannelType& outChannel, vector<uint8>& outBuffer );
        void               disconnect( int32 connectionID );
        void               disconnectAll();
        void               drainEvents( vector<NetHostEvent>& outListEvent );
        /**
         * @brief 쌓인 연결 사건과 받은 메시지를 **잠금 한 번에** 모두 꺼냅니다(@p outInbound 를 비우고 채운다). 사건이 메시지보다 먼저입니다.
         * @details 닫힌 연결의 받은 메시지는 닫을 때 지워지므로, 꺼낸 메시지는 모두 지금 그 자리의 연결 것이다 — 사건을 먼저 처리하면 같은 자리에 새로 온
         *          연결의 메시지를 옛 연결의 상태로 읽지 않는다. `receiveMessage` · `drainEvents` 와 섞어 쓰지 않는다(`NetMessageRouter::pump` 가 쓴다).
         */
        void drainInbound( NetInbound& outInbound );

        bool               isServer() const;
        NetConnectionState getConnectionState( int32 connectionID ) const;
        /** @brief 연결 통계의 사본입니다(연결돼 있지 않으면 false). 다른 스레드가 `update` 하는 중에도 안전합니다. */
        [[nodiscard]] bool getConnectionStats( int32 connectionID, NetConnectionStats& outStats ) const;
        /**
         * @brief 연결 객체를 그대로 빌려줍니다.
         * @warning 잠금 밖으로 나가는 포인터입니다 — `update` 를 도는 다른 스레드가 없을 때만(한 스레드로 쓸 때 · 시험). 스레드가 돌면 `getConnectionStats`.
         */
        const NetConnection* findConnection( int32 connectionID ) const;
        NetAddress           getConnectionAddress( int32 connectionID ) const;
        int32                getConnectedCount() const;
        /** @brief 클라이언트 — 서버가 준 자기 번호(서버의 연결 id)입니다. 연결 전이면 −1 입니다. */
        int32  getClientIndex() const;
        void   collectConnected( vector<int32>& outListConnection ) const;
        uint64 getRejectedPacketCount() const;
        /** @brief 서버 — 인증기가 이 연결에 준 주체(로그인 계정)입니다. 없으면 0 입니다. */
        uint64 getConnectionPrincipal( int32 connectionID ) const;
        /** @brief 복호 · 증명 · 키 확인 실패로 버린 패킷 수입니다(진단 · 시험). */
        uint64 getAuthenticationFailureCount() const;
        /** @brief 재전송 방지 창이 버린 패킷 수입니다. */
        uint64 getReplayRejectedCount() const;
        /** @brief 이 호스트의 프로토콜 id(`NetProtocol::makeProtocolID( 게임 id, 와이어 판 )`)입니다. */
        uint32 getProtocolID() const;
        /** @brief 연결마다의 보내기 상한(초당 바이트, `NetHostSettings::_maxBytesPerSecond`)입니다 — 키트가 틱 예산을 셈한다(`NetSendBudget::computeTickBudget`). */
        int32 getMaxBytesPerSecond() const;

        /** @brief 패킷 몸의 첫 3 비트 — 핸드셰이크 단계 · 데이터 · 끊김입니다. */
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

        /** @brief 데이터그램(머리 포함)의 패킷 종류를 엿봅니다 — 체크섬은 보지 않는다. 너무 짧으면 Count 입니다(흉내 거르개 · 진단). */
        static PacketType peekPacketType( const uint8* pData, int32 size );
        /** @brief 패킷 체크섬(머리 값을 먼저 섞은 FNV-1a)입니다 — 시험 · 진단 도구가 고친 패킷의 체크섬을 다시 맞출 때. */
        static uint32 computePacketChecksum( uint32 headerID, const uint8* pBody, int32 size );

    private:
        /** @brief 판과 상관없는 고정 머리(`NetProtocol::kHandshakeID`)로 싸는 패킷 — 요청과 거절뿐입니다. 두 패킷의 배치는 판이 바뀌어도 그대로 둔다. */
        static bool isHandshakeFramed( PacketType type ) { return type == PacketType::ConnectRequest || type == PacketType::Denied; }

        /** @brief 연결 하나의 암호 상태 — 암호화 방식일 때만 있다. */
        struct SlotSecurity
        {
            NetX25519KeyPair     _keyPair{};      ///< 이쪽의 일회 키(클라이언트는 키를 만든 뒤 지운다, 서버는 공개 키만 남긴다 — 수락 재전송)
            NetSessionKeys       _keys{};         ///< 방향별 키 · IV
            unique_ptr<INetAead> _sendAead{};     ///< 보내는 방향 키를 박은 AEAD
            unique_ptr<INetAead> _receiveAead{};  ///< 받는 방향 키를 박은 AEAD
            NetReplayWindow      _replayWindow{}; ///< 받은 패킷 번호
            uint64               _sendPacketNumber{ 0 };
            uint64               _principalID{ 0 };                                      ///< 서버 — 인증기가 준 주체
            uint8                _arrKeyConfirmTag[NetSecurityConstant::kAeadTagSize]{}; ///< 서버 — 수락을 다시 보낼 때 같은 태그
            uint8                _bKeysReady{ SW_FALSE };
        };

        struct Slot
        {
            unique_ptr<SlotSecurity> _security{}; ///< 암호화 방식일 때만
            NetConnection            _connection{};
            NetAddress               _address{};
            uint64                   _clientSalt{ 0 };
            uint64                   _serverSalt{ 0 };
            float64                  _lastReceiveTime{ 0.0 };
            float64                  _lastSendTime{ -1.0 };
            float64                  _connectStartTime{ 0.0 };
            float64                  _sendCredit{ 0.0 };                         ///< 대역폭 몫(바이트) — 0 보다 크면 꽉 찬 패킷을 보낼 수 있다(빚 모양 — 보낸 뒤 음수가 될 수 있다)
            float64                  _lastCreditTime{ -1.0 };                    ///< 몫을 마지막으로 채운 때 — 음수면 아직(연결되면 가득 채운다)
            NetConnectionState       _state{ NetConnectionState::Disconnected }; ///< 서버의 자리는 Connecting 을 거치지 않는다(응답이 맞아야 잡는다)
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
        void handlePacket( float64 nowSeconds, const NetAddress& from, const uint8* pData, int32 size );
        void updateSlots( float64 nowSeconds );
        void sendControl( const NetAddress& to, PacketType type, uint64 valueA, uint64 valueB );
        /** @brief 연결 요청을 거절합니다 — 이유와 이 호스트의 프로토콜 id, 요청의 클라이언트 소금(위조 거절을 거르는 값)을 싣는다. */
        void sendDenied( const NetAddress& to, NetDisconnectReason reason, uint64 clientSalt );
        /** @brief 클라이언트 — 도전 응답(암호화면 공개 키 · [토큰 · 증명 태그])을 보냅니다. */
        void sendChallengeResponse( Slot& slot );
        /** @brief 서버 — 수락(암호화면 공개 키 · 키 확인 태그)을 보냅니다. */
        void sendAccepted( int32 slotIndex );
        /** @brief 서버 — 응답의 공개 키 · 토큰 · 증명을 읽고 검증해 키를 만듭니다. 틀리면 거절을 보내고 false. */
        [[nodiscard]] bool acceptSecureResponse( BitReader& reader, const NetAddress& from, uint64 clientSalt, uint64 challenge, unique_ptr<SlotSecurity>& outSecurity );
        /** @brief 클라이언트 — 수락의 공개 키 · 키 확인 태그를 읽고 키를 만듭니다. 틀리면 연결을 닫고 false. */
        [[nodiscard]] bool acceptSecureAccepted( BitReader& reader, Slot& slot, int32 slotIndex, uint32 serverIndex );
        /** @brief 암호화된 데이터 · 끊기 패킷을 짓습니다(@p pPlain 을 봉인). 보낸 데이터그램 바이트 수, 실패면 0 입니다. */
        int32 sendSealed( Slot& slot, PacketType type, const uint8* pPlain, int32 plainSize );
        /** @brief 암호화된 데이터 · 끊기 몸(머리 8 바이트 뒤)을 엽니다 — 재전송 창 · 태그 검사. 연 평문은 `_listOpenScratch` 입니다. */
        [[nodiscard]] bool openSealed( Slot& slot, const uint8* pBody, int32 bodySize );
        /** @brief 암호화 방식의 데이터 · 끊기 패킷(머리 8 바이트 뒤 몸)을 처리합니다. */
        void handleSealedPacket( float64 nowSeconds, PacketType type, int32 slotIndex, const uint8* pBody, int32 bodySize );
        /** @brief 이 호스트의 기능 마스크(`NetProtocolFeature`)입니다 — 역할 · 자격이 정해진 뒤(listen · connect) 프로토콜 id 에 섞는다. */
        uint32 computeFeatureMask() const;
        bool   isEncrypted() const { return _settings._security._mode == NetSecurityMode::Encrypted; }
        /** @brief `_packetWriter` 의 몸에 헤더(머리 값 · 체크섬)를 붙여 보낼 묶음에 넣습니다. */
        void sendFramed( const NetAddress& to, uint32 headerID );
        /** @brief 패킷 하나를 씁니다 — 몫이 남았으면 메시지까지, 다 썼으면 머리(확인)만. 몫에서 보낸 바이트를 뺀다. */
        void sendPayload( float64 nowSeconds, Slot& slot );
        /** @brief 지난 채움 뒤 흐른 시간만큼 대역폭 몫을 채웁니다(상한 — 보내기 간격 두 번어치, 최소 패킷 하나). */
        void                         refillSendCredit( float64 nowSeconds, Slot& slot ) const;
        void                         closeSlot( int32 slotIndex, NetDisconnectReason reason, bool bNotifyRemote );
        bool                         startConnect( const NetAddress& serverAddress );
        bool                         connectLocked( const NetAddress& serverAddress, const NetConnectCredentials* pCredentials );
        TaskFuture<NetConnectResult> connectAsyncWith( const NetAddress& serverAddress, const NetConnectCredentials* pCredentials );
        void                         pushEvent( const NetHostEvent& event );
        void                         finishConnect( NetDisconnectReason reason );
        bool                         sendMessageLocked( int32 connectionID, NetChannelType channel, const uint8* pData, int32 size );
        void                         takePending( OutgoingBatch& outBatch, vector<FinishedConnect>& outListFinished );

        // 잠금 밖에서.
        void        sendBatch( const OutgoingBatch& batch );
        static void deliverFinished( vector<FinishedConnect>& listFinished );

        int32  findSlotByAddress( const NetAddress& address ) const;
        void   bindAddress( int32 slotIndex, const NetAddress& address );
        void   unbindAddress( int32 slotIndex );
        int32  findFreeSlot() const;
        uint64 nextSalt();
        /** @brief 상태 없는 도전 값 — 비밀 키 · 주소 · 클라이언트 소금 · 시간 칸을 섞는다. 0 이 아니다. */
        uint64       makeChallengeToken( const NetAddress& address, uint64 clientSalt, int64 window ) const;
        static int64 computeChallengeWindow( float64 nowSeconds );
        bool         isValidSlot( int32 slotIndex ) const { return slotIndex >= 0 && slotIndex < static_cast<int32>( _listSlot.size() ); }

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
        vector<uint8>                 _listDrainScratch; ///< `drainInbound` 가 메시지 하나를 꺼내 두는 자리
        BitWriter                     _plainWriter;      ///< 암호화 — NetConnection 이 쓴 평문 패킷
        vector<uint8>                 _listSealScratch;  ///< 암호화 — 봉인한 암호문 + 태그
        vector<uint8>                 _listOpenScratch;  ///< 암호화 — 연 평문
        NetConnectCredentials         _credentials;      ///< 클라이언트 — 지금 연결에 내미는 것
        NetHostSettings               _settings;
        INetTransport*                _pTransport;
        uint64                        _saltState;
        uint64                        _rejectedPacketCount;
        uint64                        _authenticationFailureCount;
        uint64                        _replayRejectedCount;
        uint64                        _mismatchLogCount;
        uint64                        _challengeSecret; ///< 서버 — 도전 값의 비밀 키(소금 씨앗에서) ///< 판 · 게임이 다른 요청 수 — 로그는 1 · 2 · 4 · 8 … 번째에만(요청 폭주가 로그를 메우지 않게)
        uint32                        _protocolID;
        atomic<uint32>                _updateDepth; ///< `update` 를 동시에 두 스레드가 부르는 실수를 잡는다
        int32                         _clientIndex;
        int32                         _receiveCursor; ///< 받기를 연결마다 고르게 돌리는 자리
        uint8                         _bServer;
        uint8                         _bConnectPending;
        uint8                         _bHasCredentials; ///< 클라이언트 — 자격을 내미는 연결(토큰 결속)
    };
} // namespace sw
