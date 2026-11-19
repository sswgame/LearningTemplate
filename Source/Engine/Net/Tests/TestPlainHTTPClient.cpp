// PlainHTTPClientTest — 개발 전용 평문 HTTP GET 클라이언트(Fileserve 장치 전송)가 믿지 않는 응답 바이트를 푸는 규칙: 상태 줄 · 헤더 문법, Content-Length · chunked ·
// 닫힘까지 본문, 머리말 · 본문 · 원시 바이트 상한, 끝나기 전 닫힘, 그리고 URL 나누기(요청 줄에 CR · LF · 공백이 들어가지 않는다). 소켓 없이 바이트 열로 본다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Net/Dev/PlainHTTPClient.h"

using namespace sw;

namespace
{
    constexpr uint32 kHeaderLimit = 256;
    constexpr uint32 kBodyLimit   = 64;

    HTTPResponseParseStatus parse( string_view bytes, bool bStreamEnded, HTTPResponse& outResponse )
    {
        return PlainHTTPClient::parseResponse( bytes, bStreamEnded, kHeaderLimit, kBodyLimit, outResponse );
    }

    HTTPResponseParseStatus parse( string_view bytes, bool bStreamEnded = false )
    {
        HTTPResponse response;
        return parse( bytes, bStreamEnded, response );
    }

    string bodyText( const HTTPResponse& response )
    {
        return string( reinterpret_cast<const char*>( response._body.data() ), response._body.size() );
    }
} // namespace

SW_TEST_CASE( PlainHTTPClientTest, ContentLengthBodyCompletesOnlyWhenWhole )
{
    const string whole = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Type: text/plain\r\n\r\nhello";
    HTTPResponse response;
    SW_REQUIRE_TRUE( parse( whole, false, response ) == HTTPResponseParseStatus::Complete );
    SW_REQUIRE_EQUAL( 200, response._status );
    SW_REQUIRE_TRUE( bodyText( response ) == "hello" );

    const string_view cut = string_view{ whole }.substr( 0, whole.size() - 2 );
    SW_REQUIRE_TRUE( parse( cut, false, response ) == HTTPResponseParseStatus::Incomplete );
    SW_REQUIRE_TRUE( parse( cut, true, response ) == HTTPResponseParseStatus::Rejected ); // 끝나기 전 닫힘
    SW_REQUIRE_EQUAL( 0, response._status );
    SW_REQUIRE_TRUE( response._error.empty() == false );

    SW_REQUIRE_TRUE( parse( "HTTP/1.1 200 OK\r\nContent-Le", false ) == HTTPResponseParseStatus::Incomplete ); // 머리말 중간
    SW_REQUIRE_TRUE( parse( "HTTP/1.1 200 OK\r\nContent-Le", true ) == HTTPResponseParseStatus::Rejected );
}

SW_TEST_CASE( PlainHTTPClientTest, StatusLineAndNoBodyResponses )
{
    HTTPResponse response;
    SW_REQUIRE_TRUE( parse( "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n", false, response ) == HTTPResponseParseStatus::Complete );
    SW_REQUIRE_EQUAL( 404, response._status );
    SW_REQUIRE_TRUE( parse( "HTTP/1.1 204 No Content\r\n\r\n", false, response ) == HTTPResponseParseStatus::Complete );
    SW_REQUIRE_EQUAL( 204, response._status );
    // 길이 없는 본문은 닫힘이 끝이다.
    SW_REQUIRE_TRUE( parse( "HTTP/1.0 200\r\n\r\nabc", false ) == HTTPResponseParseStatus::Incomplete );
    SW_REQUIRE_TRUE( parse( "HTTP/1.0 200\r\n\r\nabc", true, response ) == HTTPResponseParseStatus::Complete );
    SW_REQUIRE_TRUE( bodyText( response ) == "abc" );

    for ( const string_view bad : { string_view{ "HTTP/2 200 OK\r\n\r\n" }, string_view{ "HTTP/1.1 20 OK\r\n\r\n" }, string_view{ "HTTP/1.1 2000 OK\r\n\r\n" },
                                    string_view{ "HTTP/1.1 099 Low\r\n\r\n" }, string_view{ "HTTP/1.1 600 High\r\n\r\n" }, string_view{ "HTTP/1.1 100 Continue\r\n\r\n" },
                                    string_view{ "HTTP/1.2 200 OK\r\n\r\n" }, string_view{ "http/1.1 200 OK\r\n\r\n" }, string_view{ "HTTP/1.1 2x0 OK\r\n\r\n" },
                                    string_view{ "HTTP/1.1 200 O\rK\r\n\r\n" } } )
    {
        SW_REQUIRE_TRUE( parse( bad, true ) == HTTPResponseParseStatus::Rejected );
    }
}

