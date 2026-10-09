#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/CacheStore/Server/Driver/Resp/RespCodec.h"

#include "Core/String/StringUtil.h"

#include <charconv>
#include <cmath>

namespace sw
{
    namespace
    {
        struct RespCodecInternal
        {
            static constexpr int64   kMaxExactScore      = 1ll << 53;
            static constexpr float64 kMaxExactScoreFloat = 9007199254740992.0;

            static void appendDecimal( vector<uint8>& outBytes, int64 value )
            {
                utf8                       arrBuffer[constant::kMaxBuffer32];
                const std::to_chars_result result = std::to_chars( arrBuffer, arrBuffer + constant::kMaxBuffer32, value );
                outBytes.insert( outBytes.end(), arrBuffer, result.ptr );
            }

            static void appendCrlf( vector<uint8>& outBytes )
            {
                outBytes.push_back( '\r' );
                outBytes.push_back( '\n' );
            }

            /** @brief RESP 길이 · 정수 줄을 읽습니다(부호 · 숫자만 — `StringUtil` 보다 엄격하다). */
            [[nodiscard]] static bool parseLineInteger( const uint8* pBegin, const uint8* pEnd, int64& outValue )
            {
                const utf8* pTextBegin = reinterpret_cast<const utf8*>( pBegin );
                const utf8* pTextEnd   = reinterpret_cast<const utf8*>( pEnd );
                if ( pTextBegin == pTextEnd )
                    return false;
                const std::from_chars_result result = std::from_chars( pTextBegin, pTextEnd, outValue );
                return result.ec == std::errc{} && result.ptr == pTextEnd;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string_view RespValue::getText() const { return string_view{ reinterpret_cast<const utf8*>( _bytes.data() ), _bytes.size() }; }

    bool RespValue::isText( string_view text ) const
    {
        const bool bTextual = _type == RespType::SimpleString || _type == RespType::BulkString;
        return bTextual && getText() == text;
    }

    bool RespValue::tryReadIntegerText( int64& outValue ) const
    {
        if ( _type == RespType::Integer )
        {
            outValue = _integer;
            return true;
        }
        if ( _type != RespType::BulkString && _type != RespType::SimpleString )
            return false;
        const string_view text         = getText();
        int64             integerValue = 0;
        if ( StringUtil::parseInt64( text, integerValue ) )
        {
            if ( integerValue < -RespCodecInternal::kMaxExactScore || RespCodecInternal::kMaxExactScore < integerValue )
                return false;
            outValue = integerValue;
            return true;
        }
        // 서버는 점수를 배정밀도 글로 준다("1e+16" · "2.5") — 정수이고 정확한 범위 안일 때만 받는다.
        float64 floatValue = 0.0;
        if ( StringUtil::parseDouble( text, floatValue ) == false || std::isfinite( floatValue ) == false )
            return false;
        const bool bInRange = -RespCodecInternal::kMaxExactScoreFloat <= floatValue && floatValue <= RespCodecInternal::kMaxExactScoreFloat;
        if ( bInRange == false || std::floor( floatValue ) != floatValue )
            return false;
        outValue = static_cast<int64>( floatValue );
        return true;
    }
} // namespace sw

namespace sw
{
    RespCommand::RespCommand( string_view name )
        : _argumentBytes{}
        , _argumentCount{ 0 }
    {
        addText( name );
    }

    RespCommand& RespCommand::addText( string_view text ) { return addBytes( reinterpret_cast<const uint8*>( text.data() ), text.size() ); }

    RespCommand& RespCommand::addBytes( const uint8* pData, size_t size )
    {
        _argumentBytes.push_back( '$' );
        RespCodecInternal::appendDecimal( _argumentBytes, static_cast<int64>( size ) );
        RespCodecInternal::appendCrlf( _argumentBytes );
        if ( size > 0 )
            _argumentBytes.insert( _argumentBytes.end(), pData, pData + size );
        RespCodecInternal::appendCrlf( _argumentBytes );
        ++_argumentCount;
        return *this;
    }

    RespCommand& RespCommand::addInteger( int64 value )
    {
        utf8                       arrBuffer[constant::kMaxBuffer32];
        const std::to_chars_result result = std::to_chars( arrBuffer, arrBuffer + constant::kMaxBuffer32, value );
        return addText( string_view{ arrBuffer, static_cast<size_t>( result.ptr - arrBuffer ) } );
    }

    void RespCommand::appendTo( vector<uint8>& outBytes ) const
    {
        outBytes.push_back( '*' );
        RespCodecInternal::appendDecimal( outBytes, _argumentCount );
        RespCodecInternal::appendCrlf( outBytes );
        outBytes.insert( outBytes.end(), _argumentBytes.begin(), _argumentBytes.end() );
    }
} // namespace sw

namespace sw
{
    RespParser::RespParser()
        : _buffer{}
        , _pFailureText{ "" }
        , _readOffset{ 0 }
        , _bFailed{ SW_FALSE }
    {
    }

    void RespParser::append( const uint8* pData, size_t size )
    {
        if ( _bFailed == SW_TRUE || size == 0 )
            return;
        // 앞쪽에 다 읽은 바이트가 쌓이면 당겨 온다(버퍼가 끝없이 자라지 않게).
        const bool bShouldCompact = _readOffset > 4096 && _readOffset * 2 > _buffer.size();
        if ( bShouldCompact )
        {
            _buffer.erase( _buffer.begin(), _buffer.begin() + static_cast<ptrdiff_t>( _readOffset ) );
            _readOffset = 0;
        }
        _buffer.insert( _buffer.end(), pData, pData + size );
    }

