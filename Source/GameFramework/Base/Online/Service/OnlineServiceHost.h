/**
 * @file OnlineServiceHost.h
 * @brief 프로세스 하나의 서비스 호스트를 정의합니다.
 * @details 호스트는 스트림 엔드포인트와 요청 서버를 갖는다.
 *          스키마 해시 비교, 인증 확인, 요청 보호(주소와 계정마다 토큰 버킷, 몸 상한)를 거친 뒤 메서드 영역을 맡은 서비스(`IOnlineHostService`)로 요청을 넘긴다.
 *          언리얼 Online Services 의 인터페이스 그룹, gRPC 서버의 서비스 등록과 같은 구조이다.
 *
 *          - 서비스 스레드 하나(`tick` 을 부르는 스레드)에서 돈다. 서비스의 콜백은 모두 그 스레드에서 불린다.
 *            계정과 연결의 테이블을 갖고 알림(`sendPush`)을 보낸다.
 *          - **캐시 앞과 서버 버스의 소비자는 호스트 하나이다.**
 *            캐시 답과 채널 메시지는 `getEphemeralRouter()` 가 요청 id 와 채널로, 버스 메시지는 `subscribeServerBus` 한 서비스에 나눠 준다.
 *            서비스가 `pollReplies` 나 `pollMessages` 를 직접 부르면 서로의 답을 가져간다.
 *          - 메서드 영역이 겹치는 서비스는 `registerHostService` 가 거절한다.
 *          - **GM 리슨**(`_pAdminTransport`)을 주면 리슨이 둘이 된다. 호스트 안의 묶음 전송(`ListenerTransport`)이 연결 핸들에 리슨 번호를 섞어 두 전송을 전송 하나로 보이므로
 *            엔드포인트 · 요청 서버 · 계정 표는 하나다(GM 이 GM 포트에서 로그인해도 같은 계정 표). 서비스의 노출(`getExposure`)과 연결이 들어온 리슨이 맞지 않는 요청은
 *            없는 메서드와 같은 `kInvalidRequest` 로 답한다(영역이 있는지 드러내지 않는다). Hello · Ping 은 두 리슨 모두 받는다(OnlineKits I6).
 *          - **Hello 는 다른 어떤 바이트보다 먼저 온라인 스키마 해시(`OnlineProtocolRegistry`)를 비교한다.** 다르면 `kVersionMismatch` 와 서버 해시로 답하고 연결을 닫는다.
 *            해시는 `initialize` 때 한 번 정한다. 이 비교는 B 형식이 아니라 고정된 작은 형식이라 스키마가 바뀌어도 늘 읽힌다.
 *          - **수명 계약**: 서비스는 빌려 쓴다. 호스트 `shutdown` 이 서비스마다 `onHostShutdown` 을 한 번 불러 호스트 포인터와 구독을 떼게 한다.
 *            버스 구독은 `subscribeServerBus` 가 돌려준 구독(`Subscription`)이 소유한다 — 서비스가 먼저 사라지면 구독이 풀리고, 호스트가 먼저 내려가면 무효가 된다.
 *            그래서 서비스 객체는 호스트보다 먼저 종료돼도 늦게 종료돼도 된다.
 *            서비스 쪽에서 맡긴 비동기 일(캐시 라우터, 접속 상태 찾기)은 서비스 `shutdown` 이 `cancel` 한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Log/LogContext.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/Network/Message/StreamMessageEndpoint.h"
#include "Core/Network/NetTypes.h"
#include "Core/Serialization/BitStream.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Guard/TokenBucketMap.h"
#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/Base/Online/Service/OnlineProtocolRegistry.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class IEphemeralStore;
    class IServiceStore;
    class IStreamTransport;
    class OnlineServiceHost;
    class RemoteConfig;

    /** @brief 서비스가 요청 하나를 받을 때의 문맥입니다. 콜백 동안만 유효하므로, 나중에 답할 요청은 `_token` 을 복사해 두어야 합니다. */
    struct OnlineCallContext
    {
        NetRequestToken        _token{};
        NetIdempotencyKey      _idempotencyKey{};
        StreamConnectionHandle _connection{};
        AccountID              _accountID{ kInvalidAccountID }; ///< 로그인한 연결이면(`bindAccount` 로 붙인 주체)
        uint64                 _remoteKey{ 0 };                 ///< 원격 주소 해시(로그인 전 도배 제한 단위)
        LogTraceID             _traceID{};                      ///< 요청 추적 id(요청 머리 — 없으면 요청 서버가 만든다)
        int64                  _nowMs{ 0 };                     ///< 서버 벽시계(마지막 `tick` 의 시각)
        uint16                 _method{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 서비스가 어느 리슨의 연결에서 요청을 받는지입니다(노출 면).
     * @details `Admin` 서비스는 GM 리슨(`OnlineServiceHostConfig::_pAdminTransport`)의 연결만 받는다. GM 리슨 없이 `Admin` 서비스를 올리면 `initialize` · `registerHostService` 가
     *          거절한다 — 플레이어 포트가 GM 서비스를 받는 숨은 폴백은 두지 않는다(GM 도구를 공용 포트에 여는 설정 실수를 기동에서 잡는다).
     */
    enum class OnlineServiceExposure : uint8
    {
        Player,         ///< 플레이어 리슨만
        Admin,          ///< GM 리슨만(GM 도구)
        PlayerAndAdmin, ///< 두 리슨 모두(계정 — GM 도 로그인한다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class IOnlineHostService
     * @brief 메서드 영역 하나를 맡은 서비스입니다. 모든 콜백은 호스트 `tick` 스레드에서 불립니다.
     */
    class SW_GF_API IOnlineHostService
    {
    public:
        IOnlineHostService()          = default;
        virtual ~IOnlineHostService() = default;

        IOnlineHostService( const IOnlineHostService& )            = delete;
        IOnlineHostService& operator=( const IOnlineHostService& ) = delete;

        /** @brief 이 서비스의 키트 프로토콜 항목입니다. 영역은 여기서 나오고, 항목은 온라인 스키마 해시에 섞입니다. */
        virtual const OnlineProtocolEntry& getProtocol() const = 0;
        uint16                             getMethodRange() const { return getProtocol().getMethodRange(); }
        /** @brief 이 서비스를 받는 리슨입니다. 기본값은 플레이어 리슨이며, GM 도구가 부르는 서비스는 `Admin` 으로 답해 플레이어 포트에서 닫습니다. */
        virtual OnlineServiceExposure getExposure() const { return OnlineServiceExposure::Player; }
        /** @brief 로그인 없이 부를 수 있는 메서드면 true 입니다. 기본값은 false 입니다. */
        virtual bool isAnonymousMethod( uint16 method ) const
        {
            (void)method;
            return false;
        }
        /** @brief 요청 하나를 받습니다. 바로 `host.respond*` 로 답하거나, 토큰을 보관했다가 저장 일이 끝난 뒤 답합니다. 답은 한 번만 할 수 있습니다. */
        virtual void onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) = 0;
        /** @brief 서비스 스레드의 틱입니다. 완료를 거두고 시한을 처리합니다. */
        virtual void onServiceTick( OnlineServiceHost& host, int64 nowMs )
        {
            (void)host;
            (void)nowMs;
        }
        /** @brief 계정이 이 프로세스의 연결에 처음 붙었을 때(`bindAccount`) 불립니다. 길드 붙이기처럼 접속한 계정마다 할 일을 맡깁니다. 같은 계정을 다시 붙이면 불리지 않습니다. */
        virtual void onAccountJoined( OnlineServiceHost& host, AccountID accountID )
        {
            (void)host;
            (void)accountID;
        }
        /** @brief 계정이 이 프로세스에서 떠났을 때(로그아웃, 끊김, 밀려남) 불립니다. 거래를 취소하고 채널을 떠나는 처리를 합니다. */
        virtual void onAccountLeft( OnlineServiceHost& host, AccountID accountID )
        {
            (void)host;
            (void)accountID;
        }
        /** @brief 이 서비스가 `subscribeServerBus` 한 주제의 메시지를 받습니다. 자기 서버가 낸 것도 오므로 `getOriginServerID` 로 거릅니다. 본문은 주제 정의(`ServerBusTopic::read`)로 읽습니다. */
        virtual void onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message )
        {
            (void)host;
            (void)message;
        }
        /**
         * @brief 호스트가 종료할 때 한 번 불립니다.
         * @details 기다리던 캐시 답(Unavailable)을 모두 알린 뒤, 버스 구독과 전송을 닫기 전에 불린다. 보관하던 호스트 포인터와 구독 목록을 버려야 한다.
         *          이 뒤로 서비스는 호스트를 부르지 않는다. 서비스 객체가 호스트보다 오래 살아도(늦게 `shutdown`) 사라진 호스트를 만지지 않는다.
         */
        virtual void onHostShutdown( OnlineServiceHost& host ) { (void)host; }
    };
} // namespace sw

