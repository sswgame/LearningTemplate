// 최소 HTTP — URL · 퍼센트 · 폼, 파서(Content-Length · chunked · 닫힐 때까지 · 상한), 루프백 클라이언트 ↔ 서버(평문 · TLS 1.3 자체 서명 신뢰),
// 404 · 쿼리 · POST 몸(localhost 는 해석 스레드가 푼다), 시한 · 풀리지 않는 이름 · TLS 컨텍스트 없음은 전송 실패.
#include "pch.h"

#include "Core/Network/NetTypes.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/Time/Clock.h"
#include "Core/Time/MonotonicClock.h"

#include "GameFramework/Base/Online/HTTP/BlockingHTTPClient.h"
#include "GameFramework/Base/Online/HTTP/HTTPClient.h"
#include "GameFramework/Base/Online/HTTP/HTTPServer.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"

#include "TestFramework/TestFramework.h"

#include <chrono>
#include <thread>

using namespace sw;

namespace
{
    /** @brief 경로마다 정한 답 — `/echo` 는 쿼리 · 몸을 되돌리고 `/chunked` 는 chunked 로, 나머지는 404. */
    class EchoHandler final : public IHTTPRequestHandler
    {
    public:
        void onHTTPRequest( const HTTPServerRequest& request, HTTPServerResponse& outResponse ) override
        {
            if ( request._path == "/echo" )
            {
                const HTTPHeader* pName = request.findQuery( "name" );
                string            text  = string( toString( request._method ) ) + ":" + ( pName != nullptr ? pName->_value : string() ) + ":";
                text += request.getBodyText();
                outResponse._bodyBytes.assign( reinterpret_cast<const uint8*>( text.data() ), reinterpret_cast<const uint8*>( text.data() ) + text.size() );
                outResponse._listHeader.push_back( HTTPHeader{ "X-Test", "yes" } );
                return;
            }
            if ( request._path == "/chunked" )
            {
                const string text = "this body arrives in seven byte chunks";
                outResponse._bodyBytes.assign( reinterpret_cast<const uint8*>( text.data() ), reinterpret_cast<const uint8*>( text.data() ) + text.size() );
                outResponse._bChunked = SW_TRUE;
                return;
            }
            outResponse._statusCode = HTTPConstant::kStatusNotFound;
        }
    };

    struct HTTPPair
    {
        LoopbackStreamNetwork _network;
        EchoHandler           _handler;
        HTTPServer            _server;
        ManualClock           _clock; ///< 클라이언트 시한을 재는 시계(`run` 이 단계마다 민다). 클라이언트보다 먼저 선언한다
        HTTPClient            _client;

        HTTPPair()
            : _network{ 7u }
            , _handler{}
            , _server{}
            , _clock{ MonotonicMilliseconds{ 0 } }
            , _client{ _clock }
        {
        }

        bool initialize( ITLSContext* pServerContext, int32 maxBodyBytes = HTTPConstant::kDefaultMaxBodyBytes )
        {
            StreamTransportConfig transportSettings;
            transportSettings._ioThreadCount = 0;
            HTTPServerConfig serverSettings;
            serverSettings._pTLSContext = pServerContext;
            HTTPClientConfig clientSettings;
            clientSettings._maxResponseBodyBytes = maxBodyBytes;
            return _server.initialize( _network.createTransport(), transportSettings, serverSettings, &_handler ) &&
                   _client.initialize( _network.createTransport(), transportSettings, clientSettings );
        }

        /** @brief 요청 하나를 끝까지 돌립니다. @p bServe 가 false 면 서버를 돌리지 않는다(시한 테스트). */
        HTTPClientResponse run( const HTTPClientRequest& request, bool bServe = true )
        {
            const uint64 requestID = _client.submitRequest( request );
            for ( int32 step = 0; step < 400; ++step )
            {
                if ( bServe )
                    _server.update();
                _client.update();
                vector<HTTPClientResponse> listResponse;
                (void)_client.pollResponses( listResponse );
                for ( HTTPClientResponse& response : listResponse )
                {
                    if ( response._requestID == requestID )
                        return std::move( response );
                }
                _clock.advanceMilliseconds( 100 );
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) ); // 이름(localhost)은 해석 스레드가 푼다 — 실제 시간을 조금 준다
            }
            HTTPClientResponse lost;
            lost._failureText = "test harness ran out of steps";
            return lost;
        }

        string makeURL( const utf8* pScheme, const utf8* pPath ) const { return string( pScheme ) + "://localhost:" + to_string( static_cast<int32>( _server.getListenPort() ) ) + pPath; }
    };
} // namespace

