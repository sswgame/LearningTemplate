/**
 * @file OnlineServiceHost.h
 * @brief 서비스 틀 — 프로세스 하나의 서비스 호스트. 스트림 끝점 · 요청 서버를 갖고 판 협상 · 인증 확인 · 요청 보호(주소 · 계정마다 토큰 버킷, 몸 상한)를 한 뒤
 *        메서드 영역을 맡은 서비스(`IOnlineService`)로 넘깁니다.
 * @details - 서비스 스레드 하나(`tick` 을 부르는 스레드)에서 돈다 — 서비스의 콜백은 모두 그 스레드다. 계정 ↔ 연결 표를 갖고 알림(`sendPush`)을 보낸다.
 *          - **캐시 앞 · 서버 버스의 소비자는 호스트 하나다**: 캐시 답 · 채널 메시지는 `getEphemeralRouter()` 가 요청 id · 채널로, 버스 메시지는 `subscribeServerBus`
 *            한 서비스에 나눠 준다. 서비스가 `pollReplies` · `pollMessages` 를 직접 부르면 서로의 답을 가져간다.
 *          - 메서드 영역이 겹치는 서비스는 `registerService` 가 거절한다.
 *          - **수명 계약**: 서비스는 빌려 쓴다. 호스트 `shutdown` 이 서비스마다 `onHostShutdown` 을 한 번 불러 호스트 포인터 · 구독을 떼게 하므로, 서비스 객체는
 *            호스트보다 먼저든 늦게든 내려가도 된다. 서비스 쪽에서 맡긴 비동기 일(캐시 라우터 · 접속 상태 찾기)은 서비스 `shutdown` 이 `cancel` 한다.
 *          언리얼 Online Services 의 인터페이스 묶음 · gRPC 서버의 서비스 등록과 같은 모양이다.
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

    /** @brief 서비스가 요청 하나를 받을 때의 문맥입니다(콜백 동안 유효 — 나중에 답할 것은 `_token` 을 복사해 든다). */
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
     * @class IOnlineService
     * @brief 메서드 영역 하나를 맡은 서비스입니다. 모든 콜백은 호스트 `tick` 스레드입니다.
     */
    class SW_GF_API IOnlineService
    {
    public:
        IOnlineService()          = default;
        virtual ~IOnlineService() = default;

        IOnlineService( const IOnlineService& )            = delete;
        IOnlineService& operator=( const IOnlineService& ) = delete;

        virtual uint16 getMethodRange() const     = 0;
        virtual uint32 getProtocolVersion() const = 0;
        /** @brief 로그인 없이 부를 수 있는 메서드인가입니다(기본: 아니다). */
        virtual bool isAnonymousMethod( uint16 method ) const
        {
            (void)method;
            return false;
        }
        /** @brief 요청 하나 — 바로 `host.respond*` 하거나 토큰을 들고 저장 일 뒤에 답한다(한 번만). */
        virtual void onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) = 0;
        /** @brief 서비스 스레드 틱(완료 거두기 · 시한). */
        virtual void onServiceTick( OnlineServiceHost& host, int64 nowMs )
        {
            (void)host;
            (void)nowMs;
        }
        /** @brief 계정이 이 프로세스에서 떠났다(로그아웃 · 끊김 · 밀려남). 거래 취소 · 채널 떠나기. */
        virtual void onAccountLeft( OnlineServiceHost& host, AccountID accountID )
        {
            (void)host;
            (void)accountID;
        }
        /** @brief 이 서비스가 `subscribeServerBus` 한 주제의 메시지입니다. 자기 서버가 낸 것도 온다(`_originServerID` 로 거른다). */
        virtual void onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message )
        {
            (void)host;
            (void)message;
        }
        /**
         * @brief 호스트가 내려간다 — 기다리던 캐시 답(Unavailable)을 모두 알린 뒤, 버스 구독 · 전송을 닫기 전에 한 번. 들고 있던 호스트 포인터 · 구독 목록을 버린다.
         *        이 뒤로 서비스는 호스트를 부르지 않는다 — 서비스 객체가 호스트보다 오래 살아도(늦게 `shutdown`) 사라진 호스트를 만지지 않는다.
         */
        virtual void onHostShutdown( OnlineServiceHost& host ) { (void)host; }
    };
} // namespace sw