namespace sw
{
    /** @brief 호스트 설정입니다. 저장소, 캐시, 버스, 원격 설정은 빌려 쓰므로 호스트보다 오래 살아야 합니다. */
    struct OnlineServiceHostConfig
    {
        StreamTransportConfig _transportSettings{}; ///< `_ioThreadCount == 0` 이면 호스트 `tick` 이 전송을 돈다(루프백 테스트)
        StreamEndpointConfig  _endpointSettings{};
        NetAddress            _listenAddress{ NetAddress::makeAnyInterface( 7100 ) }; ///< 포트 0 = 아무 포트(`getListenPort`)
        NetAddress            _adminListenAddress{ NetAddress::makeLoopback( 0 ) };   ///< `_pAdminTransport` 가 받을 주소. 포트 0 = 아무 포트(`getAdminListenPort`)
        IStreamTransport*     _pAdminTransport{ nullptr };                            ///< 빌려 쓴다. 있으면 GM 리슨을 따로 열고 서비스의 노출(`getExposure`)대로 리슨을 가른다
        IServiceStore*        _pServiceStore{ nullptr };
        IEphemeralStore*      _pEphemeralStore{ nullptr };     ///< 없으면 서버 한 대(라우터 없음)
        IServerBusTransport*  _pServerBusTransport{ nullptr }; ///< 없으면 서버 한 대. 키트에는 호스트가 만든 앞(`getServerBus`)만 간다
        RemoteConfig*         _pRemoteConfig{ nullptr };
        int32                 _requestBurstPerAccount{ 30 };
        int64                 _requestRefillMsPerAccount{ 100 };
        int32                 _requestBurstPerRemote{ 10 }; ///< 로그인 전(주소마다)
        int64                 _requestRefillMsPerRemote{ 500 };
        int64                 _helloTimeoutMs{ 5000 };        ///< 연결이 Hello 를 끝내야 하는 기한. 넘으면 닫는다(열기만 하고 슬롯을 잡는 연결을 막는다)
        int32                 _maxConnectionsPerRemote{ 64 }; ///< 주소(IP)마다 동시 연결 상한. 넘는 연결은 바로 닫는다. 한 주소에서 많이 붙이는 부하 봇은 올린다
    };
} // namespace sw

