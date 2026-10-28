/**
 * @file NetHost.h
 * @brief 서버와 클라이언트의 엔드포인트를 구현합니다.
 * @details 연결 핸드셰이크(요청, 챌린지, 응답, 수락)를 처리하고, 체크섬과 프로토콜 ID로 다른 프로그램의 패킷을 걸러 낸다.
 *          연결 유지, 타임아웃, 연결 끊기, 연결별 신뢰성 계층도 여기서 맡는다.
 *
 *          연결 요청에는 게임 ID와 프로토콜 ID(게임 ID에 와이어 버전을 합친 값, `NetProtocol`)가 실린다.
 *          서버는 프로토콜 ID가 다르면 이유를 붙여 거절한다. 같은 게임의 다른 버전은 `VersionMismatch`, 다른 게임은 `Rejected` 이고, 양쪽 로그에 두 값을 남긴다.
 *          요청과 거절 패킷만 버전과 상관없는 고정 헤더(`NetProtocol::kHandshakeID`)로 싼다.
 *          나머지 패킷은 프로토콜 ID로 싸므로, 다른 버전의 패킷은 체크섬부터 맞지 않는다.
 *
 *          클라이언트-서버 복제, 락스텝, 턴 중계, MMO 같은 장르별 네트워크 방식은 이 계층 위에 키트로 구현한다.
 *          이 계층의 일은 "누가 연결됐고 어느 채널로 무엇이 왔는가"까지다.
 *
 *          챌린지 단계는 위조한 주소로 서버의 연결 슬롯을 차지하는 공격을 막는다. 챌린지 값은 실제로 그 주소로 보낸 패킷에만 들어 있다.
 *          서버는 요청에 상태를 남기지 않는다(netcode.io connect token, QUIC Retry, 언리얼 StatelessConnectHandlerComponent와 같은 방식).
 *          챌린지 값은 비밀 키, 주소, 클라이언트 솔트, 시간 윈도우를 섞어 만들어 돌려주기만 한다. 그 값을 되돌려 주는 응답이 와야 슬롯을 할당한다.
 *          응답은 값을 만든 윈도우와 그다음 윈도우 안(`NetChallenge::kWindowSeconds` 의 1~2배)에만 유효하다.
 *          값은 키를 섞는 splitmix 이므로 암호학적 MAC은 아니다. 위조한 주소로 슬롯을 채우는 공격을 막는 데까지만 쓴다.
 *
 *          **책임 나누기**: 이 클래스는 패킷 형식(체크섬 헤더, 패킷 종류), 핸드셰이크 순서, 연결 수명, 대역폭, 송신 배치를 맡는다.
 *          챌린지 값과 솔트는 `NetChallenge`, 슬롯과 주소 맵은 `NetSlotTable`, AEAD 봉인과 키 유도는 `NetPacketSealer` 가 맡는다.
 *          셋 다 가상 함수 없는 값 멤버이고 잠금이 없으며, 이 호스트의 잠금(`_mutex`) 안에서만 쓴다.
 *
 *          **대역폭 상한**: 연결마다 토큰 버킷(`NetHostConfig::_maxBytesPerSecond`, 언리얼의 `NetSpeed`)을 둔다.
 *          토큰이 남아 있으면 한 번의 전송에 패킷을 여럿(`kMaxPacketsPerSend` 까지) 보낸다.
 *          토큰을 다 쓰면 메시지 없이 헤더(확인과 연결 유지)만 보내므로, 대역폭이 포화돼도 연결은 끊기지 않는다.
 *          키트의 틱 예산은 `NetSendBudget::computeTickBudget` 이 이 값에서 할당받는다.
 *
 *          **스레드**: 공개 함수는 모두 잠금 하나로 보호하므로 어느 스레드에서나 부를 수 있다.
 *          게임 스레드가 보내고, 작업 스레드가 꺼내고, 네트워크 스레드가 `update` 한다.
 *          `update` 는 소켓 받기와 보내기를 **잠금 밖에서** 묶어 처리하고 잠금 안에서는 패킷 처리만 하므로, 시스템 호출이 보내는 쪽 스레드를 막지 않는다.
 *          `update` 를 부르는 스레드는 한 번에 하나여야 한다. 보통 `NetHostThread` 가 맡아 게임 프레임과 상관없이 받기, 확인, 재전송, 연결 유지를 돌린다.
 *          그래서 로딩이나 프레임 멈춤 중에도 연결이 끊기지 않는다.
 *          멤버의 소유: `_listReceived` · `_flushBatch` · `_listDeliver` 는 `update` 스레드 전용(잠금 밖, `_updateThread` 역할), 나머지는 모두 `_mutex` 아래다(선언의 `SW_GUARDED_BY` · `SW_REQUIRES_LOCK` · `SW_EXCLUDES_LOCK` 을 clang `-Wthread-safety` 가 본다).
 *          잠금은 `_mutex` 하나뿐이고, 잠금을 쥔 채 부르는 바깥 코드는 `INetConnectAuthenticator` 와 `INetSecurityProvider` 뿐이다(둘 다 `NetHost` 를 부르지 않는다).
 *          전송의 `receive` · `send` 와 비동기 연결 결과(`TaskPromise::setValue`)는 잠금 밖에서 부른다.
 *
 *          **암호화**(`NetHostConfig::_security`, 기본값은 꺼짐, `NetHostSecurity.h`): 응답과 수락에 일회용 X25519 공개 키를 싣는다.
 *          서버는 상태 없는 챌린지를 통과한 응답에만 키를 계산한다. 키는 HKDF(공유 비밀, 솔트는 세션 비밀)로 만든다.
 *          데이터 패킷과 끊기 패킷은 `[종류][연결 값][64비트 번호][AEAD + 태그]` 형식이고, AAD는 프로토콜 ID와 이 헤더를 이은 값이다.
 *          받는 쪽은 1024개 크기의 재전송 방지 윈도우로 걸러 내고, **복호가 성공한 뒤에** 번호를 표시한다. 먼저 표시하면 위조 패킷이 진짜 번호를 소모시킨다.
 *          토큰 결속(서버는 `INetConnectAuthenticator`, 클라이언트는 `NetConnectCredentials`)을 쓰면 응답의 세션 비밀 증명 태그가 맞아야 슬롯을 할당한다.
 *          클라이언트는 수락에 든 키 확인 태그로 서버가 같은 키를 가졌는지 확인한다.
 *          암호화 방식과 토큰 결속 여부는 프로토콜 ID에 섞이지 않고 협상하지도 않는다. 한쪽만 켜면 `SecurityMismatch` 가 된다.
 *          인증기 없는 암호화(일회용 키라 중간자 공격은 막지 못한다)는 개발 빌드에서만 허용한다. Shipping 서버는 인증기 없이 암호화로 `listen` 할 수 없다.
 *
 *          **디버거 대기**(`NetHostConfig::_debugHold`, 개발 빌드만): 디버거가 붙은 호스트는 데이터 패킷마다 1비트로 이 사실을 알린다.
 *          상대는 그 연결의 타임아웃을 `_debugHoldTimeoutSeconds` 로 늘리므로, 한쪽이 중단점에 멈춰도 다른 쪽이 연결을 끊지 않는다.
 *          멈췄던 쪽은 `update` 사이의 빈 시간만큼 연결 시계를 앞으로 민다.
 *          Shipping 은 이 비트를 보내지도 믿지도 않는다. 비트를 켠 클라이언트가 슬롯을 계속 붙잡는 일을 막기 위해서다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Network/Connection/NetChallenge.h"
#include "Core/Network/Connection/NetHostSecurity.h"
#include "Core/Network/Connection/NetPacketSealer.h"
#include "Core/Network/Connection/NetSlotTable.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/NetTransport.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Task/TaskFuture.h"

namespace sw
{
    struct NetConnectionStats;

    class INetTransport;
    class NetConnection;

    /**
     * @brief 이 호스트가 중단점에 멈출 수 있다고 상대에게 알릴 시점을 정합니다.
     * @details 개발 빌드에서만 동작한다. Shipping 은 알리지 않고 상대의 알림도 무시한다.
     *          알리는 쪽은 데이터 패킷마다 1비트를 켠다. 받는 쪽은 그 연결의 타임아웃을 `NetHostConfig::_debugHoldTimeoutSeconds` 로 늘린다.
     *          알리는 쪽 자신은 `update` 사이가 크게 벌어지면(프로세스가 멈췄다고 보고) 그만큼 시계가 멈춘 것으로 간주한다(가상 시계).
     */
    enum class NetDebugHold : uint8
    {
        Off = 0,              ///< 알리지 않는다. 타임아웃 테스트나 디버거 아래에서 원래 타임아웃을 보려는 경우에 쓴다.
        WhenDebuggerAttached, ///< 이 프로세스에 디버거가 붙어 있을 때만 알린다(`Process::isDebuggerPresent`, 1초마다 다시 확인한다).
        Always                ///< 항상 알린다. 테스트나 디버거 없이 멈추는 도구에 쓴다.
    };
} // namespace sw

