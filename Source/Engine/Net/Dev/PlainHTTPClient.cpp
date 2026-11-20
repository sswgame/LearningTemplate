#include "pch.h"

#include "Engine/Net/Dev/PlainHTTPClient.h"

#include "Core/Container/vector.h"
#include "Core/Network/Transport/PlatformSocketUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

namespace sw
{
    namespace
    {
        struct PlainHTTPClientInternal
        {
            static constexpr string_view kScheme               = "http://";
            static constexpr string_view kLineEnd              = "\r\n";
            static constexpr string_view kHeaderEnd            = "\r\n\r\n";
            static constexpr uint16      kDefaultPort          = 80;
            static constexpr int32       kFirstStatus          = 100;
            static constexpr int32       kFirstFinal           = 200; ///< 1xx 는 중간 응답이다
            static constexpr int32       kLastStatus           = 599;
            static constexpr int32       kNoContent            = 204;
            static constexpr int32       kNotModified          = 304;
            static constexpr size_t      kStatusDigits         = 3;
            static constexpr size_t      kMaxLengthDigits      = 19; ///< Content-Length 십진 자리 상한(uint64 안)
            static constexpr size_t      kMaxChunkDigits       = 15; ///< 구간 크기 16 진 자리 상한(넘침 없이 상한 비교)
            static constexpr int32       kHexBase              = 16;
            static constexpr int32       kDecimalBase          = 10;
            static constexpr size_t      kMinorVersionAndSpace = 2; ///< `HTTP/1.` 뒤 부 판 한 자리 + 공백
            static constexpr int32       kRawBodyFactor        = 2; ///< 원시 바이트 상한 = 머리말 상한 + 본문 상한 × 이 값
            static constexpr int32       kReceiveBytes         = 64 * 1024;
            static constexpr uint8       kFirstVisible         = 0x21;
            static constexpr uint8       kLastVisible          = 0x7E;

            static HTTPResponseParseStatus reject( HTTPResponse& outResponse, const utf8* pReason )
            {
                outResponse._status = 0;
                outResponse._body.clear();
                outResponse._error = pReason;
                return HTTPResponseParseStatus::Rejected;
            }

            static HTTPResponse fail( const utf8* pReason )
            {
                HTTPResponse response;
                response._error = pReason;
                return response;
            }

            static bool isDigit( char character ) { return character >= '0' && character <= '9'; }
            static bool isHexDigit( char character ) { return isDigit( character ) || ( character >= 'a' && character <= 'f' ) || ( character >= 'A' && character <= 'F' ); }

            /** @brief RFC 9110 tchar(헤더 이름 글자)인지입니다. */
            static bool isTokenChar( char character )
            {
                return isDigit( character ) || ( character >= 'a' && character <= 'z' ) || ( character >= 'A' && character <= 'Z' ) ||
                       string_view{ "!#$%&'*+-.^_`|~" }.find( character ) != string_view::npos;
            }

            static string_view trimSpaces( string_view text )
            {
                while ( text.empty() == false && ( text.front() == ' ' || text.front() == '\t' ) )
                {
                    text.remove_prefix( 1 );
                }
                while ( text.empty() == false && ( text.back() == ' ' || text.back() == '\t' ) )
                {
                    text.remove_suffix( 1 );
                }
                return text;
            }

            /** @brief 남은 시한을 기다림 함수가 받는 ms 로 자릅니다(지났으면 0). */
            static int32 remainingMilliseconds( const Deadline& deadline ) { return PlatformSocketUtil::toWaitMilliseconds( deadline.getRemainingMilliseconds() ); }

