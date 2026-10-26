#include "pch.h"

#include "Engine/Serialization/JSON/JSONWriter.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include <charconv>
#include <cmath>
#include <cstring>

namespace sw
{
    namespace
    {
        struct JSONWriterInternal
        {
            /** @brief 수 한 개의 최대 글자 수입니다(고정 소수: 정수부 20 자 + `.` + 소수 18 자, 실수: 가장 짧은 왕복 24 자 + `.0`). */
            static constexpr uint32 kNumberBufferSize = 48;
            /** @brief 고정 소수 자리의 한도입니다. 10^19 는 uint64 를 넘는다. */
            static constexpr uint32 kMaxFractionDigits = 18;
            /** @brief 잘못된 UTF-8 바이트를 대신하는 글자(U+FFFD)입니다. */
            static constexpr string_view kReplacement = "\\ufffd";

            /** @brief 따로 적는 이스케이프가 있는 제어 문자이면 그 글자(`n` 등), 없으면 0 입니다. */
            static utf8 getShortEscape( utf8 character )
            {
                switch ( character )
                {
                    case '\b':
                        return 'b';
                    case '\f':
                        return 'f';
                    case '\n':
                        return 'n';
                    case '\r':
                        return 'r';
                    case '\t':
                        return 't';
                    default:
                        return 0;
                }
            }