SW_TEST_CASE( PlainHTTPClientTest, MalformedOrAmbiguousHeadersAreRejected )
{
    for ( const string_view bad : {
              string_view{ "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n folded\r\n\r\nx" },             // 접힌 줄
              string_view{ "HTTP/1.1 200 OK\r\nBad Name: x\r\nContent-Length: 1\r\n\r\nx" },         // 이름의 공백
              string_view{ "HTTP/1.1 200 OK\r\nContent-Length : 1\r\n\r\nx" },                       // 이름 뒤 공백
              string_view{ "HTTP/1.1 200 OK\r\nNoColon\r\n\r\n" },                                   // 콜론 없음
              string_view{ "HTTP/1.1 200 OK\r\n: empty\r\n\r\n" },                                   // 빈 이름
              string_view{ "HTTP/1.1 200 OK\r\nX: a\rb\r\nContent-Length: 0\r\n\r\n" },              // 홀로 선 CR
              string_view{ "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nxy" },  // 다른 길이 둘
              string_view{ "HTTP/1.1 200 OK\r\nContent-Length: +1\r\n\r\nx" },                       // 숫자 아님
              string_view{ "HTTP/1.1 200 OK\r\nContent-Length: 1, 1\r\n\r\nx" },                     // 목록
              string_view{ "HTTP/1.1 200 OK\r\nContent-Length: 99999999999999999999\r\n\r\nx" },     // 자리 넘침
              string_view{ "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\nx" },                  // 모르는 코딩
              string_view{ "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n0\r\n\r\n" }, // 겹친 코딩
              string_view{ "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n" },
              string_view{ "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 1\r\n\r\n0\r\n\r\n" }, // 둘 다(밀반입)
          } )
    {
        SW_REQUIRE_TRUE( parse( bad, true ) == HTTPResponseParseStatus::Rejected );
    }
    // 같은 길이가 두 번이면 받는다(RFC 9110 8.6). 이름은 대소문자를 가리지 않는다.
    HTTPResponse response;
    SW_REQUIRE_TRUE( parse( "HTTP/1.1 200 OK\r\ncontent-length: 1\r\nCONTENT-LENGTH: 1\r\n\r\nx", false, response ) == HTTPResponseParseStatus::Complete );
    SW_REQUIRE_TRUE( bodyText( response ) == "x" );
}

SW_TEST_CASE( PlainHTTPClientTest, ChunkedBodyIsJoinedAndChecked )
{
    const string whole = "HTTP/1.1 200 OK\r\nTransfer-Encoding: Chunked\r\n\r\n5;name=value\r\nhello\r\n6\r\n world\r\n0\r\nX-Trailer: 1\r\n\r\n";
    HTTPResponse response;
    SW_REQUIRE_TRUE( parse( whole, false, response ) == HTTPResponseParseStatus::Complete );
    SW_REQUIRE_TRUE( bodyText( response ) == "hello world" );
    SW_REQUIRE_TRUE( parse( "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nA\r\n0123456789\r\n0\r\n\r\n", false, response ) == HTTPResponseParseStatus::Complete );
    SW_REQUIRE_TRUE( bodyText( response ) == "0123456789" );

    // 어디서 끊겨도 끝나기 전에는 Incomplete, 닫혔으면 거절이다.
    for ( size_t cut = whole.find( "\r\n\r\n" ) + 4; cut < whole.size(); ++cut )
    {
        const string_view part = string_view{ whole }.substr( 0, cut );
        SW_REQUIRE_TRUE( parse( part, false ) == HTTPResponseParseStatus::Incomplete );
        SW_REQUIRE_TRUE( parse( part, true ) == HTTPResponseParseStatus::Rejected );
    }

    for ( const string_view bad : {
              string_view{ "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\nhello\r\n0\r\n\r\n" }, // 16 진 아님
              string_view{ "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5 \r\nhello\r\n0\r\n\r\n" }, // 크기 뒤 공백
              string_view{ "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n-5\r\nhello\r\n0\r\n\r\n" }, // 부호
              string_view{ "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhelloXX0\r\n\r\n" },    // 데이터 뒤 CRLF 없음
              string_view{ "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n10000000000000000\r\n" },    // 자리 넘침
          } )
    {
        SW_REQUIRE_TRUE( parse( bad, true ) == HTTPResponseParseStatus::Rejected );
    }
}

