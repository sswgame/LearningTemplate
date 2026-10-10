/**
 * @file HttpClient.h
 * @brief 최소 HTTP/1.1 클라이언트 — 요청을 맡기고(`submitRequest`) 응답을 거둡니다(`pollResponses`). 요청마다 연결 하나, TLS 는 호스트마다 올린 컨텍스트로.
 * @details - 전송은 이 객체가 갖는다(처리기 = 이 객체). I/O 스레드가 없는 전송(루프백 시험)은 `tick` 이 `pollIo` 를 돈다. 콜백은 I/O 스레드라 상태는 잠금 하나로 지킨다.
 *          - `https://` 는 그 호스트에 `registerTLSContext` 한 컨텍스트가 있어야 한다(서버 이름 검사 · 신뢰는 컨텍스트가 정한다) — 없으면 전송 실패로 끝난다.
 *          - 시한은 맡긴 때부터(`_timeoutMs`) — 넘으면 연결을 끊고 전송 실패. 응답 몸 상한을 넘어도 전송 실패.
 *          언리얼 FHttpModule · libcurl multi 처럼 비동기 요청 · 완료 거두기 모양이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Transport/IStreamTransport.h"

#include "GameFramework/Base/Online/Http/HttpLink.h"
#include "GameFramework/Base/Online/Http/HttpTypes.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ITLSContext;

    /** @brief HTTP 클라이언트 설정입니다. */
    struct HttpClientSettings
    {
        int32 _maxResponseBodyBytes{ HttpConstant::kDefaultMaxBodyBytes };
        int32 _maxConcurrentRequests{ 32 }; ///< 넘으면 바로 전송 실패(쌓지 않는다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class HttpClient
     * @brief HTTP 클라이언트 하나입니다. `submitRequest` · `pollResponses` · `tick` 은 한 스레드(주인)에서 부른다.
     */
    class SW_GF_API HttpClient final : public IStreamHandler
    {
    public:
        HttpClient();
        ~HttpClient() override;

        HttpClient( const HttpClient& )            = delete;
        HttpClient& operator=( const HttpClient& ) = delete;

        /** @brief 전송(넘겨받는다)을 띄웁니다. */
        [[nodiscard]] bool initialize( unique_ptr<IStreamTransport> transport, const StreamTransportSettings& transportSettings, const HttpClientSettings& settings );
        /** @brief 날아가던 요청은 전송 실패로 끝나고 다음 `pollResponses` 가 거둔다. */
        void shutdown();

        /** @brief @p host 의 `https://` 요청에 쓸 TLS 컨텍스트(빌려 쓴다 — 이 객체보다 오래 산다)를 올립니다. */
        void registerTLSContext( string_view host, ITLSContext* pTLSContext );
        /** @brief 요청을 맡깁니다. 0 이 아닌 요청 id 입니다(응답의 `_requestID`). 응답은 반드시 한 번 온다(실패도). */
        uint64 submitRequest( const HttpClientRequest& request, int64 nowMs );
        /** @brief 전송을 돌리고(I/O 스레드가 없을 때) 시한을 봅니다. */
        void tick( int64 nowMs );
        /** @brief 끝난 응답을 @p outListResponse 뒤에 붙입니다. 붙인 수입니다. */
        int32 pollResponses( vector<HttpClientResponse>& outListResponse );
        int32 getPendingCount() const;

        // IStreamHandler — I/O 스레드
        void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override;
        void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) override;
        void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override;

    private:
        struct Call
        {
            HttpLink          _link{};
            HttpMessageParser _parser{};
            vector<uint8>     _requestBytes{};
            vector<uint8>     _plainBytes{};
            uint64            _requestID{ 0 };
            int64             _deadlineMs{ 0 };
            uint8             _bOpened{ SW_FALSE };
        };

        /** @brief 잠금 안에서 — 요청을 끝내고 응답을 쌓는다(연결은 부르는 쪽이 닫는다). */
        void finishLocked( Call& call, const utf8* pFailure );
        void failImmediately( uint64 requestID, const utf8* pFailure );

        mutable mutex                           _mutex;
        unordered_map<uint64, unique_ptr<Call>> _mapCall; ///< 연결(packed) → 요청
        unordered_map<string, ITLSContext*>     _mapHostToTLSContext;
        vector<HttpClientResponse>              _listDone;
        unique_ptr<IStreamTransport>            _transport;
        HttpClientSettings                      _settings;
        uint64                                  _nextRequestID;
        int32                                   _ioThreadCount;
        uint8                                   _bInitialized;
    };
} // namespace sw