namespace sw
{
    /**
     * @class OnlineServiceHost
     * @brief 서비스 호스트입니다.
     */
    class SW_GF_API OnlineServiceHost final : public IStreamEndpointListener, public INetRequestHandler, private SubscriptionSource
    {
    public:
        OnlineServiceHost();
        ~OnlineServiceHost() override;

        OnlineServiceHost( const OnlineServiceHost& )            = delete;
        OnlineServiceHost& operator=( const OnlineServiceHost& ) = delete;

        /** @brief 전송(빌려 쓴다)을 띄우고 받기 시작합니다. `_pAdminTransport` 가 있으면 GM 리슨도 엽니다. 서비스는 그 전에 등록해야 합니다. */
        [[nodiscard]] bool initialize( IStreamTransport* pTransport, const OnlineServiceHostConfig& settings, string& outError );
        void               shutdown();

        /** @brief 서비스를 등록합니다. 빌려 쓰며, 영역이 이미 있거나 메서드 번호가 겹치면 오류 로그를 남기고 false 를 반환하고 아무것도 등록하지 않습니다. */
        [[nodiscard]] bool registerHostService( IOnlineHostService* pService );
        /** @brief 엔드포인트 pump, 요청 시한, 캐시 라우터, 서버 버스, 서비스 틱을 차례로 돌립니다. */
        void update( int64 nowMs );

