/**
 * @file OnlineServiceClient.h
 * @brief 서비스 틀의 클라이언트 쪽 — 연결 하나 + 요청 클라이언트 + Hello(키트 판 협상) + 알림을 영역마다 나누기 + 끊기면 다시 연결(물러남).
 * @details - 두 모드: **자기 끝점**(`initialize` — 전송은 빌려 쓰고 끝점 · 요청 클라이언트를 가진다, 게임 클라이언트)과 **공유 끝점**(`initializeOnSharedEndpoint` —
 *            끝점 · 요청 클라이언트 · 전송은 부르는 쪽 것, 이 객체는 연결 하나의 Hello · 알림만 — 부하 시험 봇이 연결 수천을 한 프로세스에서 연다).
 *          - Hello 가 끝나기 전에 보낸 요청은 모았다가 끝나면 맡긴 순서대로 보낸다(시한은 맡긴 때부터). 다시 연결하면 Hello 를 다시 하고 서비스에 `onClientReady`
 *            를 알린다 — 계정 키트가 재접속(토큰)을 먼저 보내는 자리다. 끊길 때 날아가던 요청은 ConnectionLost 로 끝난다(재시도는 같은 멱등 키로 부르는 쪽이).
 *          - 판이 안 맞으면(`kVersionMismatch`) 다시 연결하지 않는다 — 클라이언트는 "업데이트 필요" 화면.
 *          - 모든 콜백은 `tick`(공유 모드는 `handleFrame` · `handleClosed`)을 부른 스레드다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/Network/Message/StreamMessageEndpoint.h"
#include "Core/Network/NetTypes.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class IStreamTransport;
    class OnlineServiceClient;

    /** @brief 받은 응답입니다. 몸은 콜백 동안만 유효합니다. */
    struct OnlineResponse
    {
        const uint8*     _pBody{ nullptr }; ///< Ok 면 서비스 몸, ApplicationError 면 오류 코드 뒤의 자세한 몸
        int32            _bodySize{ 0 };
        uint64           _requestID{ 0 }; ///< `sendRequest` 가 돌려준 id
        uint16           _errorCode{ 0 }; ///< `OnlineError::kOk` 또는 공통 · 키트 코드. 전송 실패(끊김 · 시한)는 `kUnavailable`
        NetRequestStatus _status{ NetRequestStatus::Ok };

        bool isOk() const { return _status == NetRequestStatus::Ok; }
    };

    using OnlineResponseDelegate = Delegate<void( const OnlineResponse& )>;
} // namespace sw

namespace sw
{
    /**
     * @class IOnlineClientService
     * @brief 클라이언트 쪽 키트 하나(계정 · 거래 · 채팅 …의 클라이언트)입니다 — 영역 · 판을 Hello 에 싣고, 그 영역의 알림을 받습니다.
     */
    class SW_GF_API IOnlineClientService
    {
    public:
        IOnlineClientService()          = default;
        virtual ~IOnlineClientService() = default;

        IOnlineClientService( const IOnlineClientService& )            = delete;
        IOnlineClientService& operator=( const IOnlineClientService& ) = delete;

        virtual uint16 getMethodRange() const                        = 0;
        virtual uint32 getProtocolVersion() const                    = 0;
        virtual void   onServicePush( uint16 kind, BitReader& body ) = 0;
        /** @brief Hello 가 끝났다(처음 · 다시 연결). 계정 키트는 여기서 재접속 요청을 보낸다. */
        virtual void onClientReady( OnlineServiceClient& client ) { (void)client; }
        /** @brief 연결이 끊겼다(다시 연결을 기다린다). 계정 키트는 로그인 상태를 내린다. */
        virtual void onClientDisconnected( OnlineServiceClient& client ) { (void)client; }
    };
} // namespace sw

namespace sw
{
    enum class OnlineClientState : uint8
    {
        Disconnected = 0,
        Connecting,
        Negotiating, ///< 열렸고 Hello 를 기다린다
        Ready,
        VersionMismatch ///< 다시 연결하지 않는다
    };

