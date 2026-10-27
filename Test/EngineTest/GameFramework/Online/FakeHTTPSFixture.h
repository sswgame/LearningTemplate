/**
 * @file FakeHTTPSFixture.h
 * @brief 제공자 테스트(푸시 · 영수증 · 외부 로그인)가 쓰는 가짜 HTTPS 서버와 HTTP 클라이언트 묶음입니다.
 * @details 스위트 파일이 아닙니다. 루프백 망 위에 자체 서명 인증서의 HTTPS 서버(처리기는 테스트가 준다)와, 그 인증서를 믿는 `HTTPClient` 를 세웁니다.
 *          I/O 스레드가 없는 전송이라 `pump` 가 서버를 한 번 돌린다 — 제공자의 `update` 와 번갈아 부른다. 서버 주소는 `makeURL` 이다(호스트 `localhost`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"

#include "GameFramework/Base/Online/HTTP/HTTPClient.h"
#include "GameFramework/Base/Online/HTTP/HTTPServer.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"

#include <chrono>
#include <thread>

namespace test
{
    /** @brief 응답 본문을 글로 채웁니다. */
    inline void setResponseText( sw::HTTPServerResponse& outResponse, int32 statusCode, sw::string_view text )
    {
        outResponse._statusCode = statusCode;
        outResponse._bodyBytes.assign( reinterpret_cast<const uint8*>( text.data() ), reinterpret_cast<const uint8*>( text.data() ) + text.size() );
    }

    /** @brief 가짜 HTTPS 서버 + 클라이언트입니다. 처리기와 클라이언트는 이 객체가 소유합니다. */
    struct FakeHTTPSFixture
    {
        sw::LoopbackStreamNetwork       _network;
        sw::unique_ptr<sw::ITLSContext> _serverContext;
        sw::unique_ptr<sw::ITLSContext> _clientContext;
        sw::HTTPServer                  _server;
        sw::HTTPClient                  _client;
        int64                           _nowMs;

        explicit FakeHTTPSFixture( sw::IHTTPRequestHandler* pHandler )
            : _network{ 21u }
            , _serverContext{}
            , _clientContext{}
            , _server{}
            , _client{}
            , _nowMs{ 1000000 }
        {
            sw::INetSecurityProvider& provider = sw::NetSecurity::getProvider();
            sw::string                certificatePem;
            sw::string                privateKeyPem;
            (void)provider.createSelfSignedCertificate( "localhost", 1, certificatePem, privateKeyPem ); // 실패면 PEM 이 비어 아래 TLS 준비가 실패로 드러난다
            sw::TLSContextConfig serverTLS;
            serverTLS._role           = sw::TLSRole::Server;
            serverTLS._certificatePem = certificatePem;
            serverTLS._privateKeyPem  = privateKeyPem;
            sw::TLSContextConfig clientTLS;
            clientTLS._role       = sw::TLSRole::Client;
            clientTLS._trustPem   = certificatePem;
            clientTLS._serverName = "localhost";
            sw::string error;
            _serverContext = provider.createTLSContext( serverTLS, error );
            _clientContext = provider.createTLSContext( clientTLS, error );
            sw::StreamTransportConfig transportSettings;
            transportSettings._ioThreadCount = 0;
            sw::HTTPServerConfig serverSettings;
            serverSettings._pTLSContext = _serverContext.get();
            (void)_server.initialize( _network.createTransport(), transportSettings, serverSettings, pHandler ); // 테스트 준비 — 실패하면 뒤 단언이 드러낸다
            (void)_client.initialize( _network.createTransport(), transportSettings, sw::HTTPClientConfig{} );   // 테스트 준비 — 실패하면 뒤 단언이 드러낸다
            _client.registerTLSContext( "localhost", _clientContext.get() );
        }

        /** @brief 같은 망 위의 HTTP 클라이언트를 하나 더 만듭니다(응답을 따로 회수해야 하는 제공자 — 애플 로그인의 JWKS 와 교환 · 철회). */
        sw::unique_ptr<sw::HTTPClient> createClient()
        {
            sw::unique_ptr<sw::HTTPClient> client = sw::make_unique<sw::HTTPClient>();
            sw::StreamTransportConfig      transportSettings;
            transportSettings._ioThreadCount = 0;
            (void)client->initialize( _network.createTransport(), transportSettings, sw::HTTPClientConfig{} ); // 테스트 준비 — 실패하면 뒤 단언이 드러낸다
            client->registerTLSContext( "localhost", _clientContext.get() );
            return client;
        }

        sw::string makeURL( const utf8* pPath ) const { return "https://localhost:" + sw::to_string( static_cast<int32>( _server.getListenPort() ) ) + pPath; }

        /** @brief 서버를 한 번 돌리고 시계를 앞으로 보냅니다(`localhost` 이름 해석은 클라이언트의 해석 스레드가 하므로 짧게 쉰다). */
        void pump( int64 advanceMs = 10 )
        {
            _server.update();
            _nowMs += advanceMs;
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
    };
} // namespace test