            /**
             * @brief chunked 본문(@p bytes — 머리말 뒤)을 풉니다. 머리 줄 · 트레일러는 @p maxLineBytes, 본문 합은 @p maxBodyBytes 까지다.
             */
            static HTTPResponseParseStatus parseChunkedBody( string_view bytes, bool bStreamEnded, uint32 maxLineBytes, uint32 maxBodyBytes, HTTPResponse& outResponse )
            {
                const auto needMore = [&]( string_view rest ) -> HTTPResponseParseStatus
                {
                    if ( rest.size() > maxLineBytes )
                        return reject( outResponse, "chunk header or trailer too large" );
                    return bStreamEnded ? reject( outResponse, "connection closed inside a chunked body" ) : HTTPResponseParseStatus::Incomplete;
                };
                vector<uint8>& body = outResponse._body;
                body.clear();
                size_t position = 0;
                for ( ;; )
                {
                    const string_view rest    = bytes.substr( position );
                    const size_t      lineEnd = rest.find( kLineEnd );
                    if ( lineEnd == string_view::npos )
                        return needMore( rest );
                    if ( lineEnd > maxLineBytes )
                        return reject( outResponse, "chunk header too large" );
                    const string_view line   = rest.substr( 0, lineEnd );
                    size_t            digits = 0;
                    while ( digits < line.size() && isHexDigit( line[digits] ) )
                    {
                        ++digits;
                    }
                    // 크기 뒤에는 확장(`;이름=값`)만 온다 — 공백 · 다른 글자는 거절한다(밀반입 기법이 흔히 쓰는 자리).
                    if ( digits == 0 || digits > kMaxChunkDigits || ( digits < line.size() && line[digits] != ';' ) || line.find( '\r' ) != string_view::npos || line.find( '\n' ) != string_view::npos )
                        return reject( outResponse, "malformed chunk size" );
                    uint64 chunkSize = 0;
                    if ( StringUtil::parseUint64( line.substr( 0, digits ), chunkSize, kHexBase ) == false )
                        return reject( outResponse, "malformed chunk size" );
                    position += lineEnd + kLineEnd.size();
                    if ( chunkSize == 0 )
                        break;
                    if ( chunkSize > maxBodyBytes || body.size() + chunkSize > maxBodyBytes )
                        return reject( outResponse, "response body exceeds the limit" );
                    if ( bytes.size() - position < chunkSize + kLineEnd.size() )
                        return bStreamEnded ? reject( outResponse, "connection closed inside a chunk" ) : HTTPResponseParseStatus::Incomplete;
                    const string_view data = bytes.substr( position, static_cast<size_t>( chunkSize ) );
                    if ( bytes.substr( position + data.size(), kLineEnd.size() ) != kLineEnd )
                        return reject( outResponse, "chunk data is not followed by CRLF" );
                    body.insert( body.end(), data.begin(), data.end() );
                    position += data.size() + kLineEnd.size();
                }
                // 트레일러: 빈 줄까지(필드는 쓰지 않는다).
                const string_view trailer = bytes.substr( position );
                if ( trailer.substr( 0, kLineEnd.size() ) == kLineEnd )
                    return HTTPResponseParseStatus::Complete;
                const size_t trailerEnd = trailer.find( kHeaderEnd );
                if ( trailerEnd == string_view::npos )
                    return needMore( trailer );
                if ( trailerEnd + kHeaderEnd.size() > maxLineBytes )
                    return reject( outResponse, "chunk trailer too large" );
                return HTTPResponseParseStatus::Complete;
            }
        };
    } // namespace

    PlainHTTPClient::PlainHTTPClient( const FileServeConfig& settings )
        : _settings{ settings }
    {
    }

    bool PlainHTTPClient::splitURL( string_view url, string& outHostAndPort, string& outTarget )
    {
        if ( url.substr( 0, PlainHTTPClientInternal::kScheme.size() ) != PlainHTTPClientInternal::kScheme )
            return false;
        for ( const char character : url )
        {
            const auto byte = static_cast<uint8>( character );
            if ( byte < PlainHTTPClientInternal::kFirstVisible || byte > PlainHTTPClientInternal::kLastVisible || character == '#' )
                return false;
        }
        const string_view rest         = url.substr( PlainHTTPClientInternal::kScheme.size() );
        const size_t      authorityEnd = rest.find_first_of( "/?" );
        const string_view authority    = rest.substr( 0, authorityEnd );
        if ( authority.empty() || authority.find( '@' ) != string_view::npos )
            return false;
        outHostAndPort.assign( authority.data(), authority.size() );
        const string_view target = authorityEnd == string_view::npos ? string_view{} : rest.substr( authorityEnd );
        outTarget.clear();
        if ( target.empty() || target.front() != '/' )
            outTarget.push_back( '/' );
        outTarget.append( target.data(), target.size() );
        return true;
    }

