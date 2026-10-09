// 최소 HTTP — URL · 퍼센트 · 폼, 파서(Content-Length · chunked · 닫힐 때까지 · 상한), 루프백 클라이언트 ↔ 서버(평문 · TLS 1.3 자체 서명 신뢰),
// 404 · 쿼리 · POST 몸, 시한 · 이름 해석 없음 · TLS 컨텍스트 없음은 전송 실패.
#include "pch.h"

#include "Core/Network/NetTypes.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"

#include "Engine/Network/EngineNetSecurity.h"

#include "GameFramework/Base/Online/Http/HttpClient.h"
#include "GameFramework/Base/Online/Http/HttpServer.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 경로마다 정한 답 — `/echo` 는 쿼리 · 몸을 되돌리고 `/chunked` 는 chunked 로, 나머지는 404. */
    class EchoHandler final : public IHttpRequestHandler
    {
    public:
        void onHttpRequest( const HttpServerRequest& request, HttpServerResponse& outResponse ) override
        {
            if ( request._path == "/echo" )
            {
                const HttpHeader* pName = request.findQuery( "name" );
                string            text  = string( toString( request._method ) ) + ":" + ( pName != nullptr ? pName->_value : string() ) + ":";
                text += request.getBodyText();
                outResponse._bodyBytes.assign( text.begin(), text.end() );
                outResponse._listHeader.push_back( HttpHeader{ "X-Test", "yes" } );
                return;
            }
            if ( request._path == "/chunked" )
            {
                const string text = "this body arrives in seven byte chunks";
                outResponse._bodyBytes.assign( text.begin(), text.end() );
                outResponse._bChunked = SW_TRUE;
                return;
            }
            outResponse._statusCode = HttpConstant::kStatusNotFound;
        }
    };

    struct HttpPair
    {
        LoopbackStreamNetwork _network;
        EchoHandler           _handler;
        HttpServer            _server;
        HttpClient            _client;

        HttpPair()
            : _network{ 7u }
            , _handler{}
            , _server{}
            , _client{}
        {
        }

        bool initialize( ITlsContext* pServerContext, int32 maxBodyBytes = HttpConstant::kDefaultMaxBodyBytes )
        {
            StreamTransportSettings transportSettings;
            transportSettings._ioThreadCount = 0;
            HttpServerSettings serverSettings;
            serverSettings._pTlsContext = pServerContext;
            HttpClientSettings clientSettings;
            clientSettings._maxResponseBodyBytes = maxBodyBytes;
            return _server.initialize( _network.createTransport(), transportSettings, serverSettings, &_handler ) &&
                   _client.initialize( _network.createTransport(), transportSettings, clientSettings );
        }

        /** @brief 요청 하나를 끝까지 돌립니다. @p bServe 가 false 면 서버를 돌리지 않는다(시한 시험). */
        HttpClientResponse run( const HttpClientRequest& request, bool bServe = true )
        {
            int64        nowMs     = 0;
            const uint64 requestId = _client.submitRequest( request, nowMs );
            for ( int32 step = 0; step < 400; ++step )
            {
                if ( bServe )
                    _server.tick();
                _client.tick( nowMs );
                vector<HttpClientResponse> listResponse;
                (void)_client.pollResponses( listResponse );
                for ( HttpClientResponse& response : listResponse )
                {
                    if ( response._requestId == requestId )
                        return std::move( response );
                }
                nowMs += 100;
            }
            HttpClientResponse lost;
            lost._failureText = "test harness ran out of steps";
            return lost;
        }

        string makeUrl( const utf8* pScheme, const utf8* pPath ) const { return string( pScheme ) + "://localhost:" + to_string( static_cast<int32>( _server.getListenPort() ) ) + pPath; }
    };
} // namespace