        // 답은 서비스가 부르며 한 번만 보낼 수 있다. 이미 답했거나 끝난 요청이면 false 를 반환한다.
        bool respondOk( const NetRequestToken& token, const BitWriter& body );
        /**
         * @brief @p message 를 키트 메시지 목록 @p TMessages 로 써서 성공 응답으로 보냅니다. 범위 밖 값이 있어 쓰지 못하면 `kInternal` 오류로 응답합니다.
         * @details 쓰기가 범위 밖 값을 로그에 남기며, 고칠 곳은 그 값을 만든 서버 코드입니다. 키트 서버가 응답 메시지를 보내는 길은 이것 하나입니다.
         * @return 메시지를 썼으면 true 입니다. 연결이 그사이 닫혀 응답이 버려졌어도 true 입니다.
         */
        template <typename TMessages, typename TMessage>
        bool respondMessage( const NetRequestToken& token, const TMessage& message )
        {
            BitWriter body;
            if ( TMessages::write( body, message ) == false )
            {
                (void)respondError( token, OnlineError::kInternal );
                return false;
            }
            (void)respondOk( token, body );
            return true;
        }
        bool respondError( const NetRequestToken& token, uint16 errorCode, const BitWriter* pDetail = nullptr );

        // 계정과 연결을 잇는다. 계정 키트가 로그인과 재접속 때 부른다.
        /** @brief 연결에 계정을 붙입니다. 그 계정이 다른 연결에 붙어 있으면 false 이며, 계정 키트가 먼저 `unbindAccount` 를 불러야 합니다. */
        [[nodiscard]] bool bindAccount( StreamConnectionHandle connection, AccountID accountID );
        /** @brief 계정의 연결을 닫고 서비스들에 `onAccountLeft` 를 알립니다. */
        void unbindAccount( AccountID accountID );
        bool findConnection( AccountID accountID, StreamConnectionHandle& outConnection ) const;
        /** @brief 알림을 보냅니다. Message 프레임은 `[종류 u16][몸]` 이며, 종류는 영역에 0x80..0xFF 를 더한 값입니다. 그 계정이 이 프로세스에 없으면 false 입니다. */
        bool sendPush( AccountID accountID, uint16 kind, const BitWriter& body );
        /** @brief 이 프로세스에 붙은(로그인한) 모든 계정에 알림을 보내고 보낸 수를 반환합니다. 다른 서버는 각자 보내며, 서비스가 버스로 알립니다. */
        int32 sendPushToAll( uint16 kind, const BitWriter& body );

        /** @brief 캐시 답과 채널 메시지를 나눠 주는 라우터입니다. 캐시가 없으면(서버 한 대) nullptr 이며, 호스트 `tick` 이 비웁니다. */
        EphemeralStoreRouter* getEphemeralRouter();
        /**
         * @brief @p pService 가 @p topic 의 버스 메시지를 받게 하고, 그 구독을 돌려줍니다. 구독이 사라지면 끊고, 그 주제를 듣는 서비스가 없으면 버스 구독도 닫습니다.
         * @details 같은 주제를 둘이 구독해도 버스에는 한 번만 구독한다. 버스가 없거나(서버 한 대 · `initialize` 전) 같은 서비스가 같은 주제를 이미 구독했으면
         *          오류를 남기고 빈 구독을 돌려준다 — 서버 한 대인지는 부르는 쪽이 `getServerBus` 로 먼저 가른다. 호스트가 `shutdown` 하면 남은 구독은 무효가 된다.
         */
        [[nodiscard]] Subscription subscribeServerBus( string_view topic, IOnlineHostService* pService );

        RemoteConfig* getRemoteConfig() const { return _settings._pRemoteConfig; }
        /** @brief 버스의 타입 있는 앞입니다. 버스가 없으면(서버 한 대) nullptr 입니다. `initialize` 뒤부터 있습니다. */
        ServerBus*       getServerBus() const { return _pServerBus.get(); }
        IServiceStore*   getServiceStore() const { return _settings._pServiceStore; }
        IEphemeralStore* getEphemeralStore() const { return _settings._pEphemeralStore; }
        uint16           getListenPort() const;
        /** @brief GM 리슨의 포트입니다. GM 리슨이 없으면 0 입니다. */
        uint16 getAdminListenPort() const;
        int32  getConnectionCount() const { return static_cast<int32>( _mapConnection.size() ); }

        // IStreamEndpointListener 와 INetRequestHandler 구현이다. 호스트 `tick` 안에서 불린다.
        void onEndpointOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override;
        void onEndpointFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize ) override;
        void onEndpointClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override;
        void onNetRequest( NetRequestServer& server, const NetRequestContext& context ) override;

    private:
        class ListenerTransport;

