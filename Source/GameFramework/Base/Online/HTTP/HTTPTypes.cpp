#include "pch.h"

#include "GameFramework/Base/Online/HTTP/HTTPTypes.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/HexUtil.h"
#include "Core/String/StringBuilder.h"

namespace sw
{
    namespace
    {
        struct HTTPTypesInternal
        {
            static constexpr int32 kMaxChunkLine   = 64;
            static constexpr int64 kMaxChunkSize   = static_cast<int64>( 64 * constant::kBytesPerMegabyte );
            static constexpr int32 kMaxTargetBytes = 8192;

            static bool isUnreserved( utf8 ch )
            {
                const bool bLetter = ( 'a' <= ch && ch <= 'z' ) || ( 'A' <= ch && ch <= 'Z' );
                const bool bDigit  = '0' <= ch && ch <= '9';
                return bLetter || bDigit || ch == '-' || ch == '.' || ch == '_' || ch == '~';
            }

            static int32 findHexValue( utf8 ch )
            {
                uint32 value = 0;
                return HexDigitUtil::tryParseDigit( ch, value ) ? static_cast<int32>( value ) : -1;
            }

            /** @brief @p bytes 에서 "\r\n" 의 위치입니다(없으면 npos). */
            static size_t findLineEnd( const vector<uint8>& bytes, size_t from )
            {
                for ( size_t index = from; index + 1 < bytes.size(); ++index )
                {
                    if ( bytes[index] == '\r' && bytes[index + 1] == '\n' )
                        return index;
                }
                return string::npos;
            }

            static void   appendText( vector<uint8>& outBytes, string_view text ) { outBytes.insert( outBytes.end(), reinterpret_cast<const uint8*>( text.data() ), reinterpret_cast<const uint8*>( text.data() ) + text.size() ); }
            static string makeHex( size_t value )
            {
                string text;
                HexUtil::appendUint64Minimal( text, value, 1, HexCase::Upper );
                return text;
            }

            static void appendHeader( vector<uint8>& outBytes, string_view name, string_view value )
            {
                appendText( outBytes, name );
                appendText( outBytes, ": " );
                appendText( outBytes, value );
                appendText( outBytes, "\r\n" );
            }

