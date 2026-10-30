#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Base/Online/HTTP/HTTPTypes.h"

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
    SW_REQUIRE_TRUE( sw::HTTPUtil::findHeader( parser.getHeaders(), "host" ) != nullptr );
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
