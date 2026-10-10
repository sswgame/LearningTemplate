#include "pch.h"

#include "GameFramework/Base/Online/HTTP/HTTPTypes.h"

#include "Core/Container/StringUtil.h"

namespace sw
{
    namespace
    {
        struct HTTPTypesInternal
        {
            static constexpr utf8  kHexDigit[]     = "0123456789ABCDEF";
            static constexpr int32 kMaxChunkLine   = 64;
            static constexpr int64 kMaxChunkSize   = 64ll * 1024 * 1024;
            static constexpr int32 kMaxTargetBytes = 8192;

            static bool isUnreserved( utf8 ch )
            {
                const bool bLetter = ( 'a' <= ch && ch <= 'z' ) || ( 'A' <= ch && ch <= 'Z' );
                const bool bDigit  = '0' <= ch && ch <= '9';
                return bLetter || bDigit || ch == '-' || ch == '.' || ch == '_' || ch == '~';
            }

            static int32 findHexValue( utf8 ch )
            {
                if ( '0' <= ch && ch <= '9' )
                    return ch - '0';
                if ( 'a' <= ch && ch <= 'f' )
                    return ch - 'a' + 10;
                if ( 'A' <= ch && ch <= 'F' )
                    return ch - 'A' + 10;
                return -1;
            }

            /** @brief @p bytes 에서 "\r\n" 의 자리입니다(없으면 npos). */
            static size_t findLineEnd( const vector<uint8>& bytes, size_t from )
            {
                for ( size_t index = from; index + 1 < bytes.size(); ++index )
                {
                    if ( bytes[index] == '\r' && bytes[index + 1] == '\n' )
                        return index;
                }
                return string::npos;
            }

            static void   appendText( vector<uint8>& outBytes, string_view text ) { outBytes.insert( outBytes.end(), text.begin(), text.end() ); }
            static string makeHex( size_t value )
            {
                string text;
                do
                {
                    text.insert( text.begin(), kHexDigit[value & 0xFu] );
                    value >>= 4;
                } while ( value != 0 );
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

    const HTTPHeader* HTTPClientResponse::findHeader( string_view name ) const { return HTTPUtil::findHeader( _listHeader, name ); }

    const HTTPHeader* HTTPServerRequest::findHeader( string_view name ) const { return HTTPUtil::findHeader( _listHeader, name ); }

    const HTTPHeader* HTTPServerRequest::findQuery( string_view name ) const
    {
        for ( const HTTPHeader& pair : _listQuery )
        {
            if ( pair._name == name )
                return &pair;
        }
        return nullptr;
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
            int64 port = 0;
            if ( HTTPTypesInternal::parseUnsigned( authority.substr( colon + 1 ), 10, port ) == false || port <= 0 || port > 65535 )
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
            const uint8 byteValue = static_cast<uint8>( ch );
            encoded.push_back( '%' );
            encoded.push_back( HTTPTypesInternal::kHexDigit[byteValue >> 4] );
            encoded.push_back( HTTPTypesInternal::kHexDigit[byteValue & 0xFu] );
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
        for ( const HTTPHeader& header : listHeader )
        {
            if ( isEqualIgnoreCase( header._name, name ) )
                return &header;
        }
        return nullptr;
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
            _target = string( firstLine.substr( space + 1, secondSpace - space - 1 ) );
            if ( _target.empty() || _target.front() != '/' || static_cast<int32>( _target.size() ) > HTTPTypesInternal::kMaxTargetBytes )
                return false;
        }
        while ( rest.empty() == false )
        {
            const size_t      lineEnd = rest.find( "\r\n" );
            const string_view line    = rest.substr( 0, lineEnd );
            rest                      = lineEnd == string_view::npos ? string_view{} : rest.substr( lineEnd + 2 );
            if ( line.empty() )
                continue;
            const size_t colon = line.find( ':' );
            if ( colon == string_view::npos || colon == 0 )
                return false;
            HTTPHeader& header = _listHeader.emplace_back();
            header._name       = string( StringUtil::trim( line.substr( 0, colon ) ) );
            header._value      = string( StringUtil::trim( line.substr( colon + 1 ) ) );
        }
        return true;
    }

