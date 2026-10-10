/**
 * @file HTTPServer.h
 * @brief 최소 HTTP/1.1 서버 — 연결마다 요청 하나를 받아 처리기에 넘기고 답한 뒤 닫습니다. PC 외부 로그인의 루프백 리다이렉트 받기(127.0.0.1) · 시험의 가짜 제공자가 씁니다.
 * @details 게임 서비스 · 운영 끝점이 아니다(그쪽은 `Online/Service` · 관측 끝점). 받은 요청은 I/O 스레드에서 모으고 `tick` 을 부른 스레드에서 처리기를 부른다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Transport/IStreamTransport.h"

#include "GameFramework/Base/Online/HTTP/HTTPLink.h"
#include "GameFramework/Base/Online/HTTP/HTTPTypes.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ITlsContext;

    /**
     * @class IHTTPRequestHandler
     * @brief 요청 하나에 답을 채웁니다(`HTTPServer::tick` 의 스레드).
     */
    class SW_GF_API IHTTPRequestHandler
    {
    public:
        IHTTPRequestHandler()          = default;
        virtual ~IHTTPRequestHandler() = default;

        IHTTPRequestHandler( const IHTTPRequestHandler& )            = delete;
        IHTTPRequestHandler& operator=( const IHTTPRequestHandler& ) = delete;

        virtual void onHTTPRequest( const HTTPServerRequest& request, HTTPServerResponse& outResponse ) = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief HTTP 서버 설정입니다. */
    struct HTTPServerSettings
    {
        NetAddress   _listenAddress{ NetAddress::makeLoopback( 0 ) }; ///< 포트 0 = 아무 포트(`getListenPort`)
        ITlsContext* _pTlsContext{ nullptr };                         ///< 있으면 HTTPS(빌려 쓴다)
        int32        _maxRequestBodyBytes{ 64 * 1024 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class HTTPServer
     * @brief HTTP 서버 하나입니다.
     */
    class SW_GF_API HTTPServer final : public IStreamHandler
    {
    public:
        HTTPServer();
        ~HTTPServer() override;

        HTTPServer( const HTTPServer& )            = delete;
        HTTPServer& operator=( const HTTPServer& ) = delete;

        /** @brief 전송(넘겨받는다)을 띄우고 듣습니다. @p pHandler 는 빌려 쓴다. */
        [[nodiscard]] bool initialize( unique_ptr<IStreamTransport> transport, const StreamTransportSettings& transportSettings, const HTTPServerSettings& settings,
                                       IHTTPRequestHandler* pHandler );
        void               shutdown();
        /** @brief 전송을 돌리고(I/O 스레드가 없을 때) 모인 요청을 처리기에 넘겨 답합니다. */
        void tick();

        uint16 getListenPort() const;
        int32  getHandledCount() const { return _handledCount; }

        // IStreamHandler — I/O 스레드
        void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override;
        void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) override;
        void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override;

    private:
        struct Peer
        {
            HTTPLink          _link{};
            HTTPMessageParser _parser{};
            vector<uint8>     _plainBytes{};
            uint8             _bComplete{ SW_FALSE };
        };

        struct ReadyRequest
        {
            HTTPServerRequest      _request{};
            StreamConnectionHandle _handle{};
            uint8                  _bMalformed{ SW_FALSE };
        };

        mutable mutex                           _mutex;
        unordered_map<uint64, unique_ptr<Peer>> _mapPeer;
        vector<ReadyRequest>                    _listReady;
        unique_ptr<IStreamTransport>            _transport;
        HTTPServerSettings                      _settings;
        IHTTPRequestHandler*                    _pHandler;
        int32                                   _ioThreadCount;
        int32                                   _handledCount;
        uint8                                   _bInitialized;
    };
} // namespace sw
