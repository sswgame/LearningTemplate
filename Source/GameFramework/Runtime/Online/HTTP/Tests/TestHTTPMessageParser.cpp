#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "GameFramework/Runtime/Online/HTTP/HTTPTypes.h"

// HTTPMessageParserTest — 신뢰 경계: 받은 바이트를 요청 · 응답으로 읽는 증분 파서. 쪼개 넣기, chunked, 상한, 길이 머리의 모순을 본다(nogpu).

namespace
{
    sw::HTTPParseState feed( sw::HTTPMessageParser& parser, sw::string_view text )
    {
        return parser.append( reinterpret_cast<const uint8*>( text.data() ), text.size() );
    }

    /** @brief 한 바이트씩 넣습니다 — 경계에서 끊긴 머리 · 청크를 이어 읽는지 본다. */
    sw::HTTPParseState feedByteByByte( sw::HTTPMessageParser& parser, sw::string_view text )
    {
        sw::HTTPParseState state = sw::HTTPParseState::NeedMore;
        for ( const utf8 ch : text )
        {
            state = parser.append( reinterpret_cast<const uint8*>( &ch ), 1 );
        }
        return state;
    }

    sw::string bodyText( sw::HTTPMessageParser& parser )
    {
        return sw::string( reinterpret_cast<const utf8*>( parser.getBody().data() ), parser.getBody().size() );
    }
} // namespace

/**
 * @brief [HTTPMessageParserTest] 한 바이트씩 들어온 요청을 끝까지 읽는다 — 방법 · 대상 · 머리 · 몸
 */
SW_TEST_CASE( HTTPMessageParserTest, ReadsARequestFedByteByByte )
{
    sw::HTTPMessageParser parser;
    parser.reset( sw::HTTPMessageKind::Request, 1024 );
    SW_REQUIRE_TRUE( feedByteByByte( parser, "POST /token?x=1 HTTP/1.1\r\nHost: a.test\r\nContent-Length: 5\r\n\r\nhello" ) == sw::HTTPParseState::Complete );
    SW_EXPECT_TRUE( parser.getMethod() == sw::HTTPMethod::Post );
    SW_EXPECT_EQUAL( sw::string( "/token?x=1" ), parser.getTarget() );
    SW_REQUIRE_TRUE( sw::HTTPHeader::find( parser.getHeaders(), "host" ) != nullptr );
    SW_EXPECT_EQUAL( sw::string( "hello" ), bodyText( parser ) );
}

/**
 * @brief [HTTPMessageParserTest] chunked 응답을 이어 붙이고, 길이가 없는 응답은 연결이 닫힐 때 끝난다
 */