SW_TEST_CASE( HttpTest, UrlFormAndPercentRules )
{
    HttpUrl url;
    SW_ASSERT_TRUE( HttpUrl::parse( "https://example.com:8443/a/b?x=1", url ) );
    SW_EXPECT_EQUAL( string( "example.com" ), url._host );
    SW_EXPECT_EQUAL( uint16( 8443 ), url._port );
    SW_EXPECT_EQUAL( string( "/a/b?x=1" ), url._target );
    SW_EXPECT_TRUE( url._bSecure == SW_TRUE );
    SW_ASSERT_TRUE( HttpUrl::parse( "http://127.0.0.1?q", url ) );
    SW_EXPECT_EQUAL( uint16( 80 ), url._port );
    SW_EXPECT_EQUAL( string( "/?q" ), url._target );
    SW_EXPECT_FALSE( HttpUrl::parse( "ftp://example.com/", url ) );
    SW_EXPECT_FALSE( HttpUrl::parse( "https://user@example.com/", url ) );
    SW_EXPECT_FALSE( HttpUrl::parse( "https://example.com:0/", url ) );
    SW_EXPECT_FALSE( HttpUrl::parse( "https://example.com/#frag", url ) );

    const vector<HttpHeader> listPair = {
        {"redirect_uri", "http://127.0.0.1:5/callback"},
        {       "scope",              "openid profile"},
        {          "한",                          "글"}
    };
    const string form = HttpUtil::encodeForm( listPair );
    SW_EXPECT_EQUAL( string( "redirect_uri=http%3A%2F%2F127.0.0.1%3A5%2Fcallback&scope=openid%20profile&%ED%95%9C=%EA%B8%80" ), form );
    vector<HttpHeader> decoded;
    SW_ASSERT_TRUE( HttpUtil::decodeForm( form, decoded ) );
    SW_ASSERT_EQUAL( size_t( 3 ), decoded.size() );
    SW_EXPECT_EQUAL( string( "openid profile" ), decoded[1]._value );
    SW_EXPECT_EQUAL( string( "글" ), decoded[2]._value );
    SW_ASSERT_TRUE( HttpUtil::decodeForm( "a=1+2", decoded ) );
    SW_EXPECT_EQUAL( string( "1 2" ), decoded[0]._value );
    SW_EXPECT_FALSE( HttpUtil::decodeForm( "a=%G1", decoded ) );
}

SW_TEST_CASE( HttpTest, ParserHandlesLengthChunkedCloseAndLimits )
{
    HttpMessageParser parser;
    parser.reset( HttpMessageKind::Response, 64 );
    const string fixed = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nX-A:  b \r\n\r\nhello";
    // 한 바이트씩 넣어도 같은 결과
    HttpParseState state = HttpParseState::NeedMore;
    for ( const utf8 ch : fixed )
    {
        state = parser.append( reinterpret_cast<const uint8*>( &ch ), 1 );
    }
    SW_ASSERT_TRUE( state == HttpParseState::Complete );
    SW_EXPECT_EQUAL( 200, parser.getStatusCode() );
    SW_EXPECT_EQUAL( size_t( 5 ), parser.getBody().size() );
    SW_EXPECT_EQUAL( string( "b" ), HttpUtil::findHeader( parser.getHeaders(), "x-a" )->_value );

    parser.reset( HttpMessageKind::Response, 64 );
    const string chunked = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3;ext=1\r\nabc\r\n2\r\nde\r\n0\r\nX-Trailer: 1\r\n\r\n";
    SW_ASSERT_TRUE( parser.append( reinterpret_cast<const uint8*>( chunked.data() ), chunked.size() ) == HttpParseState::Complete );
    SW_EXPECT_EQUAL( string( "abcde" ), string( reinterpret_cast<const utf8*>( parser.getBody().data() ), parser.getBody().size() ) );

    parser.reset( HttpMessageKind::Response, 64 );
    const string untilClose = "HTTP/1.0 200 OK\r\n\r\nrest of body";
    SW_EXPECT_TRUE( parser.append( reinterpret_cast<const uint8*>( untilClose.data() ), untilClose.size() ) == HttpParseState::NeedMore );
    SW_EXPECT_TRUE( parser.finishOnClose() == HttpParseState::Complete );
    SW_EXPECT_EQUAL( size_t( 12 ), parser.getBody().size() );

    parser.reset( HttpMessageKind::Response, 4 );
    const string tooLarge = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
    SW_EXPECT_TRUE( parser.append( reinterpret_cast<const uint8*>( tooLarge.data() ), tooLarge.size() ) == HttpParseState::Failed );

    parser.reset( HttpMessageKind::Response, 64 );
    const string truncated = "HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\nhello";
    SW_EXPECT_TRUE( parser.append( reinterpret_cast<const uint8*>( truncated.data() ), truncated.size() ) == HttpParseState::NeedMore );
    SW_EXPECT_TRUE( parser.finishOnClose() == HttpParseState::Failed ); // 길이를 다 못 받고 닫혔다

    parser.reset( HttpMessageKind::Request, 64 );
    const string request = "POST /echo?name=a%20b HTTP/1.1\r\nHost: x\r\nContent-Length: 2\r\n\r\nhi";
    SW_ASSERT_TRUE( parser.append( reinterpret_cast<const uint8*>( request.data() ), request.size() ) == HttpParseState::Complete );
    SW_EXPECT_TRUE( parser.getMethod() == HttpMethod::Post );
    SW_EXPECT_EQUAL( string( "/echo?name=a%20b" ), parser.getTarget() );
    parser.reset( HttpMessageKind::Request, 64 );
    const string badMethod = "DELETE / HTTP/1.1\r\n\r\n";
    SW_EXPECT_TRUE( parser.append( reinterpret_cast<const uint8*>( badMethod.data() ), badMethod.size() ) == HttpParseState::Failed );
}

