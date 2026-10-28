// HTTP/2 클라이언트 — 루프백 위의 TLS(ALPN h2) 가짜 HTTP/2 서버로 POST 왕복과 연결 재사용, 상태 코드 전달, h2 를 협상하지 않는 서버의 거절,
// https 가 아닌 주소 · 등록 없는 호스트의 즉시 실패, 답하지 않는 서버의 시한을 본다. 서버는 손으로 만든 프레임 · HPACK 이다(`FakeHTTP2Fixture.h`, 스위트는 HTTP2ClientTest).
#include "pch.h"

#include "EngineTest/GameFramework/Online/FakeHTTP2Fixture.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using test::FakeHTTP2Fixture;
using test::SeenRequest;

/**
 * @brief [HTTP2ClientTest] ALPN h2 를 협상한 연결로 POST 가 왕복하고(머리 · 본문이 서버에 닿고 응답 머리 · 본문이 돌아온다), 둘째 요청은 같은 연결을 쓴다
 */
SW_TEST_CASE( HTTP2ClientTest, PostsOverAlpnAndReusesTheConnection )
{
    FakeHTTP2Fixture fixture( "h2", "h2" );
    SW_ASSERT_TRUE( fixture._bReady );
    const uint64 first = fixture._client.submitRequest( fixture.makePost( "/3/device/abc", "{\"aps\":{}}", 10000 ) );
    SW_ASSERT_TRUE( first != 0 );
    vector<HTTPClientResponse> listResponse = fixture.collect( 1 );
    SW_ASSERT_EQUAL( size_t( 1 ), listResponse.size() );
    SW_EXPECT_EQUAL( first, listResponse[0]._requestID );
    SW_EXPECT_TRUE( listResponse[0].isSuccess() );
    SW_EXPECT_TRUE( listResponse[0].getBodyText() == "{\"reason\":\"ok\"}" );
    const HTTPHeader* pApnsID = listResponse[0].findHeader( "apns-id" );
    SW_ASSERT_NOT_NULL( pApnsID );
    SW_EXPECT_TRUE( pApnsID->_value == "fake-id" );
    SW_ASSERT_EQUAL( size_t( 1 ), fixture._server._listSeen.size() );
    const SeenRequest& seen = fixture._server._listSeen[0];
    SW_EXPECT_TRUE( seen.valueOf( ":method" ) == "POST" && seen.valueOf( ":path" ) == "/3/device/abc" );
    SW_EXPECT_TRUE( seen.valueOf( "apns-topic" ) == "com.example.app" );
    SW_EXPECT_TRUE( seen.getBodyText() == "{\"aps\":{}}" );

    const uint64 second = fixture._client.submitRequest( fixture.makePost( "/3/device/def", "{}", 10000 ) );
    listResponse.clear();
    listResponse = fixture.collect( 1 );
    SW_ASSERT_EQUAL( size_t( 1 ), listResponse.size() );
    SW_EXPECT_EQUAL( second, listResponse[0]._requestID );
    SW_EXPECT_EQUAL( int32( 1 ), fixture._server._acceptedCount ); // 연결은 하나
    SW_EXPECT_EQUAL( int32( 1 ), fixture._client.getSessionCount() );
    SW_EXPECT_EQUAL( int32( 0 ), fixture._client.getPendingCount() );
}

/**
 * @brief [HTTP2ClientTest] 상태 코드는 그대로 돌아온다(410) — 제공자가 그 코드로 토큰을 지운다
 */
SW_TEST_CASE( HTTP2ClientTest, ReturnsErrorStatusFromTheServer )
{
    FakeHTTP2Fixture fixture( "h2", "h2" );
    SW_ASSERT_TRUE( fixture._bReady );
    fixture._server._statusCode = 410;
    (void)fixture._client.submitRequest( fixture.makePost( "/3/device/gone", "{}", 10000 ) ); // 응답은 아래에서 모아 요청 id 를 따로 쓰지 않는다
    const vector<HTTPClientResponse> listResponse = fixture.collect( 1 );
    SW_ASSERT_EQUAL( size_t( 1 ), listResponse.size() );
    SW_EXPECT_TRUE( listResponse[0]._bTransportFailed == SW_FALSE );
    SW_EXPECT_EQUAL( int32( 410 ), listResponse[0]._statusCode );
}

