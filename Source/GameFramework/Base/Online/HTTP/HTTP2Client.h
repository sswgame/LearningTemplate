/**
 * @file HTTP2Client.h
 * @brief HTTP/2 클라이언트입니다(온라인 키트 H2-4). `HTTPClient` 와 같은 모양(`submitRequest` · `update` · `pollResponses`)이라 제공자가 같은 방식으로 쓰고, 안은 호스트마다 하나의 TLS(ALPN `h2`) 연결을 오래 열어 두고 요청을 스트림으로 다중화합니다.
 * @details - 연결은 호스트 · 포트마다 하나이고 처음 요청이 올 때 맺는다. 푸시 서버(APNs)처럼 연결을 오래 쓰는 상대를 위한 구조다 — 연결이 닫히면 날아가던 요청은 전송 실패로 끝나고 다음 요청이 새 연결을 맺는다.
 *          - `https://` 만 받는다(평문 h2c 는 없다). 그 호스트에 `registerTLSContext` 한 컨텍스트(`TLSContextConfig::_alpnProtocol` = `kALPNProtocol`)가 있어야 하고, 핸드셰이크 뒤 협상된 프로토콜이 `h2` 가 아니면 그 연결은 실패다.
 *          - 이름 해석 · 시한 · 응답 상한은 `HTTPClient` 와 같다(해석은 전용 스레드, 시한은 요청한 때부터, 상한을 넘으면 전송 실패). 시한이 넘은 스트림은 RST_STREAM(CANCEL)으로 취소하고 연결은 둔다.
 *          - 프로토콜 상태(스트림 · 흐름 제어 · SETTINGS · GOAWAY)는 `HTTP2Connection` 이 맡고 이 클래스는 전송 · TLS · 요청 id · 시한을 잇는다. GOAWAY 를 받은 연결은 새 요청을 받지 않고 남은 스트림이 끝나면 닫는다.
 *          I/O 스레드가 없는 전송(루프백 테스트)은 `update` 가 `pollIO` 를 돈다. 콜백은 I/O 스레드라 상태는 잠금 하나로 지킨다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/OnceInit.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/IDAllocator.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Task/DedicatedWorker.h"
#include "Core/Time/Clock.h"

#include "GameFramework/Base/Online/HTTP/HTTP2Connection.h"
#include "GameFramework/Base/Online/HTTP/HTTPLink.h"
#include "GameFramework/Base/Online/HTTP/HTTPTypes.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ITLSContext;

    /** @brief HTTP/2 클라이언트 설정입니다. */
    struct HTTP2ClientConfig
    {
        int32 _maxResponseBodyBytes{ HTTPConstant::kDefaultMaxBodyBytes };
        int32 _maxConcurrentRequests{ 256 }; ///< 넘으면 바로 전송 실패(쌓지 않는다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class HTTP2Client
     * @brief HTTP/2 클라이언트 하나입니다. `submitRequest`, `pollResponses`, `update` 는 한 스레드(주인)에서 부른다.
     */
    class SW_GF_API HTTP2Client final : public IStreamHandler
    {
    public:
        /** @brief TLS 컨텍스트가 협상해야 하는 ALPN 이름입니다(`NetSecurity::createClientTLSContext` 의 마지막 인자). */
        static constexpr const utf8* kALPNProtocol = "h2";

        /** @param clock 시한을 재는 단조 시계입니다. 빌려 쓰므로 이 객체보다 오래 살아야 합니다. */
        explicit HTTP2Client( const IClock& clock );
        ~HTTP2Client() override;

        HTTP2Client( const HTTP2Client& )            = delete;
        HTTP2Client& operator=( const HTTP2Client& ) = delete;

        /** @brief 전송(넘겨받는다)을 띄웁니다. */
        [[nodiscard]] bool initialize( unique_ptr<IStreamTransport> transport, const StreamTransportConfig& transportSettings, const HTTP2ClientConfig& settings );
        /** @brief 날아가던 요청은 전송 실패로 끝나고 다음 `pollResponses` 가 회수한다. */
        void shutdown();

        /** @brief @p host 의 요청에 쓸 TLS 컨텍스트(ALPN `h2`)를 등록합니다. 빌려 쓰므로 이 객체보다 오래 살아야 합니다. */
        void registerTLSContext( string_view host, ITLSContext* pTLSContext );
        /** @brief 요청을 맡깁니다. 0 이 아닌 요청 id 입니다(응답의 `_requestID`). 응답은 반드시 한 번 온다(실패도). */
        uint64 submitRequest( const HTTPClientRequest& request );
        /** @brief 전송을 돌리고(I/O 스레드가 없을 때) 이름 해석 · 시한을 봅니다. */
        void update();
        /** @brief 끝난 응답을 @p outListResponse 뒤에 붙입니다. 붙인 수입니다. */
        int32 pollResponses( vector<HTTPClientResponse>& outListResponse );
        int32 getPendingCount() const;
        /** @brief 열려 있는 연결의 수입니다(테스트가 연결 재사용을 봅니다). */
        int32 getSessionCount() const;

        // IStreamHandler: I/O 스레드
        void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override;
        void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) override;
        void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override;

    private:
        enum class ResolveState : uint8
        {
            Pending = 0,
            Resolved,
            Failed
        };

        enum class SessionState : uint8
        {
            Resolving = 0, ///< 이름을 푸는 중
            Connecting,    ///< 연결을 맺는 중
            Handshaking,   ///< TLS 핸드셰이크 중
            Ready          ///< HTTP/2 가 섰다
        };

        /** @brief 연결 전에 기다리는 요청입니다. */
        struct Pending
        {
            HTTPClientRequest _request{};
            HTTPAddress       _url{};
            uint64            _requestID{ 0 };
        };

        /** @brief 호스트 · 포트 하나의 연결입니다. */
        struct Session
        {
            HTTPLink                                     _link{};
            unique_ptr<HTTP2Connection>                  _connection{};
            vector<Pending>                              _listWaiting{}; ///< 연결이 설 때까지 기다리는 요청
            unordered_map<uint64, MonotonicMilliseconds> _mapDeadline{}; ///< 끝나지 않은 요청(기다리는 것 + 날아가는 것)의 시한
            vector<uint8>                                _plainBytes{};
            vector<uint8>                                _outBytes{};
            string                                       _key{};
            string                                       _host{};
            NetAddress                                   _address{};
            StreamConnectionHandle                       _handle{};
            uint64                                       _sessionID{ 0 };
            SessionState                                 _state{ SessionState::Resolving };
            ResolveState                                 _resolveState{ ResolveState::Pending };
            uint8                                        _bAccepting{ SW_TRUE }; ///< 새 요청을 받는가 — 실패 · GOAWAY 뒤에는 꺼진다
        };

        /** @brief 잠금 안에서 부릅니다. 호스트의 연결을 만들고(숫자 주소면 바로 연결, 이름이면 해석을 맡긴다) 돌려줍니다. 연결하지 못하면 nullptr. */
        Session* createSessionLocked( const HTTPAddress& url, ITLSContext* pTLSContext, string_view key ) SW_REQUIRES_LOCK( _mutex );
        bool     connectSessionLocked( Session& session ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 잠금 안에서 부릅니다. 해석이 끝났거나 시한이 지난 연결을 이어 가거나 정리합니다. */
        void advanceResolvingLocked() SW_REQUIRES_LOCK( _mutex );
        void resolveSession( uint64& sessionID, bool bStopping );
        /** @brief 잠금 안에서 부릅니다. 핸드셰이크가 끝난 연결에서 HTTP/2 를 세우고 기다리던 요청을 보냅니다. false 면 연결이 실패했다(부르는 쪽이 닫는다). */
        [[nodiscard]] bool establishLocked( Session& session ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 잠금 안에서 부릅니다. 쌓인 보낼 바이트를 쓰고 끝난 응답을 회수합니다. false 면 쓰기가 실패했다. */
        [[nodiscard]] bool pumpLocked( Session& session ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 잠금 안에서 부릅니다. 기다리던 요청을 연결에 맡기고 `pumpLocked` 합니다. */
        [[nodiscard]] bool sendWaitingLocked( Session& session ) SW_REQUIRES_LOCK( _mutex );
        void               collectLocked( Session& session ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 잠금 안에서 부릅니다. 연결의 모든 요청을 실패로 끝내고 더는 새 요청을 받지 않게 합니다(전송을 닫는 일은 부르는 쪽이 한다). */
        void     failSessionLocked( Session& session, const utf8* pFailure ) SW_REQUIRES_LOCK( _mutex );
        void     eraseSessionLocked( uint64 sessionID ) SW_REQUIRES_LOCK( _mutex );
        void     failImmediately( uint64 requestID, const utf8* pFailure ) SW_REQUIRES_LOCK( _mutex );
        Session* findByHandleLocked( StreamConnectionHandle handle ) SW_REQUIRES_LOCK( _mutex );

        const IClock&                                            _clock;
        mutable mutex                                            _mutex;
        unordered_map<uint64, unique_ptr<Session>> _mapSession   SW_GUARDED_BY( _mutex ); ///< 연결 id → 연결
        unordered_map<string, uint64> _mapKeyToSession           SW_GUARDED_BY( _mutex ); ///< 새 요청을 받는 연결(호스트:포트)
        unordered_map<uint64, uint64> _mapHandleToSession        SW_GUARDED_BY( _mutex ); ///< 스트림 연결(packed) → 연결 id
        unordered_map<string, ITLSContext*> _mapHostToTLSContext SW_GUARDED_BY( _mutex );
        vector<HTTPClientResponse> _listDone                     SW_GUARDED_BY( _mutex );
        OnceInit<unique_ptr<IStreamTransport>>                   _transport;
        OnceInit<HTTP2ClientConfig>                              _settings;
        DedicatedWorker<uint64>                                  _resolver; ///< 이름 해석 스레드(연결 id 를 받는다). 처음 이름을 받을 때 띄운다
        IDAllocator<uint64> _requestIDAllocator                  SW_GUARDED_BY( _mutex );
        IDAllocator<uint64> _sessionIDAllocator                  SW_GUARDED_BY( _mutex );
        int32                                                    _ioThreadCount;
        atomic<bool>                                             _bInitialized;
    };
} // namespace sw