SW_TEST_CASE( HTTPMessageParserTest, ReadsChunkedAndUntilCloseResponses )
{
    sw::HTTPMessageParser chunked;
    chunked.reset( sw::HTTPMessageKind::Response, 1024 );
    SW_REQUIRE_TRUE( feedByteByByte( chunked, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWiki\r\n5;ext=1\r\npedia\r\n0\r\n\r\n" ) ==
                     sw::HTTPParseState::Complete );
    SW_EXPECT_EQUAL( 200, chunked.getStatusCode() );
    SW_EXPECT_EQUAL( sw::string( "Wikipedia" ), bodyText( chunked ) );

    sw::HTTPMessageParser untilClose;
    untilClose.reset( sw::HTTPMessageKind::Response, 1024 );
    SW_EXPECT_TRUE( feed( untilClose, "HTTP/1.1 200 OK\r\n\r\nstream" ) == sw::HTTPParseState::NeedMore );
    SW_EXPECT_TRUE( untilClose.finishOnClose() == sw::HTTPParseState::Complete );
    SW_EXPECT_EQUAL( sw::string( "stream" ), bodyText( untilClose ) );
}

/**
 * @brief [HTTPMessageParserTest] 상한을 넘는 몸 · 머리는 거절한다 — 머리가 한 번에 다 들어와도 같다
 */
SW_TEST_CASE( HTTPMessageParserTest, RefusesOversizedMessages )
{
    sw::HTTPMessageParser bigBody;
    bigBody.reset( sw::HTTPMessageKind::Request, 4 );
    SW_EXPECT_TRUE( feed( bigBody, "POST / HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello" ) == sw::HTTPParseState::Failed );

    sw::HTTPMessageParser bigChunks;
    bigChunks.reset( sw::HTTPMessageKind::Response, 4 );
    SW_EXPECT_TRUE( feed( bigChunks, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n3\r\ndef\r\n0\r\n\r\n" ) == sw::HTTPParseState::Failed );

    sw::string hugeHead = "GET / HTTP/1.1\r\nX-Fill: ";
    hugeHead.append( static_cast<size_t>( sw::HTTPConstant::kMaxHeaderBytes ), 'a' );
    hugeHead += "\r\n\r\n";
    sw::HTTPMessageParser oneShot;
    oneShot.reset( sw::HTTPMessageKind::Request, 1024 );
    SW_EXPECT_TRUE( feed( oneShot, hugeHead ) == sw::HTTPParseState::Failed );
}

/**
 * @brief [HTTPMessageParserTest] 몸의 길이를 둘로 읽을 수 있는 머리는 거절한다 — 서로 다른 Content-Length 둘, 모르는 전송 부호화, 틀린 길이 · 청크 크기
 * @details 앞단과 이 파서가 다른 길이를 고르면 뒤 요청이 몸에 숨어 들어온다(요청 밀반입, RFC 9112 6.3).
 */
SW_TEST_CASE( HTTPMessageParserTest, RefusesAmbiguousLengths )
{
    sw::HTTPMessageParser twoLengths;
    twoLengths.reset( sw::HTTPMessageKind::Request, 1024 );
    SW_EXPECT_TRUE( feed( twoLengths, "POST / HTTP/1.1\r\nContent-Length: 5\r\nContent-Length: 10\r\n\r\nhello" ) == sw::HTTPParseState::Failed );

    sw::HTTPMessageParser gzip;
    gzip.reset( sw::HTTPMessageKind::Request, 1024 );
    SW_EXPECT_TRUE( feed( gzip, "POST / HTTP/1.1\r\nTransfer-Encoding: gzip\r\n\r\n" ) == sw::HTTPParseState::Failed );

    sw::HTTPMessageParser negative;
    negative.reset( sw::HTTPMessageKind::Request, 1024 );
    SW_EXPECT_TRUE( feed( negative, "POST / HTTP/1.1\r\nContent-Length: -1\r\n\r\n" ) == sw::HTTPParseState::Failed );

    sw::HTTPMessageParser badChunk;
    badChunk.reset( sw::HTTPMessageKind::Response, 1024 );
    SW_EXPECT_TRUE( feed( badChunk, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n" ) == sw::HTTPParseState::Failed );

    sw::HTTPMessageParser sameTwice; // 같은 값이 두 번이면 모순이 아니다
    sameTwice.reset( sw::HTTPMessageKind::Request, 1024 );
    SW_EXPECT_TRUE( feed( sameTwice, "POST / HTTP/1.1\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\nok" ) == sw::HTTPParseState::Complete );
}

/**
 * @brief [HTTPMessageParserTest] IPv6 URL 은 대괄호 없이 호스트가 되고(TLS 이름 · 주소 풀이), 머리에는 다시 대괄호로 적으며 기본 포트는 뺀다. 대괄호가 틀리면 거절한다
 */
SW_TEST_CASE( HTTPMessageParserTest, Ipv6Addresses )
{
    sw::HTTPAddress address;
    SW_REQUIRE_TRUE( sw::HTTPAddress::parse( "https://[2001:db8::1]/v1", address ) );
    SW_EXPECT_EQUAL( sw::string( "2001:db8::1" ), address._host );
    SW_EXPECT_EQUAL( uint16( 443 ), address._port );
    SW_EXPECT_EQUAL( sw::string( "[2001:db8::1]" ), address.makeAuthority() );
    SW_REQUIRE_TRUE( sw::HTTPAddress::parse( "http://[::1]:8080?x=1", address ) );
    SW_EXPECT_EQUAL( sw::string( "::1" ), address._host );
    SW_EXPECT_EQUAL( sw::string( "/?x=1" ), address._target );
    SW_EXPECT_EQUAL( sw::string( "[::1]:8080" ), address.makeAuthority() );
    SW_REQUIRE_TRUE( sw::HTTPAddress::parse( "http://api.test", address ) );
    SW_EXPECT_EQUAL( sw::string( "api.test" ), address.makeAuthority() );
    SW_EXPECT_FALSE( sw::HTTPAddress::parse( "https://2001:db8::1/", address ) );
    SW_EXPECT_FALSE( sw::HTTPAddress::parse( "https://[2001:db8::1/", address ) );
    SW_EXPECT_FALSE( sw::HTTPAddress::parse( "https://[api.test]/", address ) );
    SW_EXPECT_FALSE( sw::HTTPAddress::parse( "https://[::1]8443/", address ) );
    SW_EXPECT_FALSE( sw::HTTPAddress::parse( "https://[::1]:/", address ) );
    SW_EXPECT_FALSE( sw::HTTPAddress::parse( "https://a]b/", address ) );
}

/**
 * @brief [HTTPMessageParserTest] URL 나누기는 사용자 정보 · 조각 · 빈 호스트 · 틀린 포트를 거절하고, 퍼센트 풀기는 틀린 `%` 를 거절한다
 */
SW_TEST_CASE( HTTPMessageParserTest, AddressAndPercentRules )
{
    sw::HTTPAddress address;
    SW_REQUIRE_TRUE( sw::HTTPAddress::parse( "https://api.test:8443/v1/x?y=2", address ) );
    SW_EXPECT_EQUAL( sw::string( "api.test" ), address._host );
    SW_EXPECT_EQUAL( uint16( 8443 ), address._port );
    SW_EXPECT_EQUAL( sw::string( "/v1/x?y=2" ), address._target );
    SW_EXPECT_TRUE( address._bSecure == SW_TRUE );
    SW_EXPECT_FALSE( sw::HTTPAddress::parse( "https://user@api.test/", address ) );
    SW_EXPECT_FALSE( sw::HTTPAddress::parse( "https://api.test/#frag", address ) );
    SW_EXPECT_FALSE( sw::HTTPAddress::parse( "https:///path", address ) );
    SW_EXPECT_FALSE( sw::HTTPAddress::parse( "http://api.test:99999/", address ) );

    sw::string decoded;
    SW_EXPECT_TRUE( sw::HTTPUtil::decodePercent( "a%20b+c", true, decoded ) );
    SW_EXPECT_EQUAL( sw::string( "a b c" ), decoded );
    SW_EXPECT_FALSE( sw::HTTPUtil::decodePercent( "bad%2", false, decoded ) );
    SW_EXPECT_FALSE( sw::HTTPUtil::decodePercent( "bad%zz", false, decoded ) );
    SW_EXPECT_EQUAL( sw::string( "a%20b%2Fc" ), sw::HTTPUtil::encodePercent( "a b/c" ) );
}

/** @brief [HTTPClientResponseTest] 갈래: 상태 코드마다 한 갈래이고, 다시 보내면 될 수 있는 것은 전송 실패 · 429 · 5xx 뿐이다 */
SW_TEST_CASE( HTTPClientResponseTest, KindOfStatus )
{
    sw::HTTPClientResponse response;
    response.setTransportFailed( "down" );
    SW_EXPECT_TRUE( response.getKind() == sw::HTTPResponseKind::TransportFailed );
    SW_EXPECT_TRUE( response.isTransientFailure() );
    SW_EXPECT_FALSE( response.isSuccess() );

    const auto check = [&]( int32 status, sw::HTTPResponseKind kind, bool bTransient )
    {
        response.setStatusCode( status );
        SW_EXPECT_TRUE( response.getKind() == kind );
        SW_EXPECT_TRUE( response.isTransientFailure() == bTransient );
        SW_EXPECT_TRUE( response.isSuccess() == ( kind == sw::HTTPResponseKind::Success ) );
    };
    check( 200, sw::HTTPResponseKind::Success, false );
    check( 204, sw::HTTPResponseKind::Success, false );
    check( 302, sw::HTTPResponseKind::Refused, false );
    check( 400, sw::HTTPResponseKind::Refused, false );
    check( 401, sw::HTTPResponseKind::Denied, false );
    check( 403, sw::HTTPResponseKind::Denied, false );
    check( 404, sw::HTTPResponseKind::NotFound, false );
    check( 410, sw::HTTPResponseKind::Gone, false );
    check( 429, sw::HTTPResponseKind::RateLimited, true );
    check( 500, sw::HTTPResponseKind::ServerError, true );
    check( 503, sw::HTTPResponseKind::ServerError, true );
}

/** @brief [HTTPClientResponseTest] `Retry-After` 는 초와 HTTP 날짜 두 꼴을 읽고, 지난 날짜 · 틀린 글 · 음수는 0 이며 상한에서 자른다 */
SW_TEST_CASE( HTTPClientResponseTest, RetryAfterReadsSecondsAndDate )
{
    constexpr int64 kDateMs = 784111777000ll; // RFC 9110 의 본보기 날짜
    const auto      read    = [&]( sw::string_view value, int64 nowMs )
    {
        sw::HTTPClientResponse response;
        response.setStatusCode( 503 );
        response._listHeader.push_back( sw::HTTPHeader{ "retry-after", sw::string( value ) } );
        return response.getRetryAfterMs( sw::UnixMilliseconds{ nowMs } );
    };
    SW_EXPECT_EQUAL( int64( 120000 ), read( "120", kDateMs ) );
    SW_EXPECT_EQUAL( int64( 5000 ), read( "Sun, 06 Nov 1994 08:49:37 GMT", kDateMs - 5000 ) );
    SW_EXPECT_EQUAL( int64( 0 ), read( "Sun, 06 Nov 1994 08:49:37 GMT", kDateMs + 1 ) );
    SW_EXPECT_EQUAL( int64( 0 ), read( "-3", 0 ) );
    SW_EXPECT_EQUAL( int64( 0 ), read( "soon", 0 ) );
    SW_EXPECT_EQUAL( sw::HTTPConstant::kMaxRetryAfterSeconds * 1000, read( "9223372036854775807", 0 ) );
    SW_EXPECT_EQUAL( sw::HTTPConstant::kMaxRetryAfterSeconds * 1000, read( "Sun, 06 Nov 1994 08:49:37 GMT", 0 ) );
    SW_EXPECT_EQUAL( int64( 0 ), sw::HTTPClientResponse{}.getRetryAfterMs( sw::UnixMilliseconds{ 0 } ) );

    const sw::optional<int64> date = sw::HTTPClientResponse::parseDate( "Sun, 06 Nov 1994 08:49:37 GMT" );
    SW_REQUIRE_TRUE( date.has_value() );
    SW_EXPECT_EQUAL( kDateMs, *date );
    for ( const utf8* pText : { "", "Sun, 06 Xxx 1994 08:49:37 GMT", "Sun, 06 anF 1994 08:49:37 GMT", "Sun, 31 Feb 1994 08:49:37 GMT", "Sun, 06 Nov 1994 24:00:00 GMT", "Sun, 06 Nov 1994 08:49:37 KST", "Sunday, 06-Nov-94 08:49:37 GMT" } )
        SW_EXPECT_FALSE( sw::HTTPClientResponse::parseDate( pText ).has_value() );
}
