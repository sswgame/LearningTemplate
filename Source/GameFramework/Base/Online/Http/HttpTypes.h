/**
 * @file HttpTypes.h
 * @brief 최소 HTTP/1.1 — 요청 · 응답 · 머리 · URL · 폼/쿼리 글 · 증분 메시지 파서입니다. 외부 로그인 제공자(JWKS · 토큰 · 프로필 API) · 영수증 검증 · 푸시가
 *        쓰는 바깥 HTTPS 호출과 PC 외부 로그인의 루프백 리다이렉트 받기에 씁니다(게임 서비스 통신은 `Online/Service` 의 프레임 프로토콜).
 * @details - 연결 하나에 요청 하나(`Connection: close`) — 유지 · 파이프라인 · 리다이렉트 · 압축 응답은 하지 않는다. 몸은 `Content-Length` 또는 chunked, 둘 다 없으면 닫힐 때까지.
 *          - 머리 이름은 대소문자를 가리지 않고 찾는다. 상한(머리 16 KiB · 몸 설정값)을 넘으면 파서가 실패한다.
 *          - 호스트는 IPv4 글자 또는 `localhost` 만 — Core 주소에 이름 해석이 없다(실제 제공자 호스트 이름은 백로그).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    enum class HttpMethod : uint8
    {
        Get = 0,
        Post
    };

    SW_GF_API const utf8* toString( HttpMethod method );
} // namespace sw

namespace sw
{
    /** @brief 머리 하나(또는 쿼리 · 폼의 이름 = 값 한 쌍)입니다. */
    struct HttpHeader
    {
        string _name{};
        string _value{};
    };
} // namespace sw

namespace sw
{
    /** @brief HTTP 상한입니다. */
    struct HttpConstant
    {
        static constexpr int32 kMaxHeaderBytes      = 16 * 1024;
        static constexpr int32 kDefaultMaxBodyBytes = 1024 * 1024;
        static constexpr int64 kDefaultTimeoutMs    = 10000;
        static constexpr int32 kStatusOk            = 200;
        static constexpr int32 kStatusUnauthorized  = 401;
        static constexpr int32 kStatusForbidden     = 403;
        static constexpr int32 kStatusNotFound      = 404;
    };
} // namespace sw

namespace sw
{
    /** @brief 나가는 요청 하나입니다. */
    struct HttpClientRequest
    {
        vector<HttpHeader> _listHeader{}; ///< `Host` · `Content-Length` · `Connection` 은 클라이언트가 붙인다
        vector<uint8>      _bodyBytes{};
        string             _url{}; ///< `http://` · `https://` + 호스트[:포트] + 경로[?쿼리]
        int64              _timeoutMs{ HttpConstant::kDefaultTimeoutMs };
        HttpMethod         _method{ HttpMethod::Get };
    };
} // namespace sw

namespace sw
{
    /** @brief 받은 응답 하나입니다. 전송 실패(연결 · TLS · 시한 · 상한)면 `_bTransportFailed` 이고 상태 코드는 0 입니다. */
    struct SW_GF_API HttpClientResponse
    {
        vector<HttpHeader> _listHeader{};
        vector<uint8>      _bodyBytes{};
        string             _failureText{};
        uint64             _requestID{ 0 };
        int32              _statusCode{ 0 };
        uint8              _bTransportFailed{ SW_FALSE };

        bool              isSuccess() const { return _bTransportFailed == SW_FALSE && 200 <= _statusCode && _statusCode <= 299; }
        const HttpHeader* findHeader( string_view name ) const;
        string_view       getBodyText() const { return string_view( reinterpret_cast<const utf8*>( _bodyBytes.data() ), _bodyBytes.size() ); }
    };
} // namespace sw

namespace sw
{
    /** @brief 서버가 받은 요청 하나입니다(루프백 리다이렉트 · 시험 서버). */
    struct SW_GF_API HttpServerRequest
    {
        vector<HttpHeader> _listHeader{};
        vector<HttpHeader> _listQuery{}; ///< 경로 뒤 `?` 의 이름 = 값(퍼센트 풀림)
        vector<uint8>      _bodyBytes{};
        string             _path{}; ///< `?` 앞(퍼센트 그대로)
        HttpMethod         _method{ HttpMethod::Get };

        const HttpHeader* findHeader( string_view name ) const;
        const HttpHeader* findQuery( string_view name ) const;
        string_view       getBodyText() const { return string_view( reinterpret_cast<const utf8*>( _bodyBytes.data() ), _bodyBytes.size() ); }
    };
} // namespace sw

namespace sw
{
    /** @brief 서버가 돌려줄 응답입니다. */
    struct HttpServerResponse
    {
        vector<HttpHeader> _listHeader{}; ///< `Content-Length` · `Connection` 은 서버가 붙인다
        vector<uint8>      _bodyBytes{};
        int32              _statusCode{ HttpConstant::kStatusOk };
        uint8              _bChunked{ SW_FALSE }; ///< 몸을 chunked 로 보낸다(시험 — 받는 쪽 chunked 풀기)
    };
} // namespace sw