    bool PlainHTTPClient::isServerRootURL( string_view url )
    {
        string hostAndPort;
        string target;
        return splitURL( url, hostAndPort, target ) && target == "/" && url.back() != '/' && url.find( '?' ) == string_view::npos;
    }

    HTTPResponseParseStatus PlainHTTPClient::parseResponse( string_view bytes, bool bStreamEnded, uint32 maxHeaderBytes, uint32 maxBodyBytes, HTTPResponse& outResponse )
    {
        using Internal      = PlainHTTPClientInternal;
        outResponse._status = 0;
        outResponse._error.clear();
        outResponse._body.clear();
        if ( bytes.size() > uint64{ maxHeaderBytes } + uint64{ maxBodyBytes } * Internal::kRawBodyFactor )
            return Internal::reject( outResponse, "response exceeds the raw size limit" );

        const size_t headerEnd = bytes.find( Internal::kHeaderEnd );
        if ( headerEnd == string_view::npos )
        {
            if ( bytes.size() > maxHeaderBytes )
                return Internal::reject( outResponse, "response header too large" );
            return bStreamEnded ? Internal::reject( outResponse, "connection closed before the response header ended" ) : HTTPResponseParseStatus::Incomplete;
        }
        if ( headerEnd + Internal::kHeaderEnd.size() > maxHeaderBytes )
            return Internal::reject( outResponse, "response header too large" );

        // 상태 줄: `HTTP/1.x SSS[ 이유]`.
        const string_view     header         = bytes.substr( 0, headerEnd );
        const size_t          statusEnd      = header.find( Internal::kLineEnd );
        const string_view     statusLine     = header.substr( 0, statusEnd );
        constexpr string_view kVersionPrefix = "HTTP/1.";
        const size_t          codeAt         = kVersionPrefix.size() + Internal::kMinorVersionAndSpace;
        if ( statusLine.substr( 0, kVersionPrefix.size() ) != kVersionPrefix || statusLine.size() < codeAt + Internal::kStatusDigits ||
             ( statusLine[kVersionPrefix.size()] != '0' && statusLine[kVersionPrefix.size()] != '1' ) || statusLine[kVersionPrefix.size() + 1] != ' ' ||
             ( statusLine.size() > codeAt + Internal::kStatusDigits && statusLine[codeAt + Internal::kStatusDigits] != ' ' ) )
            return Internal::reject( outResponse, "malformed status line" );
        int32 status = 0;
        for ( size_t index = 0; index < Internal::kStatusDigits; ++index )
        {
            const char digit = statusLine[codeAt + index];
            if ( Internal::isDigit( digit ) == false )
                return Internal::reject( outResponse, "malformed status code" );
            status = status * Internal::kDecimalBase + ( digit - '0' );
        }
        if ( status < Internal::kFirstStatus || status > Internal::kLastStatus )
            return Internal::reject( outResponse, "status code out of range" );
        if ( status < Internal::kFirstFinal )
            return Internal::reject( outResponse, "interim (1xx) responses are not supported" ); // GET 만 보내니 서버가 보낼 까닭이 없다

        // 헤더 줄: `이름: 값`. 접힌 줄(obs-fold) · 이름 뒤 공백 · 홀로 선 CR/LF 는 거절한다.
        optional<uint64> contentLength;
        bool             bChunked          = false;
        bool             bTransferEncoding = false;
        string_view      lines             = statusEnd == string_view::npos ? string_view{} : header.substr( statusEnd + Internal::kLineEnd.size() );
        if ( statusLine.find( '\r' ) != string_view::npos || statusLine.find( '\n' ) != string_view::npos )
            return Internal::reject( outResponse, "malformed status line" );
        while ( lines.empty() == false )
        {
            const size_t      lineEnd = lines.find( Internal::kLineEnd );
            const string_view line    = lines.substr( 0, lineEnd );
            lines                     = lineEnd == string_view::npos ? string_view{} : lines.substr( lineEnd + Internal::kLineEnd.size() );
            const size_t colon        = line.find( ':' );
            if ( colon == string_view::npos || colon == 0 || line.find( '\r' ) != string_view::npos || line.find( '\n' ) != string_view::npos || line.find( '\0' ) != string_view::npos )
                return Internal::reject( outResponse, "malformed header line" );
            const string_view name = line.substr( 0, colon );
            for ( const char character : name )
            {
                if ( Internal::isTokenChar( character ) == false )
                    return Internal::reject( outResponse, "malformed header name" );
            }
            const string_view value = Internal::trimSpaces( line.substr( colon + 1 ) );
            if ( StringUtil::equals( name, "Content-Length", true ) )
            {
                if ( value.empty() || value.size() > Internal::kMaxLengthDigits )
                    return Internal::reject( outResponse, "malformed Content-Length" );
                for ( const char character : value )
                {
                    if ( Internal::isDigit( character ) == false )
                        return Internal::reject( outResponse, "malformed Content-Length" );
                }
                uint64 length = 0;
                if ( StringUtil::parseUint64( value, length ) == false || ( contentLength.has_value() && *contentLength != length ) )
                    return Internal::reject( outResponse, "malformed or conflicting Content-Length" );
                contentLength = length;
            }
            else if ( StringUtil::equals( name, "Transfer-Encoding", true ) )
            {
                if ( bTransferEncoding || StringUtil::equals( value, "chunked", true ) == false )
                    return Internal::reject( outResponse, "unsupported Transfer-Encoding" ); // gzip · 겹친 코딩은 보내지 않게 요청했다(Accept-Encoding: identity)
                bTransferEncoding = true;
                bChunked          = true;
            }
        }
        if ( bChunked && contentLength.has_value() )
            return Internal::reject( outResponse, "both Content-Length and Transfer-Encoding" );

        const string_view body = bytes.substr( headerEnd + Internal::kHeaderEnd.size() );
        if ( status == Internal::kNoContent || status == Internal::kNotModified )
        {
            outResponse._status = status;
            return HTTPResponseParseStatus::Complete;
        }
        HTTPResponseParseStatus result = HTTPResponseParseStatus::Incomplete;
        if ( bChunked )
            result = Internal::parseChunkedBody( body, bStreamEnded, maxHeaderBytes, maxBodyBytes, outResponse );
        else if ( contentLength.has_value() )
        {
            if ( *contentLength > maxBodyBytes )
                return Internal::reject( outResponse, "response body exceeds the limit" );
            if ( body.size() < *contentLength )
                return bStreamEnded ? Internal::reject( outResponse, "connection closed before the body ended" ) : HTTPResponseParseStatus::Incomplete;
            outResponse._body.assign( body.begin(), body.begin() + static_cast<ptrdiff_t>( *contentLength ) ); // 뒤에 더 온 바이트는 버린다(요청마다 연결을 닫는다)
            result = HTTPResponseParseStatus::Complete;
        }
        else
        {
            // 길이 없는 본문 — 닫힘이 끝이다(RFC 9112 6.3 의 마지막 규칙).
            if ( body.size() > maxBodyBytes )
                return Internal::reject( outResponse, "response body exceeds the limit" );
            if ( bStreamEnded == false )
                return HTTPResponseParseStatus::Incomplete;
            outResponse._body.assign( body.begin(), body.end() );
            result = HTTPResponseParseStatus::Complete;
        }
        if ( result == HTTPResponseParseStatus::Complete )
            outResponse._status = status;
        else if ( result == HTTPResponseParseStatus::Incomplete )
            outResponse._body.clear();
        return result;
    }