namespace sw
{
    /** @brief 호스트 설정입니다. 시간은 초입니다. */
    struct NetHostConfig
    {
        uint32            _gameID{ NetProtocol::kDefaultGameID }; ///< 게임마다 다르게 정한다. 다르면 연결을 Rejected 로 거절한다.
        uint32            _wireVersion{ 0 };                      ///< 게임과 키트 계층의 버전(`NetWireVersion::combine`). 다르면 VersionMismatch 가 된다. Core 버전은 자동으로 섞인다.
        uint64            _saltSeed{ 0 };                         ///< 챌린지 값의 시드. 0이면 운영체제 난수를 쓴다(위조 연결을 막는다). 테스트에서는 고정해도 된다.
        float64           _timeoutSeconds{ 5.0 };
        float64           _connectTimeout{ 5.0 };
        float64           _connectRetryInterval{ 0.2 };
        float64           _sendInterval{ 1.0 / 30.0 };                                         ///< 연결마다 패킷을 보내는 최소 간격.
        float64           _keepAliveInterval{ 0.25 };                                          ///< 보낼 것(메시지, 재전송, 확인)이 없으면 이 간격으로만 보낸다(연결 유지와 RTT 측정).
        float64           _debugHoldTimeoutSeconds{ 600.0 };                                   ///< 상대가 디버거 대기를 알린 연결의 타임아웃(개발 빌드만). 중단점에 오래 멈춰도 끊지 않는다.
        float64           _maxUnreliableAgeSeconds{ NetConnection::kDefaultMaxUnreliableAge }; ///< 비신뢰 메시지가 보낼 줄에서 기다릴 수 있는 시간. 넘으면 버린다(0 = 상한 없음).
        int32             _maxConnections{ 16 };
        int32             _maxBytesPerSecond{ 100000 }; ///< 연결마다 보내는 바이트 상한(토큰 버킷, 언리얼 `NetSpeed` 기본값과 같다). 다 쓰면 메시지는 기다리고 확인과 연결 유지 패킷만 나간다. 패킷 하나(1200바이트)보다 작은 값은 패킷 하나 크기로 올려 쓴다.
        NetSecurityConfig _security{};                  ///< 암호화와 토큰 결속 설정(기본값은 꺼짐). 양쪽이 같아야 연결된다.
        NetDebugHold      _debugHold{ NetDebugHold::WhenDebuggerAttached };
    };
} // namespace sw

