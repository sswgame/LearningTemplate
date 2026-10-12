/**
 * @file OnlineServiceHost.h
 * @brief 프로세스 하나의 서비스 호스트를 정의합니다.
 * @details 호스트는 스트림 엔드포인트와 요청 서버를 갖는다.
 *          버전 협상, 인증 확인, 요청 보호(주소와 계정마다 토큰 버킷, 몸 상한)를 거친 뒤 메서드 영역을 맡은 서비스(`IOnlineHostService`)로 요청을 넘긴다.
 *          언리얼 Online Services 의 인터페이스 묶음, gRPC 서버의 서비스 등록과 같은 모양이다.
 *
 *          - 서비스 스레드 하나(`tick` 을 부르는 스레드)에서 돈다. 서비스의 콜백은 모두 그 스레드에서 불린다.
 *            계정과 연결의 표를 갖고 알림(`sendPush`)을 보낸다.
 *          - **캐시 앞과 서버 버스의 소비자는 호스트 하나이다.**
 *            캐시 답과 채널 메시지는 `getEphemeralRouter()` 가 요청 id 와 채널로, 버스 메시지는 `subscribeServerBus` 한 서비스에 나눠 준다.
 *            서비스가 `pollReplies` 나 `pollMessages` 를 직접 부르면 서로의 답을 가져간다.
 *          - 메서드 영역이 겹치는 서비스는 `registerHostService` 가 거절한다.
 *          - **수명 계약**: 서비스는 빌려 쓴다. 호스트 `shutdown` 이 서비스마다 `onHostShutdown` 을 한 번 불러 호스트 포인터와 구독을 떼게 한다.
 *            그래서 서비스 객체는 호스트보다 먼저 내려가도 늦게 내려가도 된다.
 *            서비스 쪽에서 맡긴 비동기 일(캐시 라우터, 접속 상태 찾기)은 서비스 `shutdown` 이 `cancel` 한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Log/LogContext.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/Network/Message/StreamMessageEndpoint.h"
#include "Core/Network/NetTypes.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Guard/TokenBucketMap.h"
#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
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

        virtual uint16 getMethodRange() const     = 0;
        virtual uint32 getProtocolVersion() const = 0;
        /** @brief 로그인 없이 부를 수 있는 메서드면 true 입니다. 기본값은 false 입니다. */
        virtual bool isAnonymousMethod( uint16 method ) const
        {
            (void)method;
            return false;
        }
        /** @brief 요청 하나를 받습니다. 바로 `host.respond*` 로 답하거나, 토큰을 들고 있다가 저장 일이 끝난 뒤 답합니다. 답은 한 번만 할 수 있습니다. */
        virtual void onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) = 0;
        /** @brief 서비스 스레드의 틱입니다. 완료를 거두고 시한을 처리합니다. */
        virtual void onServiceTick( OnlineServiceHost& host, int64 nowMs )
        {
            (void)host;
            (void)nowMs;
        }
        /** @brief 계정이 이 프로세스에서 떠났을 때(로그아웃, 끊김, 밀려남) 불립니다. 거래를 취소하고 채널을 떠나는 처리를 합니다. */
        virtual void onAccountLeft( OnlineServiceHost& host, AccountID accountID )
        {
            (void)host;
            (void)accountID;
        }
        /** @brief 이 서비스가 `subscribeServerBus` 한 주제의 메시지를 받습니다. 자기 서버가 낸 것도 오므로 `_originServerID` 로 거릅니다. */
        virtual void onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message )
        {
            (void)host;
            (void)message;
        }
        /**
         * @brief 호스트가 내려갈 때 한 번 불립니다.
         * @details 기다리던 캐시 답(Unavailable)을 모두 알린 뒤, 버스 구독과 전송을 닫기 전에 불린다. 들고 있던 호스트 포인터와 구독 목록을 버려야 한다.
         *          이 뒤로 서비스는 호스트를 부르지 않는다. 서비스 객체가 호스트보다 오래 살아도(늦게 `shutdown`) 사라진 호스트를 만지지 않는다.
         */
        virtual void onHostShutdown( OnlineServiceHost& host ) { (void)host; }
    };
} // namespace sw