SW_TEST_CASE( HTTPTest, URLFormAndPercentRules )
{
    HTTPAddress url;
    SW_ASSERT_TRUE( HTTPAddress::parse( "https://example.com:8443/a/b?x=1", url ) );
    SW_EXPECT_EQUAL( string( "example.com" ), url._host );
    SW_EXPECT_EQUAL( uint16( 8443 ), url._port );
    SW_EXPECT_EQUAL( string( "/a/b?x=1" ), url._target );
    SW_EXPECT_TRUE( url._bSecure == SW_TRUE );
    SW_ASSERT_TRUE( HTTPAddress::parse( "http://127.0.0.1?q", url ) );
    SW_EXPECT_EQUAL( uint16( 80 ), url._port );
    SW_EXPECT_EQUAL( string( "/?q" ), url._target );
    SW_EXPECT_FALSE( HTTPAddress::parse( "ftp://example.com/", url ) );
    SW_EXPECT_FALSE( HTTPAddress::parse( "https://user@example.com/", url ) );
    SW_EXPECT_FALSE( HTTPAddress::parse( "https://example.com:0/", url ) );
    SW_EXPECT_FALSE( HTTPAddress::parse( "https://example.com/#frag", url ) );

    const vector<HTTPHeader> listPair = {
        {"redirect_uri", "http://127.0.0.1:5/callback"},
        {       "scope",              "openid profile"},
        {          "한",                          "글"}
    };
    const string form = HTTPUtil::encodeForm( listPair );
    SW_EXPECT_EQUAL( string( "redirect_uri=http%3A%2F%2F127.0.0.1%3A5%2Fcallback&scope=openid%20profile&%ED%95%9C=%EA%B8%80" ), form );
    vector<HTTPHeader> decoded;
    SW_ASSERT_TRUE( HTTPUtil::decodeForm( form, decoded ) );
    SW_ASSERT_EQUAL( size_t( 3 ), decoded.size() );
    SW_EXPECT_EQUAL( string( "openid profile" ), decoded[1]._value );
    SW_EXPECT_EQUAL( string( "글" ), decoded[2]._value );
    SW_ASSERT_TRUE( HTTPUtil::decodeForm( "a=1+2", decoded ) );
    SW_EXPECT_EQUAL( string( "1 2" ), decoded[0]._value );
    SW_EXPECT_FALSE( HTTPUtil::decodeForm( "a=%G1", decoded ) );
}

SW_TEST_CASE( HTTPTest, ParserHandlesLengthChunkedCloseAndLimits )
{
    HTTPMessageParser parser;
    parser.reset( HTTPMessageKind::Response, 64 );
    const string fixed = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nX-A:  b \r\n\r\nhello";
    // 한 바이트씩 넣어도 같은 결과
    HTTPParseState state = HTTPParseState::NeedMore;
    for ( const utf8 ch : fixed )
    {
        state = parser.append( reinterpret_cast<const uint8*>( &ch ), 1 );
    }
    SW_ASSERT_TRUE( state == HTTPParseState::Complete );
    SW_EXPECT_EQUAL( 200, parser.getStatusCode() );
    SW_EXPECT_EQUAL( size_t( 5 ), parser.getBody().size() );
    SW_EXPECT_EQUAL( string( "b" ), HTTPUtil::findHeader( parser.getHeaders(), "x-a" )->_value );

    parser.reset( HTTPMessageKind::Response, 64 );
    const string chunked = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3;ext=1\r\nabc\r\n2\r\nde\r\n0\r\nX-Trailer: 1\r\n\r\n";
    SW_ASSERT_TRUE( parser.append( reinterpret_cast<const uint8*>( chunked.data() ), chunked.size() ) == HTTPParseState::Complete );
    SW_EXPECT_EQUAL( string( "abcde" ), string( reinterpret_cast<const utf8*>( parser.getBody().data() ), parser.getBody().size() ) );

    parser.reset( HTTPMessageKind::Response, 64 );
    const string untilClose = "HTTP/1.0 200 OK\r\n\r\nrest of body";
    SW_EXPECT_TRUE( parser.append( reinterpret_cast<const uint8*>( untilClose.data() ), untilClose.size() ) == HTTPParseState::NeedMore );
    SW_EXPECT_TRUE( parser.finishOnClose() == HTTPParseState::Complete );
    SW_EXPECT_EQUAL( size_t( 12 ), parser.getBody().size() );

    parser.reset( HTTPMessageKind::Response, 4 );
    const string tooLarge = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
    SW_EXPECT_TRUE( parser.append( reinterpret_cast<const uint8*>( tooLarge.data() ), tooLarge.size() ) == HTTPParseState::Failed );

    parser.reset( HTTPMessageKind::Response, 64 );
    const string truncated = "HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\nhello";
    SW_EXPECT_TRUE( parser.append( reinterpret_cast<const uint8*>( truncated.data() ), truncated.size() ) == HTTPParseState::NeedMore );
    SW_EXPECT_TRUE( parser.finishOnClose() == HTTPParseState::Failed ); // 길이를 다 못 받고 닫혔다

    parser.reset( HTTPMessageKind::Request, 64 );
    const string request = "POST /echo?name=a%20b HTTP/1.1\r\nHost: x\r\nContent-Length: 2\r\n\r\nhi";
    SW_ASSERT_TRUE( parser.append( reinterpret_cast<const uint8*>( request.data() ), request.size() ) == HTTPParseState::Complete );
    SW_EXPECT_TRUE( parser.getMethod() == HTTPMethod::Post );
    SW_EXPECT_EQUAL( string( "/echo?name=a%20b" ), parser.getTarget() );
    parser.reset( HTTPMessageKind::Request, 64 );
    const string badMethod = "DELETE / HTTP/1.1\r\n\r\n";
    SW_EXPECT_TRUE( parser.append( reinterpret_cast<const uint8*>( badMethod.data() ), badMethod.size() ) == HTTPParseState::Failed );
}

