/**
 * @file OnlineServiceClient.h
 * @brief 서비스 틀의 클라이언트 쪽을 정의합니다.
 * @details 연결 하나, 요청 클라이언트, Hello(온라인 스키마 해시 비교), 알림을 영역마다 나누는 일, 끊겼을 때 다시 연결하는 일(물러남)을 맡는다.
 *
 *          - 두 모드가 있다.
 *            **자기 엔드포인트**(`initialize`)는 전송은 빌려 쓰고 엔드포인트와 요청 클라이언트를 가진다. 게임 클라이언트가 쓴다.
 *            **공유 엔드포인트**(`initializeOnSharedEndpoint`)는 엔드포인트, 요청 클라이언트, 전송이 부르는 쪽 것이고, 이 객체는 연결 하나의 Hello 와 알림만 맡는다.
 *            부하 테스트 봇이 연결 수천을 한 프로세스에서 열 때 쓴다.
 *          - Hello 가 끝나기 전에 보낸 요청은 모았다가, 끝나면 맡긴 순서대로 보낸다. 시한은 맡긴 때부터 센다.
 *            다시 연결하면 Hello 를 다시 하고 서비스에 `onClientReady` 를 알린다. 계정 키트는 여기서 재접속(토큰)을 먼저 보낸다.
 *            끊길 때 날아가던 요청은 ConnectionLost 로 끝난다. 재시도는 부르는 쪽이 같은 멱등 키로 해야 한다.
 *          - Hello 첫 필드는 `initialize` 때 정한 온라인 스키마 해시(`OnlineProtocolRegistry`)이다. 서버 해시와 다르면 서버가 `kVersionMismatch` 와 서버 해시로 답한다.
 *            그러면 상태가 `VersionMismatch` 가 되고 다시 연결하지 않는다. 모은 요청과 이 뒤의 요청은 모두 `kVersionMismatch` 로 끝난다.
 *            "업데이트 필요" 를 보이는 신호는 `getState()` 와 응답의 `_errorCode` 이다. 계정 키트는 이것을 `LoginResult::UpdateRequired` 로 바꾼다.
 *            지금 이 신호를 화면으로 보이는 게임 UI 는 없다.
 *          - 모든 콜백은 `tick`(공유 모드는 `handleFrame`, `handleClosed`)을 부른 스레드에서 불린다.
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

#include "GameFramework/Base/Online/Service/OnlineProtocolRegistry.h"
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
     * @brief 클라이언트 쪽 키트 하나(계정, 거래, 채팅 등의 클라이언트)입니다. 그 영역의 요청을 보내고 알림을 받습니다.
     */
    class SW_GF_API IOnlineClientService
    {
    public:
        IOnlineClientService() = default;
        /**
         * @brief 올린 서비스 클라이언트에서 스스로 내립니다.
         * @details 그 영역의 대기 · 보낸 요청의 응답 델리게이트를 부르지 않고 버린다(`OnlineServiceClient::unregisterClientService`).
         *          키트 클라이언트가 서비스 클라이언트보다 먼저 파괴되어도 시한 안에 온 응답이 파괴된 객체를 부르지 않는다.
         *          서비스 클라이언트가 먼저 파괴되면 그 소멸자가 이 키트에서 자기를 지워 두므로 파괴된 객체를 부르지 않는다.
         */
        virtual ~IOnlineClientService();

        IOnlineClientService( const IOnlineClientService& )            = delete;
        IOnlineClientService& operator=( const IOnlineClientService& ) = delete;

        /** @brief 이 키트의 프로토콜 항목입니다. 영역은 여기서 나오고, 항목은 온라인 스키마 해시에 섞입니다. */
        virtual const OnlineProtocolEntry& getProtocol() const = 0;
        uint16                             getMethodRange() const { return getProtocol().getMethodRange(); }
        virtual void                       onServicePush( uint16 kind, BitReader& body ) = 0;
        /** @brief Hello 가 끝났을 때(처음 연결과 다시 연결) 불립니다. 계정 키트는 여기서 재접속 요청을 보냅니다. */
        virtual void onClientReady( OnlineServiceClient& client ) { (void)client; }
        /** @brief 연결이 끊겼을 때 불립니다. 다시 연결을 기다리며, 계정 키트는 로그인 상태를 내립니다. */
        virtual void onClientDisconnected( OnlineServiceClient& client ) { (void)client; }

    private:
        friend class OnlineServiceClient;

        vector<OnlineServiceClient*> _listRegisteredClient{}; ///< 올린 서비스 클라이언트들(테스트는 한 키트를 여럿에 올린다). 소멸자에서 내릴 때 쓴다
        uint16                       _registeredRange{ 0 };   ///< 올릴 때의 영역. 기반 소멸자에서는 가상 함수를 부를 수 없어 담아 둔다
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

    struct OnlineServiceClientConfig
    {
        StreamTransportConfig _transportSettings{}; ///< 자기 엔드포인트 모드 — `_ioThreadCount == 0` 이면 `tick` 이 전송을 돈다
        StreamEndpointConfig  _endpointSettings{};
        NetAddress            _serverAddress{};
        string                _gameBuild{}; ///< Hello 에 싣는 게임 빌드 글(64 B 이하)
        int64                 _maxBackoffMs{ 5000 };
        uint8                 _platform{ 0 };
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

        /** @brief 키트를 올립니다. 빌려 쓰며 초기화 전에 불러야 합니다. 영역이 겹치면 false 입니다. */
        [[nodiscard]] bool registerClientService( IOnlineClientService* pService );
        /**
         * @brief 키트를 내리고 그 영역의 요청을 `cancelRequestsOf` 로 버립니다. 올리지 않은 키트면 아무것도 하지 않습니다.
         * @details 키트 기반 소멸자가 부른다. 스키마 해시는 빌드의 항목 전부에서 나오므로 내려도 바뀌지 않는다.
         */
        void unregisterClientService( IOnlineClientService* pService );
        /**
         * @brief 영역 @p methodRange 의 대기 요청과 보낸 요청에서 응답 델리게이트를 부르지 않고 버립니다.
         * @details 델리게이트 주인(키트 클라이언트)이 먼저 사라질 때 쓴다. 보낸 요청의 응답이 나중에 와도 버려진다.
         */
        void cancelRequestsOf( uint16 methodRange );

        /** @brief 자기 엔드포인트 모드로 시작합니다. 전송(빌려 쓴다)을 띄우고 서버에 연결을 겁니다. */
        [[nodiscard]] bool initialize( IStreamTransport* pTransport, const OnlineServiceClientConfig& settings, string& outError );
        /**
         * @brief 공유 엔드포인트 모드로 시작합니다.
         * @details 엔드포인트, 요청 클라이언트, 전송은 부르는 쪽(봇 실행기)이 갖는다. 이 객체는 연결 하나(@p handle, 이미 열린 것)의 Hello 와 알림 나누기만 한다.
         *          부르는 쪽 리스너가 이 연결의 프레임과 닫힘을 `handleFrame` 과 `handleClosed` 로 넘긴다.
         *          다시 연결하지 않는다. 봇이 스스로 다시 만든다.
         */
        [[nodiscard]] bool initializeOnSharedEndpoint( StreamMessageEndpoint* pEndpoint, NetRequestClient* pRequestClient, StreamConnectionHandle handle,
                                                       const OnlineServiceClientConfig& settings );
        void               shutdown();

        /** @brief 자기 엔드포인트 모드에서는 전송, 엔드포인트, 요청 시한, 다시 연결을 돌립니다. 두 모드 모두 모은 요청의 시한을 봅니다. */
        void update( int64 nowMs );

        /** @brief 요청을 보내고 id 를 반환합니다. Hello 전이거나 문이 닫혀 있으면 모아 둡니다. id 는 콜백의 `_requestID` 와 같으며, 콜백은 정확히 한 번 불립니다. */
        uint64 sendRequest( uint16 method, const BitWriter& body, const NetRequestOptions& options, OnlineResponseDelegate onResponse );
        /** @brief 다음 `sendRequest` 가 돌려줄 id 입니다. 보내는 중에 바로 온 응답을 그 요청의 것으로 맞춰 보는 데 씁니다(`ServiceClientCallTable`). */
        uint64 peekNextRequestID() const { return _nextRequestID; }
        /**
         * @brief 요청 문을 열고 닫습니다. 닫히면 @p ownerRange(계정 키트) 밖의 요청은 문이 열릴 때까지 모읍니다.
         * @details 다시 연결한 뒤 재접속 응답이 오기 전에 다른 키트의 요청이 서버에 닿으면 "로그인 안 됨" 으로 끝난다. 그것을 막으려고 계정 키트가 문을 닫고 연다.
         *          열면 모은 요청을 맡긴 순서대로 보낸다.
         */
        void setRequestGate( uint16 ownerRange, bool bClosed );
        bool isRequestGateClosed() const { return _bGateClosed == SW_TRUE; }

        /** @brief 공유 엔드포인트 모드에서 프레임을 받습니다. Message 프레임(알림)이면 영역의 서비스 `onServicePush` 로 넘깁니다. 요청 응답은 부르는 쪽이 `NetRequestClient::handleFrame` 으로 먼저 거릅니다. */
        void handleFrame( StreamFrameKind kind, const uint8* pBody, int32 bodySize );
        void handleClosed( StreamCloseReason reason );

        bool                   isReady() const { return _state == OnlineClientState::Ready; }
        OnlineClientState      getState() const { return _state; }
        StreamConnectionHandle getConnection() const { return _connection; }
        int64                  getServerTimeMs() const { return _serverTimeMs; }
        uint64                 getRemoteConfigHash() const { return _remoteConfigHash; }

        // IStreamEndpointListener 구현이다. 자기 엔드포인트 모드의 `tick` 안에서 불린다.
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
            uint8                  _bCancelled{ SW_FALSE }; ///< 꺼내 돌던 중 `cancelRequestsOf` 가 가렸다 — 부르지도 보내지도 않는다
        };

        struct PendingCall
        {
            OnlineResponseDelegate _onResponse{};
            uint64                 _requestID{ 0 };
            uint16                 _method{ 0 }; ///< `cancelRequestsOf` 가 영역으로 가린다
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
        void        notifyServices( void ( IOnlineClientService::*pNotify )( OnlineServiceClient& ) );

        OnlineServiceClientConfig          _settings;
        vector<IOnlineClientService*>      _listService;
        vector<QueuedCall>                 _listQueuedCall;
        vector<vector<QueuedCall>*>        _listDrainingCall; ///< `flushQueuedCalls` · `failQueuedCalls` 가 꺼내 도는 지역 목록(겹쳐 불리면 여럿)
        unordered_map<uint64, PendingCall> _mapPendingCall;   ///< 요청 클라이언트의 id → 부른 쪽
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
        uint64                             _schemaHash; ///< `initialize` 때 `OnlineProtocolRegistry::computeSchemaHash` 로 정한다
        uint64                             _nextRequestID;
        uint16                             _gateOwnerRange;
        OnlineClientState                  _state;
        uint8                              _bGateClosed;
        uint8                              _bSharedEndpoint;
        uint8                              _bInitialized;
    };
} // namespace sw