        /** @brief `initialize` 가 도중에 실패했을 때와 `shutdown` 이 함께 쓰는 정리입니다 — 전송 · 요청 서버 · 엔드포인트를 해제하고 연결 테이블을 비웁니다. 아직 시작하지 않은 것을 해제해도 안전합니다. */
        void releaseTransportSide();

        struct ConnectionState
        {
            uint64    _remoteKey{ 0 };
            AccountID _accountID{ kInvalidAccountID };
            int64     _openedMs{ 0 }; ///< 연결이 열린 시각(호스트 시계). Hello 기한(`_helloTimeoutMs`)의 기준
            uint8     _bHelloDone{ SW_FALSE };
            uint8     _bAdminListener{ SW_FALSE }; ///< GM 리슨으로 들어온 연결
            uint8     _bClosing{ SW_FALSE };       ///< 주소별 상한이나 Hello 기한으로 닫는 중 — 요청을 받지 않는다
        };

        struct BusSubscription
        {
            string              _topic{};
            IOnlineHostService* _pService{ nullptr };
            uint64              _subscriptionID{ 0 }; ///< 이 항목을 소유하는 구독(`Subscription`)이 찾는 번호
        };

        /** @brief 구독 @p id 가 풀렸습니다. 그 항목을 지우고, 주제를 듣는 서비스가 더 없으면 버스 구독을 닫습니다. */
        void releaseSubscription( uint64 id ) override;

        void handleHello( const NetRequestContext& context, ConnectionState& connection );
        /**
         * @brief `[kVersionMismatch u16][서버 스키마 해시 u64]` 로 답합니다. 둘 다 리틀 엔디언 고정 형식입니다.
         * @details `respondError` 는 자세한 몸을 `BitWriter` 로 받아 비트 배치를 그 형식에 맡깁니다. 이 답은 어느 빌드도 바이트로 읽어야 하므로 `LittleEndianUtil` 로 직접 씁니다.
         */
        void                respondVersionMismatch( const NetRequestToken& token );
        IOnlineHostService* findService( uint16 method ) const;
        bool                sendPushToConnection( StreamConnectionHandle connection, uint16 kind, const BitWriter& body );
        void                dispatchServerBusMessage( const ServerBusMessage& message );
        void                notifyAccountLeft( AccountID accountID );
        /** @brief Hello 를 `_helloTimeoutMs` 안에 끝내지 않은 연결을 닫습니다. `update` 가 부르며 `kHelloSweepIntervalMs` 마다 한 번 표를 돈다. */
        void closeExpiredHelloConnections();

        OnlineServiceHostConfig                          _settings;
        StreamMessageEndpoint                            _endpoint;
        NetRequestServer                                 _requestServer;
        EphemeralStoreRouter                             _ephemeralRouter;
        TokenBucketMap                                   _accountBucket;
        TokenBucketMap                                   _remoteBucket;
        vector<IOnlineHostService*>                      _listService;
        vector<BusSubscription>                          _listBusSubscription;
        vector<ServerBusMessage>                         _listBusScratch;
        vector<uint8>                                    _responseBytes;
        unordered_map<uint64, ConnectionState>           _mapConnection; ///< 연결(packed) → 상태
        unordered_map<AccountID, StreamConnectionHandle> _mapAccountToConnection;
        unordered_map<uint64, int32>                     _mapRemoteConnectionCount; ///< 주소 키(`_remoteKey`) → 열린 연결 수. `_maxConnectionsPerRemote` 를 넘는 연결은 닫는다
        vector<StreamConnectionHandle>                   _listHandleScratch;        ///< Hello 기한을 넘겨 닫을 연결(표를 돌며 닫지 않으려고 모은다)
        unique_ptr<ServerBus>                            _pServerBus;               ///< `_settings._pServerBusTransport` 의 앞이다
        unique_ptr<ListenerTransport>                    _listenerTransport;        ///< 빌린 전송(플레이어 · GM)을 묶은 것. 엔드포인트는 이것 하나를 본다
        int64                                            _nowMs;
        uint64                                           _nextBusSubscriptionID; ///< 다음 버스 구독 번호(0 은 쓰지 않는다)
        int64                                            _nextHelloSweepMs;      ///< 다음 Hello 기한 검사 시각
        uint64                                           _schemaHash;            ///< `initialize` 때 `OnlineProtocolRegistry::computeSchemaHash` 로 정한다
        uint8                                            _bInitialized;
    };
} // namespace sw