SW_TEST_CASE( HTTPTest, LoopbackRequestsGetAnswersOverPlainAndTLS )
{
    INetSecurityProvider& provider = NetSecurity::getProvider();
    string                certificatePem;
    string                privateKeyPem;
    SW_ASSERT_TRUE( provider.createSelfSignedCertificate( "localhost", 1, certificatePem, privateKeyPem ) );
    TLSContextConfig serverTLS;
    serverTLS._role           = TLSRole::Server;
    serverTLS._certificatePem = certificatePem;
    serverTLS._privateKeyPem  = privateKeyPem;
    TLSContextConfig clientTLS;
    clientTLS._role       = TLSRole::Client;
    clientTLS._trustPem   = certificatePem;
    clientTLS._serverName = "localhost";
    string                  error;
    unique_ptr<ITLSContext> serverContext = provider.createTLSContext( serverTLS, error );
    unique_ptr<ITLSContext> clientContext = provider.createTLSContext( clientTLS, error );
    SW_ASSERT_TRUE( serverContext != nullptr && clientContext != nullptr );

    const bool arrSecure[] = { false, true };
    for ( const bool bSecure : arrSecure )
    {
        HTTPPair pair;
        SW_ASSERT_TRUE( pair.initialize( bSecure ? serverContext.get() : nullptr ) );
        if ( bSecure )
            pair._client.registerTLSContext( "localhost", clientContext.get() );
        const utf8* pScheme = bSecure ? "https" : "http";

        HTTPClientRequest get;
        get._url                          = pair.makeURL( pScheme, "/echo?name=r%C3%A9" );
        const HTTPClientResponse response = pair.run( get );
        SW_ASSERT_TRUE( response.isSuccess() );
        SW_EXPECT_EQUAL( string( "GET:ré:" ), string( response.getBodyText() ) );
        SW_ASSERT_TRUE( response.findHeader( "x-test" ) != nullptr );

        HTTPClientRequest post;
        post._method      = HTTPMethod::Post;
        post._url         = pair.makeURL( pScheme, "/echo" );
        const string body = "grant_type=authorization_code";
        post._bodyBytes.assign( reinterpret_cast<const uint8*>( body.data() ), reinterpret_cast<const uint8*>( body.data() ) + body.size() );
        const HTTPClientResponse postResponse = pair.run( post );
        SW_ASSERT_TRUE( postResponse.isSuccess() );
        SW_EXPECT_EQUAL( string( "POST::grant_type=authorization_code" ), string( postResponse.getBodyText() ) );

        HTTPClientRequest chunked;
        chunked._url                             = pair.makeURL( pScheme, "/chunked" );
        const HTTPClientResponse chunkedResponse = pair.run( chunked );
        SW_ASSERT_TRUE( chunkedResponse.isSuccess() );
        SW_EXPECT_EQUAL( string( "this body arrives in seven byte chunks" ), string( chunkedResponse.getBodyText() ) );

        HTTPClientRequest missing;
        missing._url                             = pair.makeURL( pScheme, "/nothing" );
        const HTTPClientResponse missingResponse = pair.run( missing );
        SW_EXPECT_FALSE( missingResponse.isSuccess() );
        SW_EXPECT_EQUAL( 404, missingResponse._statusCode );
        SW_EXPECT_TRUE( missingResponse._bTransportFailed == SW_FALSE );
    }
}