            static uint64 getPowerOfTen( uint32 exponent )
            {
                uint64 power = 1;
                for ( uint32 index = 0; index < exponent; ++index )
                {
                    power *= 10;
                }
                return power;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    JSONWriter::JSONWriter( int32 indent )
        : _ownedText{}
        , _pString{ &_ownedText }
        , _pBuffer{ nullptr }
        , _capacity{ 0 }
        , _indent{ indent }
        , _writtenSize{ 0 }
        , _arrScope{}
        , _depth{ 0 }
        , _bAfterKey{ false }
        , _bRootWritten{ false }
        , _bFailed{ false }
    {
    }

    JSONWriter::JSONWriter( string& outJSON, int32 indent )
        : _ownedText{}
        , _pString{ &outJSON }
        , _pBuffer{ nullptr }
        , _capacity{ 0 }
        , _indent{ indent }
        , _writtenSize{ 0 }
        , _arrScope{}
        , _depth{ 0 }
        , _bAfterKey{ false }
        , _bRootWritten{ false }
        , _bFailed{ false }
    {
    }

    JSONWriter::JSONWriter( utf8* pBuffer, uint32 capacity, int32 indent )
        : _ownedText{}
        , _pString{ nullptr }
        , _pBuffer{ pBuffer }
        , _capacity{ capacity }
        , _indent{ indent }
        , _writtenSize{ 0 }
        , _arrScope{}
        , _depth{ 0 }
        , _bAfterKey{ false }
        , _bRootWritten{ false }
        , _bFailed{ false }
    {
        SW_ASSERT( pBuffer != nullptr && capacity > 0 );
        if ( _pBuffer != nullptr && _capacity > 0 )
            _pBuffer[0] = '\0';
        else
            _bFailed = true;
    }

    void JSONWriter::beginObject()
    {
        if ( prepareValue() == false )
            return;
        if ( _depth >= kMaxDepth )
        {
            fail();
            return;
        }
        appendChar( '{' );
        _arrScope[_depth++] = Scope{ true, false };
    }

    void JSONWriter::endObject()
    {
        closeScope( true, '}' );
    }

    void JSONWriter::beginArray()
    {
        if ( prepareValue() == false )
            return;
        if ( _depth >= kMaxDepth )
        {
            fail();
            return;
        }
        appendChar( '[' );
        _arrScope[_depth++] = Scope{ false, false };
    }

    void JSONWriter::endArray()
    {
        closeScope( false, ']' );
    }

    void JSONWriter::key( string_view name )
    {
        const bool bInObject = _depth > 0 && _arrScope[_depth - 1]._bObject;
        if ( bInObject == false || _bAfterKey )
        {
            fail();
            return;
        }
        writeItemSeparator();
        appendChar( '"' );
        appendEscaped( name );
        append( _indent >= 0 ? string_view{ "\": " } : string_view{ "\":" } );
        _bAfterKey = true;
    }

    void JSONWriter::valueString( string_view value )
    {
        if ( prepareValue() == false )
            return;
        appendChar( '"' );
        appendEscaped( value );
        appendChar( '"' );
    }

    void JSONWriter::valueBool( bool value )
    {
        if ( prepareValue() == false )
            return;
        append( value ? string_view{ "true" } : string_view{ "false" } );
    }

    void JSONWriter::valueInt( int64 value )
    {
        if ( prepareValue() == false )
            return;
        utf8                       arrText[JSONWriterInternal::kNumberBufferSize];
        const std::to_chars_result result = std::to_chars( arrText, arrText + JSONWriterInternal::kNumberBufferSize, value );
        append( string_view{ arrText, static_cast<size_t>( result.ptr - arrText ) } );
    }

    void JSONWriter::valueUint( uint64 value )
    {
        if ( prepareValue() == false )
            return;
        utf8                       arrText[JSONWriterInternal::kNumberBufferSize];
        const std::to_chars_result result = std::to_chars( arrText, arrText + JSONWriterInternal::kNumberBufferSize, value );
        append( string_view{ arrText, static_cast<size_t>( result.ptr - arrText ) } );
    }

    void JSONWriter::valueFloat( float64 value )
    {
        if ( prepareValue() == false )
            return;
        if ( std::isfinite( value ) == false )
        {
            // JSON 에는 NaN · 무한대가 없다 — 구조는 지키려고 null 을 쓰되 실패로 남겨 부르는 쪽이 알게 한다.
            append( "null" );
            _bFailed = true;
            return;
        }
        utf8                       arrText[JSONWriterInternal::kNumberBufferSize];
        const std::to_chars_result result = std::to_chars( arrText, arrText + JSONWriterInternal::kNumberBufferSize, value );
        const string_view          text{ arrText, static_cast<size_t>( result.ptr - arrText ) };
        append( text );
        // 정수처럼 보이는 실수는 `.0` 을 붙여 다시 읽을 때도 실수로 남긴다(nlohmann `dump` 와 같다).
        if ( text.find_first_of( ".e" ) == string_view::npos )
            append( ".0" );
    }

    void JSONWriter::valueNull()
    {
        if ( prepareValue() == false )
            return;
        append( "null" );
    }

    void JSONWriter::valueFixedPoint( uint64 scaled, uint32 fractionDigits )
    {
        using Internal = JSONWriterInternal;
        if ( fractionDigits > Internal::kMaxFractionDigits )
        {
            fail();
            return;
        }
        if ( prepareValue() == false )
            return;
        const uint64 divisor = Internal::getPowerOfTen( fractionDigits );
        utf8         arrText[Internal::kNumberBufferSize];
        utf8*        pEnd = std::to_chars( arrText, arrText + Internal::kNumberBufferSize, scaled / divisor ).ptr;
        if ( fractionDigits > 0 )
        {
            *pEnd++         = '.';
            uint64 fraction = scaled % divisor;
            for ( uint32 index = fractionDigits; index > 0; --index )
            {
                pEnd[index - 1] = static_cast<utf8>( '0' + fraction % 10 );
                fraction /= 10;
            }
            pEnd += fractionDigits;
        }
        append( string_view{ arrText, static_cast<size_t>( pEnd - arrText ) } );
    }

    void JSONWriter::valueJSON( const JSONValue& value )
    {
        if ( prepareValue() == false )
            return;
        append( value.dump() );
    }

    bool JSONWriter::isComplete() const
    {
        return _bFailed == false && _bRootWritten && _depth == 0;
    }

    std::optional<JSONText> JSONWriter::finish()
    {
        const bool bOwnsBuffer = _pString == &_ownedText;
        SW_ASSERT( bOwnsBuffer );
        if ( bOwnsBuffer == false || isComplete() == false )
            return std::nullopt;
        _bFailed = true; // 꺼낸 뒤에는 쓰지 않는다 — 이어 쓰면 빈 버퍼에 조각이 생긴다
        return JSONText{ std::move( _ownedText ) };
    }

    void JSONWriter::append( string_view text )
    {
        if ( _pString != nullptr )
        {
            _pString->append( text );
            _writtenSize += text.size();
            return;
        }
        if ( _pBuffer == nullptr || _capacity == 0 )
            return;
        const size_t room  = _capacity - 1 - _writtenSize;
        const size_t count = text.size() < room ? text.size() : room;
        std::memcpy( _pBuffer + _writtenSize, text.data(), count );
        _writtenSize += count;
        _pBuffer[_writtenSize] = '\0';
        if ( count < text.size() )
            _bFailed = true; // 넘쳤다 — 잘린 글은 올바른 JSON 이 아니다
    }

    void JSONWriter::appendChar( utf8 character )
    {
        append( string_view{ &character, 1 } );
    }

    void JSONWriter::appendEscaped( string_view text )
    {
        using Internal                   = JSONWriterInternal;
        constexpr string_view kHexDigits = "0123456789abcdef";
        size_t                runBegin   = 0;
        size_t                offset     = 0;
        while ( offset < text.size() )
        {
            const uint8 byte   = static_cast<uint8>( text[offset] );
            const bool  bPlain = byte >= 0x20 && byte < 0x80 && byte != '"' && byte != '\\';
            if ( bPlain )
            {
                ++offset;
                continue;
            }
            if ( byte >= 0x80 )
            {
                // 여러 바이트 글자: 올바르면 그대로 둔 채 지나가고, 잘못된 바이트만 U+FFFD 로 바꾼다.
                const size_t charBegin = offset;
                uint32       codepoint = 0;
                if ( StringUtil::tryDecodeUtf8( text, offset, codepoint ) )
                    continue;
                append( text.substr( runBegin, charBegin - runBegin ) );
                append( Internal::kReplacement );
                runBegin = offset;
                continue;
            }
            append( text.substr( runBegin, offset - runBegin ) );
            const utf8 shortEscape = Internal::getShortEscape( static_cast<utf8>( byte ) );
            if ( byte == '"' || byte == '\\' )
            {
                const utf8 arrEscape[2]{ '\\', static_cast<utf8>( byte ) };
                append( string_view{ arrEscape, 2 } );
            }
            else if ( shortEscape != 0 )
            {
                const utf8 arrEscape[2]{ '\\', shortEscape };
                append( string_view{ arrEscape, 2 } );
            }
            else
            {
                const utf8 arrEscape[6]{ '\\', 'u', '0', '0', kHexDigits[byte >> 4], kHexDigits[byte & 0xF] };
                append( string_view{ arrEscape, 6 } );
            }
            ++offset;
            runBegin = offset;
        }
        append( text.substr( runBegin ) );
    }

    void JSONWriter::appendNewLine( uint32 depth )
    {
        constexpr string_view kSpaces = "                                ";
        appendChar( '\n' );
        size_t remaining = static_cast<size_t>( _indent ) * depth;
        while ( remaining > 0 )
        {
            const size_t count = remaining < kSpaces.size() ? remaining : kSpaces.size();
            append( kSpaces.substr( 0, count ) );
            remaining -= count;
        }
    }

    void JSONWriter::writeItemSeparator()
    {
        Scope& scope = _arrScope[_depth - 1];
        if ( scope._bHasItem )
            appendChar( ',' );
        scope._bHasItem = true;
        if ( _indent >= 0 )
            appendNewLine( _depth );
    }

    bool JSONWriter::prepareValue()
    {
        if ( _depth == 0 )
        {
            if ( _bRootWritten )
            {
                fail();
                return false;
            }
            _bRootWritten = true;
            return true;
        }
        if ( _arrScope[_depth - 1]._bObject )
        {
            if ( _bAfterKey == false )
            {
                fail();
                return false;
            }
            _bAfterKey = false;
            return true;
        }
        writeItemSeparator();
        return true;
    }

    void JSONWriter::closeScope( bool bObject, utf8 closing )
    {
        const bool bMatches = _depth > 0 && _arrScope[_depth - 1]._bObject == bObject && _bAfterKey == false;
        if ( bMatches == false )
        {
            fail();
            return;
        }
        const bool bHadItem = _arrScope[_depth - 1]._bHasItem;
        --_depth;
        if ( bHadItem && _indent >= 0 )
            appendNewLine( _depth );
        appendChar( closing );
    }

    void JSONWriter::fail()
    {
        SW_ASSERT( false && "JSONWriter: call order does not form valid JSON" );
        _bFailed = true;
    }
} // namespace sw