SW_TEST_CASE( PlainHTTPClientTest, LimitsRejectOversizedResponses )
{
    // 머리말: 끝 줄이 오기 전에 상한을 넘으면 기다리지 않고 거절한다.
    const string longHeader = "HTTP/1.1 200 OK\r\nX-Fill: " + string( kHeaderLimit, 'a' );
    SW_REQUIRE_TRUE( parse( longHeader, false ) == HTTPResponseParseStatus::Rejected );
    // 본문: 알린 길이가 상한 위면 본문을 받기 전에 거절한다.
    SW_REQUIRE_TRUE( parse( "HTTP/1.1 200 OK\r\nContent-Length: 65\r\n\r\n", false ) == HTTPResponseParseStatus::Rejected );
    SW_REQUIRE_TRUE( parse( "HTTP/1.1 200 OK\r\nContent-Length: 64\r\n\r\n", false ) == HTTPResponseParseStatus::Incomplete );
    // 길이 없는 본문 · chunked 합.
    SW_REQUIRE_TRUE( parse( "HTTP/1.1 200 OK\r\n\r\n" + string( kBodyLimit + 1, 'b' ), false ) == HTTPResponseParseStatus::Rejected );
    string chunked = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    for ( uint32 index = 0; index < 3; ++index )
    {
        chunked += "20\r\n" + string( 32, 'c' ) + "\r\n"; // 32 × 3 > 64
    }
    SW_REQUIRE_TRUE( parse( chunked, false ) == HTTPResponseParseStatus::Rejected );
    // 원시 바이트: 1 바이트 구간에 긴 확장을 붙여 본문은 작아도 받은 바이트가 커지면 거절한다.
    string padded = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    while ( padded.size() <= kHeaderLimit + kBodyLimit * 2 )
    {
        padded += "1;" + string( 100, 'e' ) + "\r\nx\r\n";
    }
    SW_REQUIRE_TRUE( parse( padded, false ) == HTTPResponseParseStatus::Rejected );
}

SW_TEST_CASE( PlainHTTPClientTest, SplitsOnlyPlainHTTPURLs )
{
    string hostAndPort;
    string target;
    SW_REQUIRE_TRUE( PlainHTTPClient::splitURL( "http://127.0.0.1:1112/stat?path=Packs/a.pack", hostAndPort, target ) );
    SW_REQUIRE_TRUE( hostAndPort == "127.0.0.1:1112" );
    SW_REQUIRE_TRUE( target == "/stat?path=Packs/a.pack" );
    SW_REQUIRE_TRUE( PlainHTTPClient::splitURL( "http://pc.local", hostAndPort, target ) );
    SW_REQUIRE_TRUE( hostAndPort == "pc.local" && target == "/" );
    SW_REQUIRE_TRUE( PlainHTTPClient::splitURL( "http://[::1]:80?x=1", hostAndPort, target ) );
    SW_REQUIRE_TRUE( hostAndPort == "[::1]:80" && target == "/?x=1" );

    for ( const string_view bad : { string_view{ "https://pc/" }, string_view{ "ftp://pc/" }, string_view{ "http://" }, string_view{ "http:///stat" },
                                    string_view{ "http://pc/a b" }, string_view{ "http://pc/a\r\nX-Injected: 1" }, string_view{ "http://user@pc/" },
                                    string_view{ "http://pc/#frag" }, string_view{ "http://pc/\xED\x95\x9C" } } )
    {
        SW_REQUIRE_FALSE( PlainHTTPClient::splitURL( bad, hostAndPort, target ) );
    }
}

SW_TEST_CASE( PlainHTTPClientTest, ServerRootURLHasNoPathQueryOrTrailingSlash )
{
    SW_REQUIRE_TRUE( PlainHTTPClient::isServerRootURL( "http://127.0.0.1:1112" ) );
    SW_REQUIRE_TRUE( PlainHTTPClient::isServerRootURL( "http://pc.local" ) );
    SW_REQUIRE_FALSE( PlainHTTPClient::isServerRootURL( "http://127.0.0.1:1112/" ) ); // 끝점을 붙이면 `//stat` 이 된다
    SW_REQUIRE_FALSE( PlainHTTPClient::isServerRootURL( "http://127.0.0.1:1112/packs" ) );
    SW_REQUIRE_FALSE( PlainHTTPClient::isServerRootURL( "http://127.0.0.1:1112?x=1" ) );
    SW_REQUIRE_FALSE( PlainHTTPClient::isServerRootURL( "https://127.0.0.1:1112" ) );
}

SW_TEST_CASE( PlainHTTPClientTest, SendRefusesWithoutNetworkForNonGETOrBadURL )
{
    FileServeConfig settings;
    settings._maxResponseHeaderBytes = kHeaderLimit;
    settings._maxResponseBodyBytes   = kBodyLimit;
    settings._requestTimeoutMs       = 1;
    PlainHTTPClient client{ settings };
    HTTPRequest     request;
    request._method         = "POST";
    request._url            = "http://127.0.0.1:1/";
    const HTTPResponse post = client.send( request );
    SW_REQUIRE_EQUAL( 0, post._status );
    SW_REQUIRE_TRUE( post._error.empty() == false );
    request._method          = "GET";
    request._url             = "https://127.0.0.1:1/";
    const HTTPResponse https = client.send( request );
    SW_REQUIRE_EQUAL( 0, https._status );
    SW_REQUIRE_TRUE( https._error.empty() == false );
}