SW_TEST_CASE( HTTPTest, TransportFailuresAreReportedOnce )
{
    HTTPPair pair;
    SW_ASSERT_TRUE( pair.initialize( nullptr, 16 ) );

    HTTPClientRequest slow;
    slow._url                         = pair.makeURL( "http", "/echo" );
    slow._timeoutMs                   = 500;
    const HTTPClientResponse timedOut = pair.run( slow, false ); // 서버가 답하지 않는다
    SW_EXPECT_TRUE( timedOut._bTransportFailed == SW_TRUE );
    SW_EXPECT_EQUAL( string( "timed out" ), timedOut._failureText );

    HTTPClientRequest tooLarge;
    tooLarge._url                          = pair.makeURL( "http", "/chunked" ); // 몸이 상한 16 바이트를 넘는다
    const HTTPClientResponse largeResponse = pair.run( tooLarge );
    SW_EXPECT_TRUE( largeResponse._bTransportFailed == SW_TRUE );

    HTTPClientRequest named;
    named._url                             = "http://accounts.invalid/x"; // .invalid 는 풀리지 않는 이름(RFC 6761)
    const HTTPClientResponse namedResponse = pair.run( named );
    SW_EXPECT_TRUE( namedResponse._bTransportFailed == SW_TRUE ); // 풀리지 않거나(해석 실패) 해석기가 시한을 넘긴다 — 둘 다 분명한 전송 실패

    HTTPClientRequest noTLS;
    noTLS._url                             = pair.makeURL( "https", "/echo" ); // 그 호스트의 TLS 컨텍스트를 올리지 않았다
    const HTTPClientResponse noTLSResponse = pair.run( noTLS );
    SW_EXPECT_TRUE( noTLSResponse._bTransportFailed == SW_TRUE );

    HTTPClientRequest refused;
    refused._url                             = "http://127.0.0.1:1/";
    const HTTPClientResponse refusedResponse = pair.run( refused );
    SW_EXPECT_TRUE( refusedResponse._bTransportFailed == SW_TRUE );
    SW_EXPECT_EQUAL( 0, pair._client.getPendingCount() );
}

SW_TEST_CASE( HTTPTest, BlockingAdapterConvertsRequestsAndReturnsTransportFailures )
{
    HTTPRequest engineRequest;
    engineRequest._method = "post";
    engineRequest._url    = "http://127.0.0.1:1/upload";
    engineRequest._body   = "abc";
    engineRequest._listHeader.push_back( { "Content-Type", "application/json" } );
    HTTPClientRequest clientRequest;
    SW_ASSERT_TRUE( BlockingHTTPClient::makeClientRequest( engineRequest, clientRequest ) );
    SW_EXPECT_TRUE( clientRequest._method == HTTPMethod::Post );
    SW_EXPECT_EQUAL( size_t( 3 ), clientRequest._bodyBytes.size() );
    SW_ASSERT_EQUAL( size_t( 1 ), clientRequest._listHeader.size() );
    SW_EXPECT_EQUAL( string( "application/json" ), clientRequest._listHeader[0]._value );

    HTTPRequest putRequest = engineRequest;
    putRequest._method     = "PUT";
    SW_EXPECT_FALSE( BlockingHTTPClient::makeClientRequest( putRequest, clientRequest ) );

    BlockingHTTPClient notRunning;
    const HTTPResponse notRunningResponse = notRunning.send( engineRequest ); // 띄우지 않은 클라이언트도 응답은 한 번 온다
    SW_EXPECT_EQUAL( 0, notRunningResponse._status );
    SW_EXPECT_FALSE( notRunningResponse._error.empty() );
    const HTTPResponse putResponse = notRunning.send( putRequest );
    SW_EXPECT_EQUAL( 0, putResponse._status );

    LoopbackStreamNetwork network{ 11u };
    BlockingHTTPClient    adapter;
    StreamTransportConfig transportSettings;
    transportSettings._ioThreadCount = 0;
    SW_ASSERT_TRUE( adapter.initialize( network.createTransport(), transportSettings, HTTPClientConfig{} ) );
    const HTTPResponse refused = adapter.send( engineRequest ); // 듣는 쪽이 없는 포트
    SW_EXPECT_EQUAL( 0, refused._status );
    SW_EXPECT_FALSE( refused.isSuccess() );
    engineRequest._url              = "https://127.0.0.1:1/upload"; // 그 호스트의 TLS 컨텍스트가 없다
    const HTTPResponse noTLSContext = adapter.send( engineRequest );
    SW_EXPECT_EQUAL( 0, noTLSContext._status );
    adapter.shutdown();
}