namespace sw
{
    /** @brief 호스트 설정입니다. 저장소, 캐시, 버스, 원격 설정은 빌려 쓰므로 호스트보다 오래 살아야 합니다. */
    struct OnlineServiceHostSettings
    {
        StreamTransportSettings _transportSettings{}; ///< `_ioThreadCount == 0` 이면 호스트 `tick` 이 전송을 돈다(루프백 테스트)
        StreamEndpointSettings  _endpointSettings{};
        NetAddress              _listenAddress{ NetAddress::makeAnyInterface( 7100 ) }; ///< 포트 0 = 아무 포트(`getListenPort`)
        IServiceStore*          _pServiceStore{ nullptr };
        IEphemeralStore*        _pEphemeralStore{ nullptr }; ///< 없으면 서버 한 대(라우터 없음)
        IServerBus*             _pServerBus{ nullptr };
        RemoteConfig*           _pRemoteConfig{ nullptr };
        int32                   _requestBurstPerAccount{ 30 };
        int64                   _requestRefillMsPerAccount{ 100 };
        int32                   _requestBurstPerRemote{ 10 }; ///< 로그인 전(주소마다)
        int64                   _requestRefillMsPerRemote{ 500 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class OnlineServiceHost
     * @brief 서비스 호스트입니다.
     */
    class SW_GF_API OnlineServiceHost final : public IStreamEndpointListener, public INetRequestHandler
    {
    public:
        OnlineServiceHost();
        ~OnlineServiceHost() override;

        OnlineServiceHost( const OnlineServiceHost& )            = delete;
        OnlineServiceHost& operator=( const OnlineServiceHost& ) = delete;

        /** @brief 전송(빌려 쓴다)을 띄우고 받기 시작합니다. 서비스는 그 전에 올려야 합니다. */
        [[nodiscard]] bool initialize( IStreamTransport* pTransport, const OnlineServiceHostSettings& settings, string& outError );
        void               shutdown();

        /** @brief 서비스를 올립니다. 빌려 쓰며, 영역이 이미 있거나 메서드 번호가 겹치면 오류 로그를 남기고 false 를 반환하고 아무것도 올리지 않습니다. */
        [[nodiscard]] bool registerHostService( IOnlineHostService* pService );
        /** @brief 엔드포인트 pump, 요청 시한, 캐시 라우터, 서버 버스, 서비스 틱을 차례로 돌립니다. */
        void tick( int64 nowMs );

        // 답은 서비스가 부르며 한 번만 보낼 수 있다. 이미 답했거나 끝난 요청이면 false 를 반환한다.
        bool respondOk( const NetRequestToken& token, const BitWriter& body );
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
        /** @brief @p pService 가 @p topic 의 버스 메시지를 받게 합니다. 같은 주제를 둘이 구독해도 버스에는 한 번만 구독하며, 버스가 없으면 아무것도 하지 않습니다. */
        void subscribeServerBus( string_view topic, IOnlineHostService* pService );
        void unsubscribeServerBus( string_view topic, IOnlineHostService* pService );

        RemoteConfig*    getRemoteConfig() const { return _settings._pRemoteConfig; }
        IServerBus*      getServerBus() const { return _settings._pServerBus; }
        IServiceStore*   getServiceStore() const { return _settings._pServiceStore; }
        IEphemeralStore* getEphemeralStore() const { return _settings._pEphemeralStore; }
        uint16           getListenPort() const;
        int32            getConnectionCount() const { return static_cast<int32>( _mapConnection.size() ); }

        // IStreamEndpointListener 와 INetRequestHandler 구현이다. 호스트 `tick` 안에서 불린다.
        void onEndpointOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override;
        void onEndpointFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize ) override;
        void onEndpointClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override;
        void onNetRequest( NetRequestServer& server, const NetRequestContext& context ) override;

    private:
        struct ConnectionState
        {
            uint64    _remoteKey{ 0 };
            AccountID _accountID{ kInvalidAccountID };
            uint8     _bHelloDone{ SW_FALSE };
        };

        struct BusSubscription
        {
            string              _topic{};
            IOnlineHostService* _pService{ nullptr };
        };

        void                handleHello( const NetRequestContext& context, ConnectionState& connection );
        void                writeServerVersions( BitWriter& outWriter ) const;
        IOnlineHostService* findService( uint16 method ) const;
        bool                sendPushToConnection( StreamConnectionHandle connection, uint16 kind, const BitWriter& body );
        void                dispatchServerBusMessage( const ServerBusMessage& message );
        void                notifyAccountLeft( AccountID accountID );

        OnlineServiceHostSettings                        _settings;
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
        IStreamTransport*                                _pTransport;
        int64                                            _nowMs;
        uint8                                            _bInitialized;
    };
} // namespace sw