/**
 * @brief [HTTP2ClientTest] h2 를 협상하지 않는 서버(ALPN 없음)는 전송 실패로 끝난다 — HTTP/1.1 로 조용히 내려가지 않는다
 */
SW_TEST_CASE( HTTP2ClientTest, FailsWhenTheServerDoesNotNegotiateH2 )
{
    FakeHTTP2Fixture fixture( "", "h2" );
    SW_ASSERT_TRUE( fixture._bReady );
    (void)fixture._client.submitRequest( fixture.makePost( "/3/device/abc", "{}", 10000 ) ); // 응답은 아래에서 모아 요청 id 를 따로 쓰지 않는다
    const vector<HTTPClientResponse> listResponse = fixture.collect( 1 );
    SW_ASSERT_EQUAL( size_t( 1 ), listResponse.size() );
    SW_EXPECT_TRUE( listResponse[0]._bTransportFailed == SW_TRUE );
    SW_EXPECT_TRUE( listResponse[0]._failureText.find( "h2" ) != string::npos );
    SW_EXPECT_TRUE( fixture._server._listSeen.empty() );
}

/**
 * @brief [HTTP2ClientTest] https 가 아닌 주소와 TLS 컨텍스트를 등록하지 않은 호스트는 연결 없이 바로 전송 실패로 끝난다
 */
SW_TEST_CASE( HTTP2ClientTest, RefusesPlainHTTPAndUnregisteredHosts )
{
    FakeHTTP2Fixture fixture( "h2", "h2" );
    SW_ASSERT_TRUE( fixture._bReady );
    HTTPClientRequest plain;
    plain._url = "http://127.0.0.1:" + to_string( static_cast<int32>( fixture._server.getPort() ) ) + "/";
    HTTPClientRequest unknown;
    unknown._url = "https://192.0.2.1:443/";
    (void)fixture._client.submitRequest( plain );   // 응답은 아래에서 모아 요청 id 를 따로 쓰지 않는다
    (void)fixture._client.submitRequest( unknown ); // 응답은 아래에서 모아 요청 id 를 따로 쓰지 않는다
    vector<HTTPClientResponse> listResponse;
    SW_ASSERT_EQUAL( int32( 2 ), fixture._client.pollResponses( listResponse ) );
    SW_EXPECT_TRUE( listResponse[0]._bTransportFailed == SW_TRUE && listResponse[1]._bTransportFailed == SW_TRUE );
    SW_EXPECT_EQUAL( int32( 0 ), fixture._server._acceptedCount );
}

/**
 * @brief [HTTP2ClientTest] 답하지 않는 서버는 시한에 전송 실패(timed out)로 끝나고, 연결은 남아 다음 요청이 쓴다
 */
SW_TEST_CASE( HTTP2ClientTest, TimesOutASilentServerAndKeepsTheConnection )
{
    FakeHTTP2Fixture fixture( "h2", "h2" );
    SW_ASSERT_TRUE( fixture._bReady );
    fixture._server._bSilent = SW_TRUE;
    (void)fixture._client.submitRequest( fixture.makePost( "/3/device/abc", "{}", 100 ) ); // 응답은 아래에서 모아 요청 id 를 따로 쓰지 않는다
    vector<HTTPClientResponse> listResponse = fixture.collect( 1 );
    SW_ASSERT_EQUAL( size_t( 1 ), listResponse.size() );
    SW_EXPECT_TRUE( listResponse[0]._bTransportFailed == SW_TRUE );
    SW_EXPECT_TRUE( listResponse[0]._failureText.find( "timed out" ) != string::npos );
    SW_EXPECT_EQUAL( int32( 0 ), fixture._client.getPendingCount() );

    fixture._server._bSilent = SW_FALSE;
    (void)fixture._client.submitRequest( fixture.makePost( "/3/device/abc", "{}", 10000 ) ); // 응답은 아래에서 모아 요청 id 를 따로 쓰지 않는다
    listResponse = fixture.collect( 1 );
    SW_ASSERT_EQUAL( size_t( 1 ), listResponse.size() );
    SW_EXPECT_TRUE( listResponse[0].isSuccess() );
    SW_EXPECT_EQUAL( int32( 1 ), fixture._server._acceptedCount );
}