            [[nodiscard]] static bool parseUnsigned( string_view text, int32 base, int64& outValue )
            {
                if ( text.empty() || text.size() > 18 )
                    return false;
                int64 value = 0;
                for ( const utf8 ch : text )
                {
                    const int32 digit = base == 16 ? findHexValue( ch ) : ( '0' <= ch && ch <= '9' ? ch - '0' : -1 );
                    if ( digit < 0 )
                        return false;
                    value = value * base + digit;
                }
                outValue = value;
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( HTTPMethod method )
    {
        switch ( method )
        {
            case HTTPMethod::Get:
                return "GET";
            case HTTPMethod::Post:
                return "POST";
        }
        return "GET";
    }

    const HTTPHeader* HTTPClientResponse::findHeader( string_view name ) const
    {
        return HTTPUtil::findHeader( _listHeader, name );
    }

    const HTTPHeader* HTTPServerRequest::findHeader( string_view name ) const
    {
        return HTTPUtil::findHeader( _listHeader, name );
    }

    const HTTPHeader* HTTPServerRequest::findQuery( string_view name ) const
    {
        return VectorUtil::findBy<&HTTPHeader::_name>( _listQuery, name );
    }

    bool HTTPAddress::parse( string_view url, HTTPAddress& outAddress )
    {
        outAddress = HTTPAddress{};
        string_view rest;
        if ( url.substr( 0, 8 ) == "https://" )
        {
            outAddress._bSecure = SW_TRUE;
            outAddress._port    = 443;
            rest                = url.substr( 8 );
        }
        else if ( url.substr( 0, 7 ) == "http://" )
        {
            outAddress._port = 80;
            rest             = url.substr( 7 );
        }
        else
        {
            return false;
        }
        if ( rest.find( '#' ) != string_view::npos )
            return false;
        const size_t      slash     = rest.find_first_of( "/?" );
        const string_view authority = rest.substr( 0, slash );
        if ( authority.empty() || authority.find( '@' ) != string_view::npos )
            return false;
        const size_t colon = authority.rfind( ':' );
        outAddress._host   = string( authority.substr( 0, colon ) );
        if ( colon != string_view::npos )
        {
            int64      port         = 0;
            const bool bInvalidPort = HTTPTypesInternal::parseUnsigned( authority.substr( colon + 1 ), 10, port ) == false || port <= 0 || port > 65535;
            if ( bInvalidPort )
                return false;
            outAddress._port = static_cast<uint16>( port );
        }
        if ( outAddress._host.empty() )
            return false;
        if ( slash != string_view::npos )
        {
            outAddress._target = string( rest.substr( slash ) );
            if ( outAddress._target.front() == '?' )
                outAddress._target.insert( outAddress._target.begin(), '/' );
        }
        return true;
    }

    string HTTPUtil::encodePercent( string_view text )
    {
        string encoded;
        encoded.reserve( text.size() );
        for ( const utf8 ch : text )
        {
            if ( HTTPTypesInternal::isUnreserved( ch ) )
            {
                encoded.push_back( ch );
                continue;
            }
            encoded.push_back( '%' );
            HexUtil::appendUint64( encoded, static_cast<uint8>( ch ), 2, HexCase::Upper );
        }
        return encoded;
    }

    bool HTTPUtil::decodePercent( string_view text, bool bForm, string& outText )
    {
        outText.clear();
        outText.reserve( text.size() );
        for ( size_t index = 0; index < text.size(); ++index )
        {
            const utf8 ch = text[index];
            if ( ch == '+' && bForm )
            {
                outText.push_back( ' ' );
                continue;
            }
            if ( ch != '%' )
            {
                outText.push_back( ch );
                continue;
            }
            if ( index + 2 >= text.size() )
                return false;
            const int32 high = HTTPTypesInternal::findHexValue( text[index + 1] );
            const int32 low  = HTTPTypesInternal::findHexValue( text[index + 2] );
            if ( high < 0 || low < 0 )
                return false;
            outText.push_back( static_cast<utf8>( high * 16 + low ) );
            index += 2;
        }
        return true;
    }

    string HTTPUtil::encodeForm( const vector<HTTPHeader>& listPair )
    {
        string text;
        for ( const HTTPHeader& pair : listPair )
        {
            if ( text.empty() == false )
                text.push_back( '&' );
            text += encodePercent( pair._name );
            text.push_back( '=' );
            text += encodePercent( pair._value );
        }
        return text;
    }

    bool HTTPUtil::decodeForm( string_view text, vector<HTTPHeader>& outListPair )
    {
        outListPair.clear();
        while ( text.empty() == false )
        {
            const size_t      amp  = text.find( '&' );
            const string_view part = text.substr( 0, amp );
            text                   = amp == string_view::npos ? string_view{} : text.substr( amp + 1 );
            if ( part.empty() )
                continue;
            const size_t equal = part.find( '=' );
            HTTPHeader&  pair  = outListPair.emplace_back();
            if ( decodePercent( part.substr( 0, equal ), true, pair._name ) == false )
                return false;
            if ( equal != string_view::npos && decodePercent( part.substr( equal + 1 ), true, pair._value ) == false )
                return false;
        }
        return true;
    }

    const HTTPHeader* HTTPUtil::findHeader( const vector<HTTPHeader>& listHeader, string_view name )
    {
        return VectorUtil::findIf( listHeader, [&]( const HTTPHeader& header )
        { return isEqualIgnoreCase( header._name, name ); } );
    }

    bool HTTPUtil::isEqualIgnoreCase( string_view left, string_view right )
    {
        if ( left.size() != right.size() )
            return false;
        for ( size_t index = 0; index < left.size(); ++index )
        {
            if ( StringUtil::toLowerChar( left[index] ) != StringUtil::toLowerChar( right[index] ) )
                return false;
        }
        return true;
    }

    HTTPMessageParser::HTTPMessageParser()
        : _listHeader{}
        , _buffer{}
        , _bodyBytes{}
        , _target{}
        , _failureText{}
        , _remainingBytes{ 0 }
        , _maxBodyBytes{ HTTPConstant::kDefaultMaxBodyBytes }
        , _statusCode{ 0 }
        , _method{ HTTPMethod::Get }
        , _kind{ HTTPMessageKind::Response }
        , _state{ HTTPParseState::NeedMore }
        , _phase{ Phase::Head }
    {
    }

    void HTTPMessageParser::reset( HTTPMessageKind kind, int32 maxBodyBytes )
    {
        _listHeader.clear();
        _buffer.clear();
        _bodyBytes.clear();
        _target.clear();
        _failureText.clear();
        _remainingBytes = 0;
        _maxBodyBytes   = maxBodyBytes;
        _statusCode     = 0;
        _method         = HTTPMethod::Get;
        _kind           = kind;
        _state          = HTTPParseState::NeedMore;
        _phase          = Phase::Head;
    }

    HTTPParseState HTTPMessageParser::append( const uint8* pData, size_t size )
    {
        if ( _state != HTTPParseState::NeedMore )
            return _state;
        _buffer.insert( _buffer.end(), pData, pData + size );
        return advance();
    }

    HTTPParseState HTTPMessageParser::finishOnClose()
    {
        if ( _state != HTTPParseState::NeedMore )
            return _state;
        if ( _phase == Phase::UntilClose )
        {
            _phase = Phase::Done;
            _state = HTTPParseState::Complete;
            return _state;
        }
        return fail( "connection closed before the message ended" );
    }

    HTTPParseState HTTPMessageParser::fail( const utf8* pReason )
    {
        _failureText = pReason;
        _state       = HTTPParseState::Failed;
        _buffer.clear();
        return _state;
    }

    bool HTTPMessageParser::parseHead( string_view head )
    {
        const size_t      firstEnd  = head.find( "\r\n" );
        const string_view firstLine = head.substr( 0, firstEnd );
        string_view       rest      = firstEnd == string_view::npos ? string_view{} : head.substr( firstEnd + 2 );
        const size_t      space     = firstLine.find( ' ' );
        if ( space == string_view::npos )
            return false;
        if ( _kind == HTTPMessageKind::Response )
        {
            if ( firstLine.substr( 0, 7 ) != "HTTP/1." || firstLine.size() < space + 4 )
                return false;
            int64 statusCode = 0;
            if ( HTTPTypesInternal::parseUnsigned( firstLine.substr( space + 1, 3 ), 10, statusCode ) == false )
                return false;
            _statusCode = static_cast<int32>( statusCode );
        }
        else
        {
            const string_view method      = firstLine.substr( 0, space );
            const size_t      secondSpace = firstLine.find( ' ', space + 1 );
            if ( secondSpace == string_view::npos || firstLine.substr( secondSpace + 1, 7 ) != "HTTP/1." )
                return false;
            if ( method == "GET" )
                _method = HTTPMethod::Get;
            else if ( method == "POST" )
                _method = HTTPMethod::Post;
            else
                return false;
            _target                   = string( firstLine.substr( space + 1, secondSpace - space - 1 ) );
            const bool bInvalidTarget = _target.empty() || _target.front() != '/' || static_cast<int32>( _target.size() ) > HTTPTypesInternal::kMaxTargetBytes;
            if ( bInvalidTarget )
                return false;
        }
        while ( rest.empty() == false )
        {
            const size_t      lineEnd = rest.find( "\r\n" );
            const string_view line    = rest.substr( 0, lineEnd );
            rest                      = lineEnd == string_view::npos ? string_view{} : rest.substr( lineEnd + 2 );
            if ( line.empty() )
                continue;
            string_view name;
            string_view value;
            if ( StringUtil::trySplitKeyValue( line, name, value, ':' ) == false )
                return false;
            HTTPHeader& header = _listHeader.emplace_back();
            header._name       = string( name );
            header._value      = string( value );
        }
        return true;
    }

    bool HTTPMessageParser::advanceHead()
    {
        size_t headEnd = string::npos;
        for ( size_t index = 0; index + 3 < _buffer.size(); ++index )
        {
            const bool bHeaderTerminator = _buffer[index] == '\r' && _buffer[index + 1] == '\n' && _buffer[index + 2] == '\r' && _buffer[index + 3] == '\n';
            if ( bHeaderTerminator )
            {
                headEnd = index;
                break;
            }
        }
        // 머리 끝이 한 번에 들어와도 상한을 본다 — 끝을 아직 못 찾은 경우만 보면 큰 머리 하나를 통째로 보낸 상대가 지나간다.
        const size_t headSize = headEnd == string::npos ? _buffer.size() : headEnd;
        if ( headSize > static_cast<size_t>( HTTPConstant::kMaxHeaderBytes ) )
        {
            fail( "header is too large" );
            return false;
        }
        if ( headEnd == string::npos )
            return false;
        const string_view head( reinterpret_cast<const utf8*>( _buffer.data() ), headEnd );
        if ( parseHead( head ) == false )
        {
            fail( "malformed head" );
            return false;
        }
        _buffer.erase( _buffer.begin(), _buffer.begin() + static_cast<ptrdiff_t>( headEnd + 4 ) );
        const HTTPHeader* pEncoding = HTTPUtil::findHeader( _listHeader, "Transfer-Encoding" );
        const HTTPHeader* pLength   = HTTPUtil::findHeader( _listHeader, "Content-Length" );
        if ( pEncoding != nullptr && HTTPUtil::isEqualIgnoreCase( pEncoding->_value, "chunked" ) )
        {
            _phase = Phase::ChunkSize;
        }
        else if ( pEncoding != nullptr )
        {
            fail( "unsupported transfer encoding" );
            return false;
        }
        else if ( pLength != nullptr )
        {
            if ( HTTPTypesInternal::parseUnsigned( pLength->_value, 10, _remainingBytes ) == false )
            {
                fail( "malformed Content-Length" );
                return false;
            }
            // 값이 다른 Content-Length 가 둘이면 앞단과 길이를 다르게 고를 수 있다(요청 밀반입, RFC 9112 6.3) — 같은 값의 반복만 받는다.
            for ( const HTTPHeader& header : _listHeader )
            {
                if ( HTTPUtil::isEqualIgnoreCase( header._name, "Content-Length" ) && header._value != pLength->_value )
                {
                    fail( "conflicting Content-Length" );
                    return false;
                }
            }
            if ( _remainingBytes > _maxBodyBytes )
            {
                fail( "body is too large" );
                return false;
            }
            _phase = Phase::FixedBody;
        }
        else
        {
            // 요청은 길이가 없으면 몸이 없다. 응답은 닫힐 때까지가 몸이다(1xx, 204, 304 는 쓰지 않는다).
            _phase = _kind == HTTPMessageKind::Request ? Phase::Done : Phase::UntilClose;
        }
        return true;
    }

    bool HTTPMessageParser::takeBodyBytes( Phase nextPhase )
    {
        const size_t take = MathUtil::min( _buffer.size(), static_cast<size_t>( _remainingBytes ) );
        _bodyBytes.insert( _bodyBytes.end(), _buffer.begin(), _buffer.begin() + static_cast<ptrdiff_t>( take ) );
        _buffer.erase( _buffer.begin(), _buffer.begin() + static_cast<ptrdiff_t>( take ) );
        _remainingBytes -= static_cast<int64>( take );
        if ( _remainingBytes > 0 )
            return false;
        _phase = nextPhase;
        return true;
    }

    bool HTTPMessageParser::advanceChunkSize()
    {
        using Internal       = HTTPTypesInternal;
        const size_t lineEnd = Internal::findLineEnd( _buffer, 0 );
        if ( lineEnd == string::npos )
        {
            if ( static_cast<int32>( _buffer.size() ) > Internal::kMaxChunkLine )
                fail( "malformed chunk size" );
            return false;
        }
        string_view sizeText( reinterpret_cast<const utf8*>( _buffer.data() ), lineEnd );
        sizeText = sizeText.substr( 0, sizeText.find( ';' ) ); // 확장 무시
        if ( Internal::parseUnsigned( StringUtil::trim( sizeText ), 16, _remainingBytes ) == false || _remainingBytes > Internal::kMaxChunkSize )
        {
            fail( "malformed chunk size" );
            return false;
        }
        _buffer.erase( _buffer.begin(), _buffer.begin() + static_cast<ptrdiff_t>( lineEnd + 2 ) );
        if ( static_cast<int64>( _bodyBytes.size() ) + _remainingBytes > _maxBodyBytes )
        {
            fail( "body is too large" );
            return false;
        }
        _phase = _remainingBytes == 0 ? Phase::Trailer : Phase::ChunkData;
        return true;
    }

    HTTPParseState HTTPMessageParser::advance()
    {
        bool bProgress = true;
        while ( _state == HTTPParseState::NeedMore && bProgress )
        {
            switch ( _phase )
            {
                case Phase::Head:
                {
                    bProgress = advanceHead();
                    break;
                }
                case Phase::FixedBody:
                {
                    bProgress = takeBodyBytes( Phase::Done );
                    break;
                }
                case Phase::ChunkSize:
                {
                    bProgress = advanceChunkSize();
                    break;
                }
                case Phase::ChunkData:
                {
                    bProgress = takeBodyBytes( Phase::ChunkDataEnd );
                    break;
                }
                case Phase::ChunkDataEnd:
                {
                    if ( _buffer.size() < 2 )
                    {
                        bProgress = false;
                        break;
                    }
                    if ( _buffer[0] != '\r' || _buffer[1] != '\n' )
                        return fail( "chunk is not terminated" );
                    _buffer.erase( _buffer.begin(), _buffer.begin() + 2 );
                    _phase = Phase::ChunkSize;
                    break;
                }
                case Phase::Trailer:
                {
                    const size_t lineEnd = HTTPTypesInternal::findLineEnd( _buffer, 0 );
                    if ( lineEnd == string::npos )
                    {
                        if ( static_cast<int32>( _buffer.size() ) > HTTPConstant::kMaxHeaderBytes )
                            return fail( "trailer is too large" );
                        bProgress = false;
                        break;
                    }
                    _buffer.erase( _buffer.begin(), _buffer.begin() + static_cast<ptrdiff_t>( lineEnd + 2 ) );
                    if ( lineEnd == 0 )
                        _phase = Phase::Done;
                    break;
                }
                case Phase::UntilClose:
                {
                    if ( static_cast<int64>( _bodyBytes.size() + _buffer.size() ) > _maxBodyBytes )
                        return fail( "body is too large" );
                    _bodyBytes.insert( _bodyBytes.end(), _buffer.begin(), _buffer.end() );
                    _buffer.clear();
                    bProgress = false;
                    break;
                }
                case Phase::Done:
                {
                    _buffer.clear();
                    _state = HTTPParseState::Complete;
                    break;
                }
            }
        }
        return _state;
    }

    void HTTPWriteUtil::writeRequest( const HTTPClientRequest& request, const HTTPAddress& url, vector<uint8>& outBytes )
    {
        using Internal = HTTPTypesInternal;
        outBytes.clear();
        Internal::appendText( outBytes, toString( request._method ) );
        Internal::appendText( outBytes, " " );
        Internal::appendText( outBytes, url._target );
        Internal::appendText( outBytes, " HTTP/1.1\r\n" );
        string host = url._host;
        if ( ( url._bSecure == SW_TRUE && url._port != 443 ) || ( url._bSecure == SW_FALSE && url._port != 80 ) )
            host += formatToString( ":%#", static_cast<int32>( url._port ) );
        Internal::appendHeader( outBytes, "Host", host );
        Internal::appendHeader( outBytes, "Connection", "close" );
        if ( request._method == HTTPMethod::Post || request._bodyBytes.empty() == false )
            Internal::appendHeader( outBytes, "Content-Length", to_string( static_cast<int64>( request._bodyBytes.size() ) ) );
        for ( const HTTPHeader& header : request._listHeader )
        {
            const bool bOwned = HTTPUtil::isEqualIgnoreCase( header._name, "Host" ) || HTTPUtil::isEqualIgnoreCase( header._name, "Connection" ) ||
                                HTTPUtil::isEqualIgnoreCase( header._name, "Content-Length" );
            if ( bOwned == false )
                Internal::appendHeader( outBytes, header._name, header._value );
        }
        Internal::appendText( outBytes, "\r\n" );
        outBytes.insert( outBytes.end(), request._bodyBytes.begin(), request._bodyBytes.end() );
    }

    void HTTPWriteUtil::writeResponse( const HTTPServerResponse& response, vector<uint8>& outBytes )
    {
        using Internal = HTTPTypesInternal;
        outBytes.clear();
        Internal::appendText( outBytes, "HTTP/1.1 " );
        Internal::appendText( outBytes, to_string( response._statusCode ) );
        Internal::appendText( outBytes, " " );
        Internal::appendText( outBytes, getReasonPhrase( response._statusCode ) );
        Internal::appendText( outBytes, "\r\n" );
        Internal::appendHeader( outBytes, "Connection", "close" );
        if ( response._bChunked == SW_TRUE )
            Internal::appendHeader( outBytes, "Transfer-Encoding", "chunked" );
        else
            Internal::appendHeader( outBytes, "Content-Length", to_string( static_cast<int64>( response._bodyBytes.size() ) ) );
        for ( const HTTPHeader& header : response._listHeader )
        {
            Internal::appendHeader( outBytes, header._name, header._value );
        }
        Internal::appendText( outBytes, "\r\n" );
        if ( response._bChunked == SW_FALSE )
        {
            outBytes.insert( outBytes.end(), response._bodyBytes.begin(), response._bodyBytes.end() );
            return;
        }
        // 몸을 7 바이트 조각으로 — 받는 쪽이 조각 경계를 여러 번 지나게.
        static constexpr size_t kChunkBytes = 7;
        for ( size_t offset = 0; offset < response._bodyBytes.size(); offset += kChunkBytes )
        {
            const size_t take = MathUtil::min( kChunkBytes, response._bodyBytes.size() - offset );
            Internal::appendText( outBytes, Internal::makeHex( take ) );
            Internal::appendText( outBytes, "\r\n" );
            outBytes.insert( outBytes.end(), response._bodyBytes.begin() + static_cast<ptrdiff_t>( offset ), response._bodyBytes.begin() + static_cast<ptrdiff_t>( offset + take ) );
            Internal::appendText( outBytes, "\r\n" );
        }
        Internal::appendText( outBytes, "0\r\n\r\n" );
    }

    const utf8* HTTPWriteUtil::getReasonPhrase( int32 statusCode )
    {
        switch ( statusCode )
        {
            case 200:
                return "OK";
            case 302:
                return "Found";
            case 400:
                return "Bad Request";
            case 401:
                return "Unauthorized";
            case 403:
                return "Forbidden";
            case 404:
                return "Not Found";
            case 500:
                return "Internal Server Error";
            case 503:
                return "Service Unavailable";
            default:
                return "Status";
        }
    }
} // namespace sw