    struct OnlineServiceClientSettings
    {
        StreamTransportSettings _transportSettings{}; ///< 자기 끝점 모드 — `_ioThreadCount == 0` 이면 `tick` 이 전송을 돈다
        StreamEndpointSettings  _endpointSettings{};
        NetAddress              _serverAddress{};
        string                  _gameBuild{}; ///< Hello 에 싣는 게임 빌드 글(64 B 이하)
        int64                   _maxBackoffMs{ 5000 };
        uint8                   _platform{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class OnlineServiceClient
     * @brief 서비스 틀 클라이언트입니다.
     */
    class SW_GF_API OnlineServiceClient final : public IStreamEndpointListener
    {
    public:
        OnlineServiceClient();
        ~OnlineServiceClient() override;

        OnlineServiceClient( const OnlineServiceClient& )            = delete;
        OnlineServiceClient& operator=( const OnlineServiceClient& ) = delete;

        /** @brief 키트를 올립니다(빌려 쓴다, 초기화 전에). 영역이 겹치면 false. */
        [[nodiscard]] bool registerClientService( IOnlineClientService* pService );

        /** @brief 자기 끝점 모드 — 전송(빌려 쓴다)을 띄우고 서버에 겁니다. */
        [[nodiscard]] bool initialize( IStreamTransport* pTransport, const OnlineServiceClientSettings& settings, string& outError );
        /**
         * @brief 공유 끝점 모드 — 끝점 · 요청 클라이언트 · 전송은 부르는 쪽(봇 실행기)이 갖고, 이 객체는 연결 하나(@p handle — 이미 열린 것)의 Hello · 알림 나누기만 합니다.
         *        부르는 쪽 리스너가 이 연결의 프레임 · 닫힘을 `handleFrame` · `handleClosed` 로 넘깁니다. 다시 연결하지 않는다(봇이 스스로 다시 만든다).
         */
        [[nodiscard]] bool initializeOnSharedEndpoint( StreamMessageEndpoint* pEndpoint, NetRequestClient* pRequestClient, StreamConnectionHandle handle,
                                                       const OnlineServiceClientSettings& settings );
        void               shutdown();

        /** @brief 자기 끝점 모드는 전송 · 끝점 · 요청 시한 · 다시 연결을 돈다. 두 모드 모두 모은 요청의 시한을 본다. */
        void tick( int64 nowMs );

        /** @brief 요청을 보냅니다(Hello 전 · 문이 닫혀 있으면 모은다). id 입니다 — 콜백의 `_requestID`. 콜백은 정확히 한 번. */
        uint64 sendRequest( uint16 method, const BitWriter& body, const NetRequestOptions& options, OnlineResponseDelegate onResponse );
        /**
         * @brief 요청 문 — 닫히면 @p ownerRange(계정 키트) 밖의 요청은 문이 열릴 때까지 모은다. 다시 연결한 뒤 재접속 응답 전에 다른 키트의 요청이 서버에 닿아
         *        "로그인 안 됨" 으로 끝나지 않게 계정 키트가 닫고 연다. 열면 모은 요청을 맡긴 순서대로 보낸다.
         */
        void setRequestGate( uint16 ownerRange, bool bClosed );
        bool isRequestGateClosed() const { return _bGateClosed == SW_TRUE; }

        /** @brief 공유 끝점 모드 — Message 프레임(알림)이면 영역의 서비스 `onServicePush` 로. 요청 응답은 부르는 쪽이 `NetRequestClient::handleFrame` 으로 먼저 거른다. */
        void handleFrame( StreamFrameKind kind, const uint8* pBody, int32 bodySize );
        void handleClosed( StreamCloseReason reason );

        bool                   isReady() const { return _state == OnlineClientState::Ready; }
        OnlineClientState      getState() const { return _state; }
        StreamConnectionHandle getConnection() const { return _connection; }
        int64                  getServerTimeMs() const { return _serverTimeMs; }
        uint64                 getRemoteConfigHash() const { return _remoteConfigHash; }

        // IStreamEndpointListener — 자기 끝점 모드의 `tick` 안
        void onEndpointOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override;
        void onEndpointFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize ) override;
        void onEndpointClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override;

    private:
        struct QueuedCall
        {
            vector<uint8>          _bodyBytes{};
            NetRequestOptions      _options{};
            OnlineResponseDelegate _onResponse{};
            uint64                 _requestID{ 0 };
            int64                  _deadlineMs{ 0 };
            uint16                 _method{ 0 };
        };

        struct PendingCall
        {
            OnlineResponseDelegate _onResponse{};
            uint64                 _requestID{ 0 };
        };

        void        sendHello();
        void        sendQueuedCall( QueuedCall& call );
        void        flushQueuedCalls();
        bool        isGated( uint16 method ) const;
        void        failQueuedCalls( NetRequestStatus status, uint16 errorCode );
        void        beginConnect();
        void        onHelloResponse( const NetResponse& response );
        void        onNetResponse( const NetResponse& response );
        static void deliver( const OnlineResponseDelegate& onResponse, uint64 requestID, const NetResponse& response );
        void        dispatchPush( const uint8* pBody, int32 bodySize );

        OnlineServiceClientSettings        _settings;
        vector<IOnlineClientService*>      _listService;
        vector<QueuedCall>                 _listQueuedCall;
        unordered_map<uint64, PendingCall> _mapPendingCall; ///< 요청 클라이언트의 id → 부른 쪽
        unique_ptr<StreamMessageEndpoint>  _ownedEndpoint;
        unique_ptr<NetRequestClient>       _ownedRequestClient;
        StreamMessageEndpoint*             _pEndpoint;
        NetRequestClient*                  _pRequestClient;
        IStreamTransport*                  _pTransport;
        const QueuedCall*                  _pSendingCall; ///< `sendRequest` 가 그 자리에서 실패를 부를 때의 대상
        StreamConnectionHandle             _connection;
        int64                              _nowMs;
        int64                              _nextConnectMs;
        int64                              _backoffMs;
        int64                              _serverTimeMs;
        uint64                             _remoteConfigHash;
        uint64                             _nextRequestID;
        uint16                             _gateOwnerRange;
        OnlineClientState                  _state;
        uint8                              _bGateClosed;
        uint8                              _bSharedEndpoint;
        uint8                              _bInitialized;
    };
} // namespace sw