namespace sw
{
    /** @brief 호스트에서 일어난 사건입니다. */
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
    /** @brief `NetInbound` 에 든 메시지 하나입니다. 바이트는 `NetInbound::_bytes` 의 [_offset, _offset + _size) 범위이고, 첫 바이트가 메시지 종류입니다. */
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
    /** @brief `NetHost::drainInbound` 가 잠금 한 번에 꺼낸 연결 사건과 받은 메시지입니다. 다시 쓰면 할당하지 않으며, 바이트는 아레나 하나에 모읍니다. */
    struct NetInbound
    {
        vector<NetHostEvent>      _listEvent{};
        vector<NetInboundMessage> _listMessage{};
        vector<NetDeliveryNotice> _listDeliveryNotice{}; ///< 판정이 난 전달 통지(`INetPeers::sendNotifiedMessage`). 연결 ID 가 채워져 있다
        vector<uint8>             _bytes{};

        void clear()
        {
            _listEvent.clear();
            _listMessage.clear();
            _listDeliveryNotice.clear();
            _bytes.clear();
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @class INetPeers
     * @brief 키트 · 라우터가 연결에 보내고 연결을 묻는 창구입니다. 호스트 하나(`NetHost`)도, 포트 샤드 여럿을 묶은 것(`NetHostShardSet`)도 이 모양입니다.
     * @details 키트는 호스트가 몇 개인지 모른다 — 연결 ID 만 다룬다. 샤드 묶음은 연결 ID 에 샤드 번호를 접어 넣으므로(`NetHostShardSet`) 같은 ID 가 두 샤드에서 겹치지 않는다.
     *          스레드: 구현은 모든 함수를 어느 스레드에서나 부를 수 있게 한다(`NetHost` 의 잠금 약속과 같다). `drainInbound` 만 한 스레드(라우터를 pump 하는 게임 스레드)가 부른다.
     */
    class SW_API INetPeers
    {
    public:
        INetPeers()          = default;
        virtual ~INetPeers() = default;

        INetPeers( const INetPeers& )            = delete;
        INetPeers& operator=( const INetPeers& ) = delete;

        [[nodiscard]] virtual bool sendMessage( int32 connectionID, NetChannelType channel, const uint8* pData, int32 size ) = 0;
        [[nodiscard]] bool         sendMessage( int32 connectionID, NetChannelType channel, const vector<uint8>& buffer )
        {
            return sendMessage( connectionID, channel, buffer.data(), static_cast<int32>( buffer.size() ) );
        }
        /**
         * @brief 비신뢰 메시지를 전달 통지 @p tag 를 달아 보냅니다(`NetConnection::sendNotifiedMessage`). 판정은 `drainInbound` 의 `_listDeliveryNotice` 로 옵니다.
         * @details 키트가 "받았다" 확인 메시지를 따로 주고받지 않게 하는 창구다(언리얼 `PacketNotify`). 연결이 없거나 메시지가 틀리면 false 이고 통지도 나지 않는다.
         */
        [[nodiscard]] virtual bool sendNotifiedMessage( int32 connectionID, const uint8* pData, int32 size, uint32 tag ) = 0;
        /** @brief 연결된 모두에게 보냅니다. @p exceptID 는 제외합니다(-1 이면 제외 없음). 반환값은 보낸 수입니다. */
        virtual int32 broadcast( NetChannelType channel, const uint8* pData, int32 size, int32 exceptID = -1 ) = 0;
        virtual void  disconnect( int32 connectionID )                                                         = 0;
        /** @brief 쌓인 연결 사건과 받은 메시지를 모두 꺼냅니다(`NetHost::drainInbound` 와 같은 약속 — 사건이 메시지보다 앞에 온다). */
        virtual void               drainInbound( NetInbound& outInbound )                                       = 0;
        virtual bool               isServer() const                                                             = 0;
        virtual NetConnectionState getConnectionState( int32 connectionID ) const                               = 0;
        [[nodiscard]] virtual bool getConnectionStats( int32 connectionID, NetConnectionStats& outStats ) const = 0;
        virtual NetAddress         getConnectionAddress( int32 connectionID ) const                             = 0;
        /** @brief 클라이언트에서 서버가 준 자기 번호(서버 쪽 연결 ID)입니다. 연결 전이거나 서버면 -1 입니다. */
        virtual int32 getClientIndex() const                                     = 0;
        virtual int32 getConnectedCount() const                                  = 0;
        virtual void  collectConnected( vector<int32>& outListConnection ) const = 0;
        /** @brief 연결마다의 보내기 상한(초당 바이트)입니다. 샤드 묶음은 샤드마다 같은 설정을 쓴다. */
        virtual int32 getMaxBytesPerSecond() const                      = 0;
        virtual bool  isConnectionSaturated( int32 connectionID ) const = 0;
        /** @brief 큐에 넣은 메시지의 종류별 셈입니다. @p outTable 에 **더합니다**(샤드 묶음은 샤드마다 더한다) — 새로 셀 때는 빈 표를 넘긴다. */
        virtual void addSentKindTally( NetMessageKindTallyTable& outTable ) const = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetHost
     * @brief 서버는 `listen`, 클라이언트는 `connect` 로 시작한 뒤 `update( 시각 )` 를 부르는 네트워크 호스트입니다.
     * @details `update` 는 매 프레임 직접 부르거나 `NetHostThread` 에 맡긴다. 클라이언트에서 서버 연결의 ID는 0이다.
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
    class SW_API NetHost final : public INetPeers
    {
    public:
        static constexpr int32 kMaxDatagramPerUpdate = 512; ///< `update` 한 번에 받는 데이터그램 한도. 나머지는 소켓 버퍼에 남겨 다음 호출에서 받는다.
        static constexpr int32 kReceiveBatchSize     = 64;  ///< 일괄 받기 한 번에 받는 데이터그램 수. 시스템 호출 한 번이 이만큼을 받는다(`INetTransport::receiveBatch`).
        static constexpr int32 kMaxPacketsPerSend    = 8;   ///< 한 번의 전송에서 연결마다 보내는 패킷 상한. 대역폭 토큰이 남고 보낼 것이 있으면 여러 개를 보낸다.

        NetHost();
        ~NetHost() override;

        NetHost( const NetHost& )            = delete;
        NetHost& operator=( const NetHost& ) = delete;

        /** @brief 전송과 설정을 정합니다. `update` 를 도는 스레드가 없을 때 부른다. */
        void               initialize( INetTransport* pTransport, const NetHostConfig& settings ) SW_EXCLUDES_LOCK( _mutex );
        [[nodiscard]] bool listen() SW_EXCLUDES_LOCK( _mutex );
        [[nodiscard]] bool connect( const NetAddress& serverAddress ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief 세션 토큰을 내밀며 연결합니다(토큰 결속 서버용). 토큰과 비밀은 로그인 키트가 TLS 로 받은 값입니다. */
        [[nodiscard]] bool connect( const NetAddress& serverAddress, const NetConnectCredentials& credentials ) SW_EXCLUDES_LOCK( _mutex );
        /**
         * @brief 비동기로 연결하고, 연결되거나 실패(거절, 정원 초과, 타임아웃, 끊김)하면 채워지는 future 를 돌려줍니다.
         * @details 시작부터 실패하면 이미 채워진 future(Rejected)를 돌려준다.
         *          결과와 `then` 후속 작업은 그 일을 처리한 스레드(`update` 나 `disconnect` 를 부른 쪽)에서 잠금을 푼 뒤에 채운다.
         *          다시 `connect` 하면 앞의 future 는 Requested 로 끝난다.
         */
        TaskFuture<NetConnectResult> connectAsync( const NetAddress& serverAddress ) SW_EXCLUDES_LOCK( _mutex );
        TaskFuture<NetConnectResult> connectAsync( const NetAddress& serverAddress, const NetConnectCredentials& credentials ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief 소켓에서 받고(잠금 밖), 패킷 처리와 타임아웃 확인과 보낼 패킷 생성을 하고(잠금 안), 보냅니다(잠금 밖). 한 번에 한 스레드만 부른다. */
        void update( float64 nowSeconds ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief 받을 데이터그램이 생기거나 @p timeoutSeconds 가 지날 때까지 잠듭니다. `update` 를 도는 스레드가 부릅니다. */
        bool waitForReceive( float64 timeoutSeconds ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief 이 스레드를 `update` 스레드 역할의 소유자로 정합니다(`NetHostThread` 가 스레드를 시작할 때). Debug 가 `update` 진입에서 스레드를 확인한다. */
        void bindUpdateThread() { _updateThread.bindToCurrentThread(); }
        /** @brief `update` 스레드 역할을 놓습니다(그 스레드가 끝날 때) — 다음에 `update` 를 부르는 스레드가 받는다. */
        void releaseUpdateThread() { _updateThread.unbind(); }

        [[nodiscard]] bool sendMessage( int32 connectionID, NetChannelType channel, const uint8* pData, int32 size ) SW_EXCLUDES_LOCK( _mutex ) override;
        [[nodiscard]] bool sendNotifiedMessage( int32 connectionID, const uint8* pData, int32 size, uint32 tag ) SW_EXCLUDES_LOCK( _mutex ) override;
        [[nodiscard]] bool sendMessage( int32 connectionID, NetChannelType channel, const vector<uint8>& buffer )
        {
            return sendMessage( connectionID, channel, buffer.data(), static_cast<int32>( buffer.size() ) );
        }
        /** @brief 연결된 모두에게 보냅니다. @p exceptID 는 제외합니다. 반환값은 보낸 수입니다. */
        int32 broadcast( NetChannelType channel, const uint8* pData, int32 size, int32 exceptID = -1 ) SW_EXCLUDES_LOCK( _mutex ) override;
        /**
         * @brief 지금까지 보내기 큐에 넣은 메시지의 종류별 셈을 @p outTable 에 더합니다(`sendMessage` · `broadcast` 가 연결마다 센다 — 받은 쪽 셈은 `NetMessageRouter::getKindTally`).
         * @details 큐에 넣은 것을 센다. 대역폭에 막혀 늦게 나가거나 다시 보낸 바이트는 연결 통계(`NetConnectionStats`)의 몫이다. 잠금을 잡고 표 하나(4 KiB)를 더한다 — `net.stats` 처럼 느리게 묻는 쪽이 쓴다.
         */
        void addSentKindTally( NetMessageKindTallyTable& outTable ) const SW_EXCLUDES_LOCK( _mutex ) override;
        /** @brief 받은 메시지 하나를 꺼냅니다. 연결 순서대로, 채널은 신뢰 순서, 신뢰 순서 없음, 순서만, 비신뢰 순(채널 값 순)으로 확인합니다. */
        [[nodiscard]] bool receiveMessage( int32& outConnectionID, NetChannelType& outChannel, vector<uint8>& outBuffer ) SW_EXCLUDES_LOCK( _mutex );
        void               disconnect( int32 connectionID ) SW_EXCLUDES_LOCK( _mutex ) override;
        void               disconnectAll() SW_EXCLUDES_LOCK( _mutex );
        void               drainEvents( vector<NetHostEvent>& outListEvent ) SW_EXCLUDES_LOCK( _mutex );
        /**
         * @brief 쌓인 연결 사건과 받은 메시지를 **잠금 한 번에** 모두 꺼냅니다. @p outInbound 를 비운 뒤 채우며, 사건이 메시지보다 앞에 옵니다.
         * @details 닫힌 연결의 받은 메시지는 연결을 닫을 때 지워지므로, 꺼낸 메시지는 모두 지금 그 슬롯에 있는 연결의 것이다.
         *          사건을 먼저 처리하면 같은 슬롯에 새로 연결된 클라이언트의 메시지를 옛 연결의 상태로 읽지 않는다.
         *          `receiveMessage` 나 `drainEvents` 와 섞어 쓰지 않는다(`NetMessageRouter::pump` 가 이 함수를 쓴다).
         */
        void drainInbound( NetInbound& outInbound ) SW_EXCLUDES_LOCK( _mutex ) override;

        bool               isServer() const SW_EXCLUDES_LOCK( _mutex ) override;
        NetConnectionState getConnectionState( int32 connectionID ) const SW_EXCLUDES_LOCK( _mutex ) override;
        /** @brief 연결 통계의 사본입니다. 연결돼 있지 않으면 false 를 반환합니다. 다른 스레드가 `update` 하는 중에도 안전합니다. */
        [[nodiscard]] bool getConnectionStats( int32 connectionID, NetConnectionStats& outStats ) const SW_EXCLUDES_LOCK( _mutex ) override;
        /**
         * @brief 연결 객체를 그대로 빌려줍니다.
         * @warning 잠금 밖으로 나가는 포인터이므로 `update` 를 도는 다른 스레드가 없을 때만(한 스레드에서 쓰거나 테스트에서) 쓴다. 스레드가 돌고 있으면 `getConnectionStats` 를 쓴다.
         */
        const NetConnection* findConnection( int32 connectionID ) const SW_EXCLUDES_LOCK( _mutex );
        NetAddress           getConnectionAddress( int32 connectionID ) const SW_EXCLUDES_LOCK( _mutex ) override;
        int32                getConnectedCount() const SW_EXCLUDES_LOCK( _mutex ) override;
        /** @brief 클라이언트에서 서버가 준 자기 번호(서버 쪽 연결 ID)를 반환합니다. 연결 전이면 -1 입니다. */
        int32 getClientIndex() const SW_EXCLUDES_LOCK( _mutex ) override;
        void  collectConnected( vector<int32>& outListConnection ) const SW_EXCLUDES_LOCK( _mutex ) override;
        /** @brief 이 호스트가 지금 디버거 대기를 알리는지 반환합니다. 마지막 `update` 기준이며(`NetHostConfig::_debugHold`), Shipping 은 항상 false 입니다. */
        bool   isDebugHoldAnnounced() const SW_EXCLUDES_LOCK( _mutex );
        uint64 getRejectedPacketCount() const SW_EXCLUDES_LOCK( _mutex );
        /** @brief 서버에서 인증기가 이 연결에 지정한 주체(로그인 계정)를 반환합니다. 없으면 0 입니다. */
        uint64 getConnectionPrincipal( int32 connectionID ) const SW_EXCLUDES_LOCK( _mutex );
        /** @brief 복호, 증명, 키 확인에 실패해 버린 패킷 수입니다(진단과 테스트용). */
        uint64 getAuthenticationFailureCount() const SW_EXCLUDES_LOCK( _mutex );
        /** @brief 재전송 방지 윈도우가 버린 패킷 수입니다. */
        uint64 getReplayRejectedCount() const SW_EXCLUDES_LOCK( _mutex );
        /** @brief 이 호스트의 프로토콜 ID(`NetProtocol::makeProtocolID( 게임 ID, 와이어 버전 )`)입니다. */
        uint32 getProtocolID() const SW_EXCLUDES_LOCK( _mutex );
        /** @brief 연결마다의 보내기 상한(초당 바이트, `NetHostConfig::_maxBytesPerSecond`)입니다. 키트가 틱 예산을 계산할 때 씁니다(`NetSendBudget::computeTickBudget`). */
        int32 getMaxBytesPerSecond() const SW_EXCLUDES_LOCK( _mutex ) override;
        /**
         * @brief 이 연결이 포화됐는가입니다. 대역폭 토큰을 다 썼거나 앞 패킷이 비신뢰 메시지를 다 싣지 못했으면 true 입니다. 연결돼 있지 않으면 false 입니다.
         * @details 키트는 포화된 연결에 이번 틱의 비신뢰 복제(위치 · 효과)를 건너뛴다. 쌓아 두면 늦은 것이 몰려 나간다. 언리얼 `UNetConnection::IsNetReady` 가 거짓인 자리이다.
         */
        bool isConnectionSaturated( int32 connectionID ) const SW_EXCLUDES_LOCK( _mutex ) override;

        /** @brief 패킷 본문의 첫 3비트로, 핸드셰이크 단계와 데이터와 끊김을 구분합니다. */
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

        /** @brief 데이터그램(헤더 포함)의 패킷 종류를 미리 확인합니다. 체크섬은 보지 않으며, 너무 짧으면 Count 를 반환합니다(위조 패킷 필터와 진단용). */
        static PacketType peekPacketType( const uint8* pData, int32 size );
        /** @brief 패킷 체크섬입니다(헤더 값을 먼저 섞은 FNV-1a). 테스트와 진단 도구가 고친 패킷의 체크섬을 다시 맞출 때 씁니다. */
        static uint32 computePacketChecksum( uint32 headerID, const uint8* pBody, int32 size );

    private:
        /** @brief 버전과 상관없는 고정 헤더(`NetProtocol::kHandshakeID`)로 싸는 패킷입니다. 요청과 거절뿐이며, 두 패킷의 배치는 버전이 바뀌어도 그대로 둡니다. */
        static bool isHandshakeFramed( PacketType type ) { return type == PacketType::ConnectRequest || type == PacketType::Denied; }

        /** @brief 보낼 데이터그램 하나입니다. 바이트는 `OutgoingBatch::_bytes` 의 [_offset, _offset + _size) 범위입니다. */
        struct OutgoingDatagram
        {
            NetAddress _to{};
            int32      _offset{ 0 };
            int32      _size{ 0 };
        };

        /** @brief 잠금 안에서 쌓고 잠금 밖에서 보내는 배치입니다. 둘을 swap 해서 할당 없이 넘깁니다. */
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

        /** @brief 끝난 비동기 연결입니다. 잠금 밖에서 채웁니다. */
        struct FinishedConnect
        {
            TaskPromise<NetConnectResult> _promise{};
            NetConnectResult              _result{};
        };

        /** @brief 잠금 안에서 모은 경고의 종류 — 로그 id 는 소스에 글자로 있어야 하므로(`SW_LOG_WARNING_ID`) 종류마다 `update` 가 id 를 붙여 쓴다. */
        enum class DeferredWarningKind : uint8
        {
            ConnectionRefused, ///< 서버가 맞지 않는 프로토콜의 요청을 거절했다
            ServerRefused,     ///< 클라이언트가 서버의 거절을 받았다
        };

        struct DeferredWarning
        {
            DeferredWarningKind _kind{ DeferredWarningKind::ConnectionRefused };
            string              _text{};
        };

        /** @brief 데이터 패킷이 아닌 제어 패킷 하나에서 읽은 값입니다(값 둘의 뜻은 종류마다 다르다). */
        struct ControlPacket
        {
            NetAddress _from{};
            float64    _nowSeconds{ 0.0 };
            int32      _slotIndex{ -1 }; ///< 보낸 주소의 슬롯(없으면 무효)
            uint64     _valueA{ 0 };
            uint64     _valueB{ 0 };
        };

        // 아래 함수는 모두 잠금을 잡은 채로 부른다.
        /** @brief 받은 데이터그램 하나의 헤더와 종류를 검사하고 종류별 처리로 넘깁니다. 틀린 패킷은 `_rejectedPacketCount` 로만 셉니다. */
        void handlePacket( float64 nowSeconds, const NetAddress& from, const uint8* pData, int32 size ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 암호화하지 않은 데이터 패킷의 연결 값을 확인하고 본문을 연결에 넘깁니다. */
        void handlePayloadPacket( float64 nowSeconds, int32 slotIndex, BitReader& reader ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 제어 패킷을 종류별로 처리합니다(핸드셰이크 · 거절 · 끊기). */
        void handleControlPacket( PacketType type, const ControlPacket& packet, BitReader& reader ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 서버가 연결 요청에 답합니다. 프로토콜이 다르면 거절, 빈 슬롯이 없으면 가득 참, 아니면 챌린지를 보냅니다. */
        void handleConnectRequest( const ControlPacket& packet ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 서버가 챌린지 응답을 확인하고 슬롯을 할당해 수락합니다. 이미 연결된 주소의 같은 응답이면 수락만 다시 보냅니다. */
        void handleChallengeResponse( const ControlPacket& packet, BitReader& reader ) SW_REQUIRES_LOCK( _mutex );
        void updateSlots( float64 nowSeconds ) SW_REQUIRES_LOCK( _mutex );
        void sendControl( const NetAddress& to, PacketType type, uint64 valueA, uint64 valueB ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 연결 요청을 거절합니다. 이유, 이 호스트의 프로토콜 ID, 요청에 든 클라이언트 솔트(위조 거절을 걸러 내는 값)를 싣습니다. */
        void sendDenied( const NetAddress& to, NetDisconnectReason reason, uint64 clientSalt ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 클라이언트가 챌린지 응답을 보냅니다. 암호화 방식이면 공개 키를, 토큰 결속이면 토큰과 증명 태그를 함께 싣습니다. */
        void sendChallengeResponse( NetHostSlot& slot ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 서버가 수락을 보냅니다. 암호화 방식이면 공개 키와 키 확인 태그를 함께 싣습니다. */
        void sendAccepted( int32 slotIndex ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 서버가 응답의 공개 키, 토큰, 증명을 읽고 검증해 키를 만듭니다. 틀리면 거절을 보내고 false 를 반환합니다. */
        [[nodiscard]] bool acceptSecureResponse( BitReader& reader, const NetAddress& from, uint64 clientSalt, uint64 challenge, unique_ptr<NetSlotSecurity>& outSecurity ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 클라이언트가 수락의 공개 키와 키 확인 태그를 읽고 키를 만듭니다. 틀리면 연결을 닫고 false 를 반환합니다. */
        [[nodiscard]] bool acceptSecureAccepted( BitReader& reader, NetHostSlot& slot, int32 slotIndex, uint32 serverIndex ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 암호화된 데이터 패킷과 끊기 패킷을 만듭니다(@p pPlain 을 봉인). 보낸 데이터그램의 바이트 수를 반환하며, 실패하면 0 입니다. */
        int32 sendSealed( NetHostSlot& slot, PacketType type, const uint8* pPlain, int32 plainSize ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 암호화 방식의 데이터 패킷과 끊기 패킷(헤더 8바이트 뒤의 본문)을 처리합니다. */
        void handleSealedPacket( float64 nowSeconds, PacketType type, int32 slotIndex, const uint8* pBody, int32 bodySize ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 이 호스트의 기능 마스크(`NetProtocolFeature`)입니다. 역할과 자격이 정해진 뒤(listen, connect) 프로토콜 ID에 섞습니다. */
        uint32 computeFeatureMask() const SW_REQUIRES_LOCK( _mutex );
        bool   isEncrypted() const SW_REQUIRES_LOCK( _mutex ) { return _settings._security._mode == NetSecurityMode::Encrypted; }
        /** @brief `_packetWriter` 의 본문에 헤더(헤더 값과 체크섬)를 붙여 보낼 배치에 넣습니다. */
        void sendFramed( const NetAddress& to, uint32 headerID ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 패킷 하나를 씁니다. 대역폭 토큰이 남아 있으면 메시지까지 싣고, 다 썼으면 헤더(확인)만 싣습니다. 보낸 바이트만큼 토큰에서 뺍니다. */
        void sendPayload( float64 nowSeconds, NetHostSlot& slot ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 지난 충전 이후 흐른 시간만큼 대역폭 토큰을 채웁니다. 상한은 보내기 간격 두 번어치이고, 최소 패킷 하나 크기입니다. */
        void refillSendCredit( float64 nowSeconds, NetHostSlot& slot ) const SW_REQUIRES_LOCK( _mutex );
        /** @brief 디버거 대기를 알릴지 다시 정합니다(디버거는 1초마다 확인). 알리는 중에 `update` 사이가 벌어졌으면 그 시간만큼 연결 시계를 앞으로 밀습니다(가상 시계). */
        void updateDebugHold( float64 nowSeconds ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 이 연결의 타임아웃을 반환합니다. 상대가 디버거 대기를 알렸다면(개발 빌드) `_debugHoldTimeoutSeconds` 와 기본값 중 큰 쪽입니다. */
        float64                      computeTimeoutSeconds( const NetHostSlot& slot ) const SW_REQUIRES_LOCK( _mutex );
        void                         closeSlot( int32 slotIndex, NetDisconnectReason reason, bool bNotifyRemote ) SW_REQUIRES_LOCK( _mutex );
        bool                         startConnect( const NetAddress& serverAddress ) SW_REQUIRES_LOCK( _mutex );
        bool                         connectLocked( const NetAddress& serverAddress, const NetConnectCredentials* pCredentials ) SW_EXCLUDES_LOCK( _mutex );
        TaskFuture<NetConnectResult> connectAsyncWith( const NetAddress& serverAddress, const NetConnectCredentials* pCredentials ) SW_EXCLUDES_LOCK( _mutex );
        void                         pushEvent( const NetHostEvent& event ) SW_REQUIRES_LOCK( _mutex );
        void                         finishConnect( NetDisconnectReason reason ) SW_REQUIRES_LOCK( _mutex );
        bool                         sendMessageLocked( int32 connectionID, NetChannelType channel, const uint8* pData, int32 size ) SW_REQUIRES_LOCK( _mutex );
        void                         takePending( OutgoingBatch& outBatch, vector<FinishedConnect>& outListFinished ) SW_REQUIRES_LOCK( _mutex );

        // 잠금 밖에서 부른다.
        static void sendBatch( INetTransport* pTransport, const OutgoingBatch& batch );
        static void deliverFinished( vector<FinishedConnect>& listFinished );

        mutable mutex                                 _mutex;                  ///< 아래 상태 전부. 잠금 밖 전용이라고 적은 것은 제외한다.
        NetSlotTable _slotTable                       SW_GUARDED_BY( _mutex ); ///< 연결 슬롯과 주소 맵.
        NetChallenge _challenge                       SW_GUARDED_BY( _mutex ); ///< 챌린지 값의 비밀 키와 솔트 수열.
        NetPacketSealer _sealer                       SW_GUARDED_BY( _mutex ); ///< 암호화 방식의 봉인과 열기(스크래치 버퍼를 다시 쓴다).
        vector<NetHostEvent> _listEvent               SW_GUARDED_BY( _mutex );
        OutgoingBatch _pendingBatch                   SW_GUARDED_BY( _mutex );        ///< 잠금 안에서 쌓인 보낼 데이터그램
        BitWriter _packetWriter                       SW_GUARDED_BY( _mutex );        ///< 보낼 패킷의 몸
        TaskPromise<NetConnectResult> _connectPromise SW_GUARDED_BY( _mutex );        ///< 기다리는 비동기 연결(`_bConnectPending`).
        vector<FinishedConnect> _listFinished         SW_GUARDED_BY( _mutex );        ///< 끝난 비동기 연결. 잠금을 푼 뒤 채운다.
        ThreadRole                                    _updateThread;                  ///< `update` 를 도는 스레드 역할 — 아래 셋은 그 스레드만 잠금 밖에서 만진다(잠금이 아니라 분석용 능력)
        vector<NetReceivedDatagram> _listReceived     SW_GUARDED_BY( _updateThread ); ///< `update` 스레드 전용. 잠금 밖에서 받은 배치(버퍼를 다시 쓴다).
        OutgoingBatch _flushBatch                     SW_GUARDED_BY( _updateThread ); ///< `update` 스레드 전용. 잠금 밖에서 보내는 배치.
        vector<FinishedConnect> _listDeliver          SW_GUARDED_BY( _updateThread ); ///< `update` 스레드 전용.
        BitWriter _plainWriter                        SW_GUARDED_BY( _mutex );        ///< 암호화에서 NetConnection 이 쓴 평문 패킷.
        NetConnectCredentials _credentials            SW_GUARDED_BY( _mutex );        ///< 클라이언트 전용. 지금 연결에 제시하는 자격.
        vector<DeferredWarning> _listDeferredWarning  SW_GUARDED_BY( _mutex );        ///< 잠금 안(`handlePacket`)에서 모은 경고. `update` 가 잠금을 놓은 뒤 쓴다.
        NetHostConfig _settings                       SW_GUARDED_BY( _mutex );
        NetMessageKindTallyTable _sentKindTally       SW_GUARDED_BY( _mutex ); ///< `sendMessageLocked` 가 큐에 넣은 메시지마다 더한다.
        INetTransport* _pTransport                    SW_GUARDED_BY( _mutex );
        uint64 _rejectedPacketCount                   SW_GUARDED_BY( _mutex );
        uint64 _authenticationFailureCount            SW_GUARDED_BY( _mutex );
        uint64 _replayRejectedCount                   SW_GUARDED_BY( _mutex );
        uint64 _mismatchLogCount                      SW_GUARDED_BY( _mutex );
        float64 _lastUpdateTime                       SW_GUARDED_BY( _mutex ); ///< 지난 `update` 의 시각. 음수면 아직 호출한 적이 없다(가상 시계가 멈춘 시간을 측정할 때 쓴다).
        float64 _nextDebuggerPollTime                 SW_GUARDED_BY( _mutex ); ///< 디버거가 붙었는지 다시 확인할 시각.
        uint32 _protocolID                            SW_GUARDED_BY( _mutex );
        atomic<uint32>                                _updateDepth; ///< `update` 를 두 스레드가 동시에 부르는 실수를 잡는다.
        int32 _clientIndex                            SW_GUARDED_BY( _mutex );
        int32 _receiveCursor                          SW_GUARDED_BY( _mutex ); ///< 연결마다 받기를 고르게 돌리기 위한 시작 위치.
        uint8 _bServer                                SW_GUARDED_BY( _mutex );
        uint8 _bConnectPending                        SW_GUARDED_BY( _mutex );
        uint8 _bHasCredentials                        SW_GUARDED_BY( _mutex ); ///< 클라이언트 전용. 자격을 제시하는 연결이다(토큰 결속).
        uint8 _bDebugHold                             SW_GUARDED_BY( _mutex ); ///< 지금 디버거 대기를 알린다(데이터 패킷의 비트).
    };
} // namespace sw