    RespParseResult RespParser::next( RespValue& outValue )
    {
        if ( _bFailed == SW_TRUE )
            return RespParseResult::Error;
        size_t                offset = _readOffset;
        const RespParseResult result = parseValue( offset, 0, outValue );
        if ( result == RespParseResult::Complete )
            _readOffset = offset;
        if ( _readOffset == _buffer.size() )
        {
            _buffer.clear();
            _readOffset = 0;
        }
        return result;
    }

    void RespParser::reset()
    {
        _buffer.clear();
        _pFailureText = "";
        _readOffset   = 0;
        _bFailed      = SW_FALSE;
    }

    RespParseResult RespParser::parseValue( size_t& inoutOffset, int32 depth, RespValue& outValue )
    {
        if ( depth > kMaxNestedDepth )
            return fail( "RESP value nests too deep" );
        size_t                lineBegin  = 0;
        size_t                lineEnd    = 0;
        const RespParseResult lineResult = findLine( inoutOffset, lineBegin, lineEnd );
        if ( lineResult != RespParseResult::Complete )
            return lineResult;
        const uint8  marker    = _buffer[lineBegin];
        const uint8* pLineBody = _buffer.data() + lineBegin + 1;
        const uint8* pLineEnd  = _buffer.data() + lineEnd;
        const size_t afterLine = lineEnd + 2;
        outValue._bytes.clear();
        outValue._listElement.clear();
        outValue._integer = 0;
        switch ( marker )
        {
            case '+':
            case '-':
            {
                outValue._type = marker == '+' ? RespType::SimpleString : RespType::Error;
                outValue._bytes.assign( pLineBody, pLineEnd );
                inoutOffset = afterLine;
                return RespParseResult::Complete;
            }
            case ':':
            {
                if ( RespCodecInternal::parseLineInteger( pLineBody, pLineEnd, outValue._integer ) == false )
                    return fail( "RESP integer is not a number" );
                outValue._type = RespType::Integer;
                inoutOffset    = afterLine;
                return RespParseResult::Complete;
            }
            case '$':
            {
                int64 size = 0;
                if ( RespCodecInternal::parseLineInteger( pLineBody, pLineEnd, size ) == false || size < -1 )
                    return fail( "RESP bulk length is malformed" );
                if ( size == -1 )
                {
                    outValue._type = RespType::Null;
                    inoutOffset    = afterLine;
                    return RespParseResult::Complete;
                }
                if ( size > kMaxBulkSize )
                    return fail( "RESP bulk string exceeds the size limit" );
                const size_t bodyEnd = afterLine + static_cast<size_t>( size );
                if ( _buffer.size() < bodyEnd + 2 )
                    return RespParseResult::NeedMore;
                if ( _buffer[bodyEnd] != '\r' || _buffer[bodyEnd + 1] != '\n' )
                    return fail( "RESP bulk string is not terminated by CRLF" );
                outValue._type = RespType::BulkString;
                outValue._bytes.assign( _buffer.begin() + static_cast<ptrdiff_t>( afterLine ), _buffer.begin() + static_cast<ptrdiff_t>( bodyEnd ) );
                inoutOffset = bodyEnd + 2;
                return RespParseResult::Complete;
            }
            case '*':
            {
                int64 count = 0;
                if ( RespCodecInternal::parseLineInteger( pLineBody, pLineEnd, count ) == false || count < -1 )
                    return fail( "RESP array length is malformed" );
                if ( count == -1 )
                {
                    outValue._type = RespType::Null;
                    inoutOffset    = afterLine;
                    return RespParseResult::Complete;
                }
                if ( count > kMaxArrayCount )
                    return fail( "RESP array exceeds the element limit" );
                vector<RespValue> listElement;
                listElement.resize( static_cast<size_t>( count ) );
                size_t elementOffset = afterLine;
                for ( RespValue& element : listElement )
                {
                    const RespParseResult elementResult = parseValue( elementOffset, depth + 1, element );
                    if ( elementResult != RespParseResult::Complete )
                        return elementResult;
                }
                outValue._type        = RespType::Array;
                outValue._listElement = std::move( listElement );
                inoutOffset           = elementOffset;
                return RespParseResult::Complete;
            }
            default:
            {
                return fail( "RESP value has an unknown type marker" );
            }
        }
    }

    RespParseResult RespParser::findLine( size_t offset, size_t& outLineBegin, size_t& outLineEnd )
    {
        const size_t limit = _buffer.size();
        for ( size_t index = offset; index + 1 < limit; ++index )
        {
            if ( _buffer[index] != '\r' )
                continue;
            if ( _buffer[index + 1] != '\n' )
                return fail( "RESP line has a CR without LF" );
            if ( index == offset )
                return fail( "RESP line is empty" );
            outLineBegin = offset;
            outLineEnd   = index;
            return RespParseResult::Complete;
        }
        if ( static_cast<int64>( limit - offset ) > kMaxLineSize )
            return fail( "RESP line exceeds the size limit" );
        return RespParseResult::NeedMore;
    }

    RespParseResult RespParser::fail( const utf8* pFailureText )
    {
        _bFailed      = SW_TRUE;
        _pFailureText = pFailureText;
        return RespParseResult::Error;
    }
} // namespace sw
