/**
 * @file HTTPTypes.h
 * @brief 최소 HTTP/1.1 타입을 정의합니다.
 * @details 요청, 응답, 머리, URL, 폼과 쿼리 글, 증분 메시지 파서가 있다.
 *          외부 로그인 제공자(JWKS, 토큰, 프로필 API)와 영수증 검증과 푸시가 쓰는 바깥 HTTPS 호출에 쓴다.
 *          PC 외부 로그인의 루프백 리다이렉트를 받을 때도 쓴다.
 *          게임 서비스 통신은 이것을 쓰지 않고 `Online/Service` 의 프레임 프로토콜을 쓴다.
 *
 *          - 연결 하나에 요청 하나(`Connection: close`)만 처리한다. 연결 유지, 파이프라인, 리다이렉트, 압축 응답은 지원하지 않는다.
 *            몸은 `Content-Length` 나 chunked 로 읽고, 둘 다 없으면 연결이 닫힐 때까지 읽는다.
 *          - 머리 이름은 대소문자를 가리지 않고 찾는다. 상한(머리 16 KiB, 몸은 설정값)을 넘으면 파서가 실패한다.
 *          - 호스트는 IPv4 글자나 `localhost` 만 쓸 수 있다. Core 주소에 이름 해석이 없기 때문이며, 실제 제공자 호스트 이름은 후보 절(`Candidates-network.md`)에 있다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    enum class HTTPMethod : uint8
    {
        Get = 0,
        Post
    };

    SW_GF_API const utf8* toString( HTTPMethod method );
} // namespace sw

namespace sw
{
    /** @brief 머리 하나입니다. 쿼리나 폼의 이름과 값 한 쌍으로도 씁니다. */
    struct HTTPHeader
    {
        string _name{};
        string _value{};
    };
} // namespace sw

namespace sw
{
    /** @brief HTTP 상한 값들입니다. */
    struct HTTPConstant
    {
        static constexpr int32 kMaxHeaderBytes      = 16 * 1024;
        static constexpr int32 kDefaultMaxBodyBytes = static_cast<int32>( constant::kBytesPerMegabyte );
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
    struct HTTPClientRequest
    {
        vector<HTTPHeader> _listHeader{}; ///< `Host`, `Content-Length`, `Connection` 은 클라이언트가 붙인다
        vector<uint8>      _bodyBytes{};
        string             _url{}; ///< `http://` · `https://` + 호스트[:포트] + 경로[?쿼리]
        int64              _timeoutMs{ HTTPConstant::kDefaultTimeoutMs };
        HTTPMethod         _method{ HTTPMethod::Get };
    };
} // namespace sw

namespace sw
{
    /** @brief 받은 응답 하나입니다. 전송이 실패하면(연결, TLS, 시한, 상한) `_bTransportFailed` 가 켜지고 상태 코드는 0 입니다. */
    struct SW_GF_API HTTPClientResponse
    {
        vector<HTTPHeader> _listHeader{};
        vector<uint8>      _bodyBytes{};
        string             _failureText{};
        uint64             _requestID{ 0 };
        int32              _statusCode{ 0 };
        uint8              _bTransportFailed{ SW_FALSE };

        bool              isSuccess() const { return _bTransportFailed == SW_FALSE && 200 <= _statusCode && _statusCode <= 299; }
        const HTTPHeader* findHeader( string_view name ) const;
        string_view       getBodyText() const { return string_view( reinterpret_cast<const utf8*>( _bodyBytes.data() ), _bodyBytes.size() ); }
    };
} // namespace sw

namespace sw
{
    /** @brief 서버가 받은 요청 하나입니다. 루프백 리다이렉트와 테스트 서버가 씁니다. */
    struct SW_GF_API HTTPServerRequest
    {
        vector<HTTPHeader> _listHeader{};
        vector<HTTPHeader> _listQuery{}; ///< 경로 뒤 `?` 의 이름 = 값(퍼센트 풀림)
        vector<uint8>      _bodyBytes{};
        string             _path{}; ///< `?` 앞(퍼센트 그대로)
        HTTPMethod         _method{ HTTPMethod::Get };

        const HTTPHeader* findHeader( string_view name ) const;
        const HTTPHeader* findQuery( string_view name ) const;
        string_view       getBodyText() const { return string_view( reinterpret_cast<const utf8*>( _bodyBytes.data() ), _bodyBytes.size() ); }
    };
} // namespace sw

namespace sw
{
    /** @brief 서버가 돌려줄 응답입니다. */
    struct HTTPServerResponse
    {
        vector<HTTPHeader> _listHeader{}; ///< `Content-Length` · `Connection` 은 서버가 붙인다
        vector<uint8>      _bodyBytes{};
        int32              _statusCode{ HTTPConstant::kStatusOk };
        uint8              _bChunked{ SW_FALSE }; ///< 본문을 chunked 로 보낸다(받는 쪽 chunked 해석을 테스트할 때 쓴다)
    };
} // namespace sw

namespace sw
{
    /** @brief 나눈 URL 입니다. */
    struct SW_GF_API HTTPAddress
    {
        string _host{};
        string _target{ "/" }; ///< 경로 + 쿼리(요청 줄에 그대로)
        uint16 _port{ 0 };
        uint8  _bSecure{ SW_FALSE };