    HTTPParseState HTTPMessageParser::advance()
    {
        using Internal = HTTPTypesInternal;
        while ( _state == HTTPParseState::NeedMore )
        {
            switch ( _phase )
            {
                case Phase::Head:
                {
                    size_t headEnd = string::npos;
                    for ( size_t index = 0; index + 3 < _buffer.size(); ++index )
                    {
                        if ( _buffer[index] == '\r' && _buffer[index + 1] == '\n' && _buffer[index + 2] == '\r' && _buffer[index + 3] == '\n' )
                        {
                            headEnd = index;
                            break;
                        }
                    }
                    if ( headEnd == string::npos )
                    {
                        if ( static_cast<int32>( _buffer.size() ) > HTTPConstant::kMaxHeaderBytes )
                            return fail( "header is too large" );
                        return _state;
                    }
                    const string_view head( reinterpret_cast<const utf8*>( _buffer.data() ), headEnd );
                    if ( parseHead( head ) == false )
                        return fail( "malformed head" );
                    _buffer.erase( _buffer.begin(), _buffer.begin() + static_cast<ptrdiff_t>( headEnd + 4 ) );
                    const HTTPHeader* pEncoding = HTTPUtil::findHeader( _listHeader, "Transfer-Encoding" );
                    const HTTPHeader* pLength   = HTTPUtil::findHeader( _listHeader, "Content-Length" );
                    if ( pEncoding != nullptr && HTTPUtil::isEqualIgnoreCase( pEncoding->_value, "chunked" ) )
                    {
                        _phase = Phase::ChunkSize;
                    }
                    else if ( pEncoding != nullptr )
                    {
                        return fail( "unsupported transfer encoding" );
                    }
                    else if ( pLength != nullptr )
                    {
                        if ( Internal::parseUnsigned( pLength->_value, 10, _remainingBytes ) == false )
                            return fail( "malformed Content-Length" );
                        if ( _remainingBytes > _maxBodyBytes )
                            return fail( "body is too large" );
                        _phase = Phase::FixedBody;
                    }
                    else
                    {
                        // 요청은 길이가 없으면 몸이 없다. 응답은 닫힐 때까지가 몸이다(1xx · 204 · 304 는 쓰지 않는다).
                        _phase = _kind == HTTPMessageKind::Request ? Phase::Done : Phase::UntilClose;
                    }
                    break;
                }
                case Phase::FixedBody:
                {
                    const size_t take = std::min( _buffer.size(), static_cast<size_t>( _remainingBytes ) );
                    _bodyBytes.insert( _bodyBytes.end(), _buffer.begin(), _buffer.begin() + static_cast<ptrdiff_t>( take ) );
                    _buffer.erase( _buffer.begin(), _buffer.begin() + static_cast<ptrdiff_t>( take ) );
                    _remainingBytes -= static_cast<int64>( take );
                    if ( _remainingBytes > 0 )
                        return _state;
                    _phase = Phase::Done;
                    break;
                }
                case Phase::ChunkSize:
                {
                    const size_t lineEnd = Internal::findLineEnd( _buffer, 0 );
                    if ( lineEnd == string::npos )
                    {
                        if ( static_cast<int32>( _buffer.size() ) > Internal::kMaxChunkLine )
                            return fail( "malformed chunk size" );
                        return _state;
                    }
                    string_view sizeText( reinterpret_cast<const utf8*>( _buffer.data() ), lineEnd );
                    sizeText = sizeText.substr( 0, sizeText.find( ';' ) ); // 확장 무시
                    if ( Internal::parseUnsigned( StringUtil::trim( sizeText ), 16, _remainingBytes ) == false || _remainingBytes > Internal::kMaxChunkSize )
                        return fail( "malformed chunk size" );
                    _buffer.erase( _buffer.begin(), _buffer.begin() + static_cast<ptrdiff_t>( lineEnd + 2 ) );
                    if ( static_cast<int64>( _bodyBytes.size() ) + _remainingBytes > _maxBodyBytes )
                        return fail( "body is too large" );
                    _phase = _remainingBytes == 0 ? Phase::Trailer : Phase::ChunkData;
                    break;
                }
                case Phase::ChunkData:
                {
                    const size_t take = std::min( _buffer.size(), static_cast<size_t>( _remainingBytes ) );
                    _bodyBytes.insert( _bodyBytes.end(), _buffer.begin(), _buffer.begin() + static_cast<ptrdiff_t>( take ) );
                    _buffer.erase( _buffer.begin(), _buffer.begin() + static_cast<ptrdiff_t>( take ) );
                    _remainingBytes -= static_cast<int64>( take );
                    if ( _remainingBytes > 0 )
                        return _state;
                    _phase = Phase::ChunkDataEnd;
                    break;
                }
                case Phase::ChunkDataEnd:
                {
                    if ( _buffer.size() < 2 )
                        return _state;
                    if ( _buffer[0] != '\r' || _buffer[1] != '\n' )
                        return fail( "chunk is not terminated" );
                    _buffer.erase( _buffer.begin(), _buffer.begin() + 2 );
                    _phase = Phase::ChunkSize;
                    break;
                }
                case Phase::Trailer:
                {
                    const size_t lineEnd = Internal::findLineEnd( _buffer, 0 );
                    if ( lineEnd == string::npos )
                    {
                        if ( static_cast<int32>( _buffer.size() ) > HTTPConstant::kMaxHeaderBytes )
                            return fail( "trailer is too large" );
                        return _state;
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
                    return _state;
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
            host += ":" + to_string( static_cast<int32>( url._port ) );
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
            const size_t take = std::min( kChunkBytes, response._bodyBytes.size() - offset );
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