namespace sw
{
    /** @brief 나눈 URL 입니다. */
    struct SW_GF_API HttpURL
    {
        string _host{};
        string _target{ "/" }; ///< 경로 + 쿼리(요청 줄에 그대로)
        uint16 _port{ 0 };
        uint8  _bSecure{ SW_FALSE };

        /** @brief `http(s)://호스트[:포트][/경로][?쿼리]` 를 나눕니다. 사용자 정보 · 조각(`#`) · 빈 호스트 · 틀린 포트는 false 입니다. */
        [[nodiscard]] static bool parse( string_view url, HttpURL& outURL );
    };
} // namespace sw

namespace sw
{
    /** @brief 퍼센트 인코딩 · 폼 · 쿼리 · 머리 도우미입니다. */
    struct SW_GF_API HttpUtil
    {
        /** @brief RFC 3986 비예약 글자(`A-Za-z0-9-._~`)만 남기고 나머지는 `%XX` 로 씁니다. */
        static string encodePercent( string_view text );
        /** @brief `%XX` 를 풀고(폼이면 `+` 를 공백으로) 씁니다. 틀린 `%` 는 false 입니다. */
        [[nodiscard]] static bool decodePercent( string_view text, bool bForm, string& outText );
        /** @brief `이름=값&…`(application/x-www-form-urlencoded · 쿼리)을 씁니다. */
        static string encodeForm( const vector<HttpHeader>& listPair );
        /** @brief `이름=값&…` 을 풉니다. 틀린 퍼센트가 있으면 false 입니다. */
        [[nodiscard]] static bool decodeForm( string_view text, vector<HttpHeader>& outListPair );
        /** @brief 머리 목록에서 이름(대소문자 무시)으로 찾습니다. */
        static const HttpHeader* findHeader( const vector<HttpHeader>& listHeader, string_view name );
        static bool              isEqualIgnoreCase( string_view left, string_view right );
    };
} // namespace sw

namespace sw
{
    /** @brief 파서가 읽는 메시지 쪽입니다. */
    enum class HttpMessageKind : uint8
    {
        Request = 0, ///< 서버가 받는 요청(요청 줄)
        Response     ///< 클라이언트가 받는 응답(상태 줄)
    };

    /** @brief 파서 상태입니다. */
    enum class HttpParseState : uint8
    {
        NeedMore = 0,
        Complete,
        Failed
    };
} // namespace sw

namespace sw
{
    /**
     * @class HttpMessageParser
     * @brief 증분 HTTP/1.1 메시지 파서 — 바이트를 넣으면 머리 · 몸(Content-Length · chunked · 닫힐 때까지)을 모읍니다.
     */
    class SW_GF_API HttpMessageParser
    {
    public:
        HttpMessageParser();

        void reset( HttpMessageKind kind, int32 maxBodyBytes );
        /** @brief 받은 바이트를 넣습니다. 완료 · 실패 뒤의 바이트는 버린다. */
        HttpParseState append( const uint8* pData, size_t size );
        /** @brief 연결이 닫혔다 — 길이 없는 응답 몸이면 여기서 끝난다. */
        HttpParseState finishOnClose();

        HttpParseState            getState() const { return _state; }
        const string&             getFailureText() const { return _failureText; }
        int32                     getStatusCode() const { return _statusCode; }
        HttpMethod                getMethod() const { return _method; }
        const string&             getTarget() const { return _target; }
        const vector<HttpHeader>& getHeaders() const { return _listHeader; }
        vector<uint8>&            getBody() { return _bodyBytes; }

    private:
        enum class Phase : uint8
        {
            Head = 0,
            FixedBody,
            ChunkSize,
            ChunkData,
            ChunkDataEnd,
            Trailer,
            UntilClose,
            Done
        };

        HttpParseState     fail( const utf8* pReason );
        [[nodiscard]] bool parseHead( string_view head );
        HttpParseState     advance();

        vector<HttpHeader> _listHeader;
        vector<uint8>      _buffer; ///< 아직 해석하지 않은 바이트
        vector<uint8>      _bodyBytes;
        string             _target;
        string             _failureText;
        int64              _remainingBytes;
        int32              _maxBodyBytes;
        int32              _statusCode;
        HttpMethod         _method;
        HttpMessageKind    _kind;
        HttpParseState     _state;
        Phase              _phase;
    };
} // namespace sw

namespace sw
{
    /** @brief 메시지 쓰기 도우미입니다. */
    struct SW_GF_API HttpWriteUtil
    {
        static void        writeRequest( const HttpClientRequest& request, const HttpURL& url, vector<uint8>& outBytes );
        static void        writeResponse( const HttpServerResponse& response, vector<uint8>& outBytes );
        static const utf8* getReasonPhrase( int32 statusCode );
    };
} // namespace sw