        /** @brief `http(s)://호스트[:포트][/경로][?쿼리]` 를 나눕니다. 사용자 정보, 조각(`#`), 빈 호스트, 틀린 포트가 있으면 false 입니다. */
        [[nodiscard]] static bool parse( string_view url, HTTPAddress& outAddress );
    };
} // namespace sw

namespace sw
{
    /** @brief 퍼센트 인코딩, 폼, 쿼리, 머리를 다루는 도우미입니다. */
    struct SW_GF_API HTTPUtil
    {
        /** @brief RFC 3986 비예약 글자(`A-Za-z0-9-._~`)만 남기고 나머지는 `%XX` 로 씁니다. */
        static string encodePercent( string_view text );
        /** @brief `%XX` 를 풀고(폼이면 `+` 를 공백으로) 씁니다. 틀린 `%` 는 false 입니다. */
        [[nodiscard]] static bool decodePercent( string_view text, bool bForm, string& outText );
        /** @brief `이름=값&…` 형식(application/x-www-form-urlencoded, 쿼리)으로 씁니다. */
        static string encodeForm( const vector<HTTPHeader>& listPair );
        /** @brief `이름=값&…` 을 풉니다. 틀린 퍼센트 인코딩이 있으면 false 입니다. */
        [[nodiscard]] static bool decodeForm( string_view text, vector<HTTPHeader>& outListPair );
        /** @brief 머리 목록에서 이름으로 찾습니다. 대소문자는 가립니다. */
        static const HTTPHeader* findHeader( const vector<HTTPHeader>& listHeader, string_view name );
        static bool              isEqualIgnoreCase( string_view left, string_view right );
    };
} // namespace sw

namespace sw
{
    /** @brief 파서가 읽는 메시지의 방향(요청 또는 응답)입니다. */
    enum class HTTPMessageKind : uint8
    {
        Request = 0, ///< 서버가 받는 요청(요청 줄)
        Response     ///< 클라이언트가 받는 응답(상태 줄)
    };

    /** @brief 파서 상태입니다. */
    enum class HTTPParseState : uint8
    {
        NeedMore = 0,
        Complete,
        Failed
    };
} // namespace sw

namespace sw
{
    /**
     * @class HTTPMessageParser
     * @brief 증분 HTTP/1.1 메시지 파서입니다.
     * @details 바이트를 넣으면 머리와 몸을 모은다. 몸은 Content-Length, chunked, 연결이 닫힐 때까지 중 하나로 읽는다.
     */
    class SW_GF_API HTTPMessageParser
    {
    public:
        HTTPMessageParser();

        void reset( HTTPMessageKind kind, int32 maxBodyBytes );
        /** @brief 받은 바이트를 넣습니다. 완료나 실패 뒤의 바이트는 버립니다. */
        HTTPParseState append( const uint8* pData, size_t size );
        /** @brief 연결이 닫혔음을 알립니다. 길이가 없는 응답 몸이면 여기서 끝납니다. */
        HTTPParseState finishOnClose();

        HTTPParseState            getState() const { return _state; }
        const string&             getFailureText() const { return _failureText; }
        int32                     getStatusCode() const { return _statusCode; }
        HTTPMethod                getMethod() const { return _method; }
        const string&             getTarget() const { return _target; }
        const vector<HTTPHeader>& getHeaders() const { return _listHeader; }
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

        HTTPParseState     fail( const utf8* pReason );
        [[nodiscard]] bool parseHead( string_view head );
        /** @brief 버퍼에서 읽을 수 있는 데까지 단계를 넘깁니다. 단계마다 짧은 분기이고, 긴 단계는 아래 함수로 나눴습니다. */
        HTTPParseState advance();
        /**
         * @brief 머리를 끝까지 받았으면 읽고 몸을 읽는 방식(청크, 길이, 닫힐 때까지)을 정합니다.
         * @details 아래 단계 함수는 모두 같은 계약입니다. 다음 단계로 넘어갔으면 true, 데이터가 더 필요하거나 실패했으면 false 입니다.
         *          실패는 `fail` 이 상태에 남깁니다.
         */
        bool advanceHead();
        /** @brief 남은 몸 바이트를 버퍼에서 옮깁니다. 다 받으면 @p nextPhase 로 넘어갑니다. 고정 길이 몸과 청크 데이터가 같이 씁니다. */
        bool takeBodyBytes( Phase nextPhase );
        /** @brief 청크 크기 줄을 읽습니다. 크기 0 이면 트레일러로 넘어갑니다. */
        bool advanceChunkSize();

        vector<HTTPHeader> _listHeader;
        vector<uint8>      _buffer; ///< 아직 해석하지 않은 바이트
        vector<uint8>      _bodyBytes;
        string             _target;
        string             _failureText;
        int64              _remainingBytes;
        int32              _maxBodyBytes;
        int32              _statusCode;
        HTTPMethod         _method;
        HTTPMessageKind    _kind;
        HTTPParseState     _state;
        Phase              _phase;
    };
} // namespace sw

namespace sw
{
    /** @brief 메시지 쓰기 도우미입니다. */
    struct SW_GF_API HTTPWriteUtil
    {
        static void        writeRequest( const HTTPClientRequest& request, const HTTPAddress& url, vector<uint8>& outBytes );
        static void        writeResponse( const HTTPServerResponse& response, vector<uint8>& outBytes );
        static const utf8* getReasonPhrase( int32 statusCode );
    };
} // namespace sw