SW_TEST_CASE( HttpTest, LoopbackRequestsGetAnswersOverPlainAndTls )
{
    INetSecurityProvider& provider = EngineNetSecurity::getProvider();
    string                certificatePem;
    string                privateKeyPem;
    SW_ASSERT_TRUE( provider.createSelfSignedCertificate( "localhost", 1, certificatePem, privateKeyPem ) );
    TlsContextSettings serverTls;
    serverTls._role           = TlsRole::Server;
    serverTls._certificatePem = certificatePem;
    serverTls._privateKeyPem  = privateKeyPem;
    TlsContextSettings clientTls;
    clientTls._role       = TlsRole::Client;
    clientTls._trustPem   = certificatePem;
    clientTls._serverName = "localhost";
    string                  error;
    unique_ptr<ITlsContext> serverContext = provider.createTlsContext( serverTls, error );
    unique_ptr<ITlsContext> clientContext = provider.createTlsContext( clientTls, error );
    SW_ASSERT_TRUE( serverContext != nullptr && clientContext != nullptr );

    const bool arrSecure[] = { false, true };
    for ( const bool bSecure : arrSecure )
    {
        HttpPair pair;
        SW_ASSERT_TRUE( pair.initialize( bSecure ? serverContext.get() : nullptr ) );
        if ( bSecure )
            pair._client.registerTlsContext( "localhost", clientContext.get() );
        const utf8* pScheme = bSecure ? "https" : "http";

        HttpClientRequest get;
        get._url                          = pair.makeUrl( pScheme, "/echo?name=r%C3%A9" );
        const HttpClientResponse response = pair.run( get );
        SW_ASSERT_TRUE( response.isSuccess() );
        SW_EXPECT_EQUAL( string( "GET:ré:" ), string( response.getBodyText() ) );
        SW_ASSERT_TRUE( response.findHeader( "x-test" ) != nullptr );

        HttpClientRequest post;
        post._method      = HttpMethod::Post;
        post._url         = pair.makeUrl( pScheme, "/echo" );
        const string body = "grant_type=authorization_code";
        post._bodyBytes.assign( body.begin(), body.end() );
        const HttpClientResponse postResponse = pair.run( post );
        SW_ASSERT_TRUE( postResponse.isSuccess() );
        SW_EXPECT_EQUAL( string( "POST::grant_type=authorization_code" ), string( postResponse.getBodyText() ) );

        HttpClientRequest chunked;
        chunked._url                             = pair.makeUrl( pScheme, "/chunked" );
        const HttpClientResponse chunkedResponse = pair.run( chunked );
        SW_ASSERT_TRUE( chunkedResponse.isSuccess() );
        SW_EXPECT_EQUAL( string( "this body arrives in seven byte chunks" ), string( chunkedResponse.getBodyText() ) );

        HttpClientRequest missing;
        missing._url                             = pair.makeUrl( pScheme, "/nothing" );
        const HttpClientResponse missingResponse = pair.run( missing );
        SW_EXPECT_FALSE( missingResponse.isSuccess() );
        SW_EXPECT_EQUAL( 404, missingResponse._statusCode );
        SW_EXPECT_TRUE( missingResponse._bTransportFailed == SW_FALSE );
    }
}

SW_TEST_CASE( HttpTest, TransportFailuresAreReportedOnce )
{
    HttpPair pair;
    SW_ASSERT_TRUE( pair.initialize( nullptr, 16 ) );

    HttpClientRequest slow;
    slow._url                         = pair.makeUrl( "http", "/echo" );
    slow._timeoutMs                   = 500;
    const HttpClientResponse timedOut = pair.run( slow, false ); // 서버가 답하지 않는다
    SW_EXPECT_TRUE( timedOut._bTransportFailed == SW_TRUE );
    SW_EXPECT_EQUAL( string( "timed out" ), timedOut._failureText );

    HttpClientRequest tooLarge;
    tooLarge._url                          = pair.makeUrl( "http", "/chunked" ); // 몸이 상한 16 바이트를 넘는다
    const HttpClientResponse largeResponse = pair.run( tooLarge );
    SW_EXPECT_TRUE( largeResponse._bTransportFailed == SW_TRUE );

    HttpClientRequest named;
    named._url                             = "http://accounts.example.com/x";
    const HttpClientResponse namedResponse = pair.run( named );
    SW_EXPECT_TRUE( namedResponse._bTransportFailed == SW_TRUE ); // 이름 해석 없음 — 분명한 실패

    HttpClientRequest noTls;
    noTls._url                             = pair.makeUrl( "https", "/echo" ); // 그 호스트의 TLS 컨텍스트를 올리지 않았다
    const HttpClientResponse noTlsResponse = pair.run( noTls );
    SW_EXPECT_TRUE( noTlsResponse._bTransportFailed == SW_TRUE );

    HttpClientRequest refused;
    refused._url                             = "http://127.0.0.1:1/";
    const HttpClientResponse refusedResponse = pair.run( refused );
    SW_EXPECT_TRUE( refusedResponse._bTransportFailed == SW_TRUE );
    SW_EXPECT_EQUAL( 0, pair._client.getPendingCount() );
}