namespace sw
{
    /** @brief 호스트 설정입니다. 저장소 · 캐시 · 버스 · 원격 설정은 빌려 쓴다(호스트보다 오래 산다). */
    struct OnlineServiceHostSettings
    {
        StreamTransportSettings _transportSettings{}; ///< `_ioThreadCount == 0` 이면 호스트 `tick` 이 전송을 돈다(루프백 시험)
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

        /** @brief 전송(빌려 쓴다)을 띄우고 받기 시작합니다. 서비스는 그 전에 올린다. */
        [[nodiscard]] bool initialize( IStreamTransport* pTransport, const OnlineServiceHostSettings& settings, string& outError );
        void               shutdown();

        /** @brief 서비스를 올립니다(빌려 쓴다). 영역이 이미 있거나 메서드 번호가 겹치면 오류 로그와 함께 false — 아무것도 올리지 않는다. */
        [[nodiscard]] bool registerService( IOnlineService* pService );
        /** @brief 끝점 pump · 요청 시한 · 캐시 라우터 · 서버 버스 · 서비스 틱. */
        void tick( int64 nowMs );

        // 답 — 서비스가 부른다(한 번만). 이미 답했거나 끝난 요청이면 false.
        bool respondOk( const NetRequestToken& token, const BitWriter& body );
        bool respondError( const NetRequestToken& token, uint16 errorCode, const BitWriter* pDetail = nullptr );

        // 계정 ↔ 연결(계정 키트가 로그인 · 재접속 때 부른다)
        /** @brief 연결에 계정을 붙입니다. 그 계정이 다른 연결에 붙어 있으면 false(계정 키트가 먼저 `unbindAccount`). */
        [[nodiscard]] bool bindAccount( StreamConnectionHandle connection, AccountID accountID );
        /** @brief 계정의 연결을 닫고 서비스들에 `onAccountLeft` 를 알립니다. */
        void unbindAccount( AccountID accountID );
        bool findConnection( AccountID accountID, StreamConnectionHandle& outConnection ) const;
        /** @brief 알림 — Message 프레임 `[종류 u16][몸]`. 종류는 영역 + 0x80..0xFF. 그 계정이 이 프로세스에 없으면 false. */
        bool sendPush( AccountID accountID, uint16 kind, const BitWriter& body );
        /** @brief 이 프로세스에 붙은(로그인한) 모든 계정에 알림을 보냅니다. 보낸 수입니다. 다른 서버는 각자 보낸다(서비스가 버스로 알린다). */
        int32 sendPushToAll( uint16 kind, const BitWriter& body );

        /** @brief 캐시 답 · 채널 메시지를 나눠 주는 라우터입니다(캐시가 없으면 nullptr — 서버 한 대). 호스트 `tick` 이 비운다. */
        EphemeralStoreRouter* getEphemeralRouter();
        /** @brief @p pService 가 @p topic 의 버스 메시지를 받습니다(같은 주제를 둘이 구독해도 버스에는 한 번). 버스가 없으면 아무것도 하지 않는다. */
        void subscribeServerBus( string_view topic, IOnlineService* pService );
        void unsubscribeServerBus( string_view topic, IOnlineService* pService );

        RemoteConfig*    getRemoteConfig() const { return _settings._pRemoteConfig; }
        IServerBus*      getServerBus() const { return _settings._pServerBus; }
        IServiceStore*   getServiceStore() const { return _settings._pServiceStore; }
        IEphemeralStore* getEphemeralStore() const { return _settings._pEphemeralStore; }
        uint16           getListenPort() const;
        int32            getConnectionCount() const { return static_cast<int32>( _mapConnection.size() ); }

        // IStreamEndpointListener · INetRequestHandler — 호스트 `tick` 안
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
            string          _topic{};
            IOnlineService* _pService{ nullptr };
        };

        void            handleHello( const NetRequestContext& context, ConnectionState& connection );
        void            writeServerVersions( BitWriter& outWriter ) const;
        IOnlineService* findService( uint16 method ) const;
        bool            sendPushToConnection( StreamConnectionHandle connection, uint16 kind, const BitWriter& body );
        void            dispatchServerBusMessage( const ServerBusMessage& message );
        void            notifyAccountLeft( AccountID accountID );

        OnlineServiceHostSettings                        _settings;
        StreamMessageEndpoint                            _endpoint;
        NetRequestServer                                 _requestServer;
        EphemeralStoreRouter                             _ephemeralRouter;
        TokenBucketMap                                   _accountBucket;
        TokenBucketMap                                   _remoteBucket;
        vector<IOnlineService*>                          _listService;
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