    HTTPResponse PlainHTTPClient::send( const HTTPRequest& request )
    {
        using Internal = PlainHTTPClientInternal;
        if ( request._method != "GET" )
            return Internal::fail( "only GET is supported" );
        string hostAndPort;
        string target;
        if ( splitURL( request._url, hostAndPort, target ) == false )
            return Internal::fail( "malformed URL (http:// only, printable ASCII, no userinfo or fragment)" );
        const Deadline deadline = Deadline::afterMilliseconds( _settings._requestTimeoutMs );
        NetAddress     address{};
        if ( PlatformSocketUtil::resolveAddress( hostAndPort, Internal::kDefaultPort, address ) == false )
            return Internal::fail( "could not resolve the host" );
        const uint64 socketHandle = PlatformSocketUtil::connectTCPSocket( address, Internal::remainingMilliseconds( deadline ) );
        if ( socketHandle == PlatformSocketUtil::kInvalidSocket )
            return Internal::fail( "could not connect" );

        StringBuilder<constant::buffer::kMax512> requestBuilder;
        requestBuilder.appendFormat( "GET %# HTTP/1.1\r\nHost: %#\r\nConnection: close\r\nAccept-Encoding: identity\r\n\r\n", target, hostAndPort );
        const string_view requestBytes = requestBuilder.view();
        HTTPResponse      response;
        size_t            sentBytes = 0;
        while ( sentBytes < requestBytes.size() )
        {
            const int32 sent = PlatformSocketUtil::sendStream( socketHandle, reinterpret_cast<const uint8*>( requestBytes.data() + sentBytes ), static_cast<int32>( requestBytes.size() - sentBytes ) );
            if ( sent == PlatformSocketUtil::kStreamSendFailed )
            {
                PlatformSocketUtil::closeSocket( socketHandle );
                return Internal::fail( "send failed" );
            }
            sentBytes += static_cast<size_t>( sent );
            if ( sent == 0 && PlatformSocketUtil::waitWritable( socketHandle, Internal::remainingMilliseconds( deadline ) ) == false )
            {
                PlatformSocketUtil::closeSocket( socketHandle );
                return Internal::fail( "request timed out while sending" );
            }
        }

        string        received;
        vector<uint8> buffer( static_cast<size_t>( Internal::kReceiveBytes ) );
        for ( ;; )
        {
            int32                     receivedSize = 0;
            const SocketReceiveResult result       = PlatformSocketUtil::receiveStream( socketHandle, buffer.data(), Internal::kReceiveBytes, receivedSize );
            if ( result == SocketReceiveResult::Error )
            {
                response = Internal::fail( "receive failed" );
                break;
            }
            if ( result == SocketReceiveResult::WouldBlock )
            {
                const int32 remaining = Internal::remainingMilliseconds( deadline );
                if ( remaining == 0 || PlatformSocketUtil::waitReadable( socketHandle, remaining ) == false )
                {
                    response = Internal::fail( "request timed out" );
                    break;
                }
                continue;
            }
            const bool bStreamEnded = receivedSize == 0;
            received.append( reinterpret_cast<const char*>( buffer.data() ), static_cast<size_t>( receivedSize ) );
            const HTTPResponseParseStatus status = parseResponse( received, bStreamEnded, _settings._maxResponseHeaderBytes, _settings._maxResponseBodyBytes, response );
            if ( status != HTTPResponseParseStatus::Incomplete )
                break;
            if ( Internal::remainingMilliseconds( deadline ) == 0 )
            {
                response = Internal::fail( "request timed out" );
                break;
            }
        }
        PlatformSocketUtil::closeSocket( socketHandle );
        return response;
    }
} // namespace sw
