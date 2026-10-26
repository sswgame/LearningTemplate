/**
 * @file HTTPClient.h
 * @brief 최소 HTTP/1.1 클라이언트입니다. 요청을 보내고(`submitRequest`) 응답을 회수합니다(`pollResponses`). 요청마다 연결 하나이고, TLS 는 호스트마다 등록한 컨텍스트로 합니다.
 * @details - 전송은 이 객체가 갖는다(처리기 = 이 객체). I/O 스레드가 없는 전송(루프백 테스트)은 `tick` 이 `pollIO` 를 돈다. 콜백은 I/O 스레드라 상태는 잠금 하나로 지킨다.
 *          - `https://` 는 그 호스트에 `registerTLSContext` 한 컨텍스트가 있어야 한다(서버 이름 검사와 신뢰는 컨텍스트가 정한다). 없으면 전송 실패로 끝난다.
 *          - 시한은 요청한 때부터 센다(`_timeoutMs`). 시각은 생성자에서 받은 단조 시계(`IClock`)로 읽어 벽시계 보정에 흔들리지 않는다. 넘으면 연결을 끊고 전송 실패다. 응답 본문 상한을 넘어도 전송 실패다.
 *          - 호스트가 이름이면 전용 해석 스레드(`PlatformSocketUtil::resolveAddress`, 처음 쓸 때 시작한다)에 맡기고, 해석되면 `update` 가 연결한다.
 *            DNS 왕복은 막는 호출이라 요청 경로(주인 스레드, 잠금)와 TaskManager 워커에서 하지 않는다. libcurl 의 스레드 해석기에 해당한다.
 *            풀리지 않으면 전송 실패("could not resolve host"), 해석 중에도 시한은 흐른다. 숫자 주소는 해석 없이 바로 연결한다.
 *          언리얼 FHttpModule 과 libcurl multi 처럼 비동기 요청과 완료 회수 구조다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/deque.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Time/Clock.h"

#include "GameFramework/Base/Online/HTTP/HTTPLink.h"
#include "GameFramework/Base/Online/HTTP/HTTPTypes.h"
#include "GameFramework/GameFrameworkExports.h"

#include <condition_variable>

namespace sw
{
    class ITLSContext;

    /** @brief HTTP 클라이언트 설정입니다. */
    struct HTTPClientConfig
    {
        int32 _maxResponseBodyBytes{ HTTPConstant::kDefaultMaxBodyBytes };
        int32 _maxConcurrentRequests{ 32 }; ///< 넘으면 바로 전송 실패(쌓지 않는다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class HTTPClient
     * @brief HTTP 클라이언트 하나입니다. `submitRequest`, `pollResponses`, `tick` 은 한 스레드(주인)에서 부른다.
     */
    class SW_GF_API HTTPClient final : public IStreamHandler
    {
    public:
        /** @param clock 시한을 재는 단조 시계입니다(운영 `SystemMonotonicClock`, 테스트 `ManualClock`). 빌려 쓰므로 이 객체보다 오래 살아야 합니다. */
        explicit HTTPClient( const IClock& clock );
        ~HTTPClient() override;

        HTTPClient( const HTTPClient& )            = delete;
        HTTPClient& operator=( const HTTPClient& ) = delete;

        /** @brief 전송(넘겨받는다)을 띄웁니다. */
        [[nodiscard]] bool initialize( unique_ptr<IStreamTransport> transport, const StreamTransportConfig& transportSettings, const HTTPClientConfig& settings );
        /** @brief 날아가던 요청은 전송 실패로 끝나고 다음 `pollResponses` 가 회수한다. */
        void shutdown();

        /** @brief @p host 의 `https://` 요청에 쓸 TLS 컨텍스트를 등록합니다. 빌려 쓰므로 이 객체보다 오래 살아야 합니다. */
        void registerTLSContext( string_view host, ITLSContext* pTLSContext );
        /** @brief 요청을 맡깁니다. 0 이 아닌 요청 id 입니다(응답의 `_requestID`). 응답은 반드시 한 번 온다(실패도). */
        uint64 submitRequest( const HTTPClientRequest& request );
        /** @brief 전송을 돌리고(I/O 스레드가 없을 때) 시한을 봅니다. */
        void update();
        /** @brief 끝난 응답을 @p outListResponse 뒤에 붙입니다. 붙인 수입니다. */
        int32 pollResponses( vector<HTTPClientResponse>& outListResponse );
        int32 getPendingCount() const;

        // IStreamHandler: I/O 스레드
        void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override;
        void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) override;
        void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override;

    private:
        /** @brief 이름 해석의 진행입니다. */
        enum class ResolveState : uint8
        {
            Pending = 0,
            Resolved,
            Failed
        };

        struct Call
        {
            HTTPLink              _link{};
            HTTPMessageParser     _parser{};
            vector<uint8>         _requestBytes{};
            vector<uint8>         _plainBytes{};
            string                _resolveHost{}; ///< 해석을 기다리는 호스트 이름(숫자 주소면 비어 있다)
            NetAddress            _address{};     ///< 연결할 주소. 해석이 끝나면 채운다
            uint64                _requestID{ 0 };
            MonotonicMilliseconds _deadline{ 0 };
            ResolveState          _resolveState{ ResolveState::Pending };
            uint8                 _bOpened{ SW_FALSE };
        };

        /** @brief 잠금 안에서 부릅니다. 요청을 끝내고 응답을 쌓습니다(연결은 부르는 쪽이 닫는다). */
        void finishLocked( Call& call, const utf8* pFailure );
        void failImmediately( uint64 requestID, const utf8* pFailure );
        /** @brief 잠금 안에서 부릅니다. @p call 의 주소로 연결하고 연결 테이블에 넣습니다. 연결하지 못하면 응답을 쌓습니다. */
        void connectLocked( unique_ptr<Call> call );
        /** @brief 잠금 안에서 부릅니다. 해석이 끝났거나 시한이 지난 요청을 연결하거나 끝냅니다(`update`). */
        void advanceResolvingLocked( MonotonicMilliseconds now );
        /** @brief 해석 스레드입니다. 맡긴 이름을 하나씩 해석해 요청에 적습니다. 종료할 때까지 돕니다. */
        void runResolver();

        const IClock&                           _clock;
        mutable mutex                           _mutex;
        unordered_map<uint64, unique_ptr<Call>> _mapCall; ///< 연결(packed) → 요청
        unordered_map<string, ITLSContext*>     _mapHostToTLSContext;
        vector<unique_ptr<Call>>                _listResolvingCall;    ///< 이름 해석을 기다리는 요청
        deque<uint64>                           _listResolveRequestID; ///< 해석 스레드가 아직 집지 않은 요청 id
        vector<HTTPClientResponse>              _listDone;
        unique_ptr<IStreamTransport>            _transport;
        HTTPClientConfig                        _settings;
        sw::thread                              _resolver; ///< 처음 이름을 받을 때 띄운다
        std::condition_variable_any             _resolveReady;
        uint64                                  _nextRequestID;
        int32                                   _ioThreadCount;
        uint8                                   _bInitialized;
        uint8                                   _bResolverStopping;
    };
} // namespace sw
