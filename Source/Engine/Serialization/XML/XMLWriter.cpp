#include "pch.h"

#include "Engine/Serialization/XML/XMLWriter.h"

#include "Core/Container/StringUtil.h"

#include <charconv>

namespace sw
{
    namespace
    {
        struct XMLWriterInternal
        {
            /** @brief XML 1.0 이 받지 않는 글자 · 잘못된 UTF-8 바이트 자리에 쓰는 U+FFFD(UTF-8 세 바이트)입니다. */
            static constexpr string_view kReplacement = "\xEF\xBF\xBD";
            /** @brief 정수 하나의 최대 글자 수입니다. */
            static constexpr uint32 kNumberBufferSize = 24;

            static bool isNameStart( uint8 byte )
            {
                const bool bLetter = ( 'a' <= byte && byte <= 'z' ) || ( 'A' <= byte && byte <= 'Z' );
                return bLetter || byte == '_' || byte == ':' || byte >= 0x80;
            }

            static bool isNameChar( uint8 byte )
            {
                const bool bDigit = '0' <= byte && byte <= '9';
                return isNameStart( byte ) || bDigit || byte == '-' || byte == '.';
            }

            /** @brief 원소 · 속성 이름으로 쓸 수 있는 글이면 true 입니다(`"` · 공백이 든 이름으로 속성을 끼워 넣지 못한다). */
            static bool isValidName( string_view name )
            {
                bool bValid = name.empty() == false && isNameStart( static_cast<uint8>( name[0] ) );
                for ( size_t index = 1; bValid && index < name.size(); ++index )
                {
                    bValid = isNameChar( static_cast<uint8>( name[index] ) );
                }
                return bValid;
            }

            /** @brief @p byte 가 이스케이프 없이 그대로 나갈 수 있는 ASCII 이면 true 입니다. */
            static bool isPlain( uint8 byte, bool bAttribute )
            {
                const bool bMarkup = byte == '&' || byte == '<' || byte == '>' || ( bAttribute && byte == '"' );
                if ( byte >= 0x80 || bMarkup )
                    return false;
                if ( byte >= 0x20 )
                    return true;
                const bool bLayout = byte == '\t' || byte == '\n' || byte == '\r';
                return bLayout && bAttribute == false; // 속성 값의 탭 · 줄바꿈은 파서가 공백으로 바꾸므로 문자 참조로 쓴다
            }

            /** @brief 그대로 못 나가는 ASCII 한 바이트의 대신 글입니다. */
            static string_view getEscape( uint8 byte )
            {
                switch ( byte )
                {
                    case '&':
                        return "&amp;";
                    case '<':
                        return "&lt;";
                    case '>':
                        return "&gt;";
                    case '"':
                        return "&quot;";
                    case '\t':
                        return "&#9;";
                    case '\n':
                        return "&#10;";
                    case '\r':
                        return "&#13;";
                    default:
                        return kReplacement; // XML 1.0 에 없는 제어 문자
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    XMLWriter::XMLWriter()
        : _ownedText{}
        , _pString{ &_ownedText }
        , _arrOpen{}
        , _startSize{ 0 }
        , _depth{ 0 }
        , _bStartTagOpen{ false }
        , _bFailed{ false }
    {
    }

    XMLWriter::XMLWriter( string& outXML )
        : _ownedText{}
        , _pString{ &outXML }
        , _arrOpen{}
        , _startSize{ outXML.size() }
        , _depth{ 0 }
        , _bStartTagOpen{ false }
        , _bFailed{ false }
    {
    }

    void XMLWriter::declaration()
    {
        if ( _pString->size() != _startSize )
        {
            fail();
            return;
        }
        append( "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n" );
    }

    void XMLWriter::comment( string_view text )
    {
        const bool bBadComment = text.find( "--" ) != string_view::npos || ( text.empty() == false && text.back() == '-' );
        if ( bBadComment )
        {
            fail();
            return;
        }
        closeStartTag();
        append( "<!-- " );
        append( text ); // 주석 안은 엔티티를 풀지 않는다 — `--` 만 막으면 된다
        append( " -->" );
    }

    void XMLWriter::whitespace( string_view layout )
    {
        if ( layout.find_first_not_of( " \t\r\n" ) != string_view::npos )
        {
            fail();
            return;
        }
        closeStartTag();
        append( layout );
    }

    void XMLWriter::beginElement( string_view name )
    {
        const bool bPlaceable = XMLWriterInternal::isValidName( name ) && _depth < kMaxDepth;
        if ( bPlaceable == false )
        {
            fail();
            return;
        }
        closeStartTag();
        append( "<" );
        _arrOpen[_depth++] = OpenElement{ _pString->size(), static_cast<uint32>( name.size() ) };
        append( name );
        _bStartTagOpen = true;
    }

    void XMLWriter::attribute( string_view name, string_view value )
    {
        const bool bPlaceable = XMLWriterInternal::isValidName( name ) && _bStartTagOpen;
        if ( bPlaceable == false )
        {
            fail();
            return;
        }
        append( " " );
        append( name );
        append( "=\"" );
        appendEscaped( value, true );
        append( "\"" );
    }

    void XMLWriter::attributeInt( string_view name, int64 value )
    {
        utf8                       arrText[XMLWriterInternal::kNumberBufferSize];
        const std::to_chars_result result = std::to_chars( arrText, arrText + XMLWriterInternal::kNumberBufferSize, value );
        attribute( name, string_view{ arrText, static_cast<size_t>( result.ptr - arrText ) } );
    }

    void XMLWriter::text( string_view value )
    {
        if ( _depth == 0 )
        {
            fail();
            return;
        }
        closeStartTag();
        appendEscaped( value, false );
    }

    void XMLWriter::endElement()
    {
        if ( _depth == 0 )
        {
            fail();
            return;
        }
        const OpenElement element = _arrOpen[--_depth];
        if ( _bStartTagOpen )
        {
            append( "/>" );
            _bStartTagOpen = false;
            return;
        }
        // 이름은 버퍼 안의 위치로 기억했다. 자기 버퍼에서 덧붙이므로 먼저 자리를 잡아 덧붙이는 동안 버퍼가 옮겨지지 않게 한다.
        _pString->reserve( _pString->size() + element._nameSize + 3 );
        append( "</" );
        append( string_view{ _pString->data() + element._nameOffset, element._nameSize } );
        append( ">" );
    }

    void XMLWriter::fragment( const XMLWriter& piece )
    {
        const bool bUsable = &piece != this && piece.isComplete() && _depth > 0;
        if ( bUsable == false )
        {
            fail();
            return;
        }
        closeStartTag();
        append( string_view{ *piece._pString }.substr( piece._startSize ) );
    }

    bool XMLWriter::isComplete() const
    {
        return _bFailed == false && _depth == 0;
    }

    void XMLWriter::append( string_view text )
    {
        _pString->append( text );
    }

    void XMLWriter::appendEscaped( string_view text, bool bAttribute )
    {
        using Internal  = XMLWriterInternal;
        size_t runBegin = 0;
        size_t offset   = 0;
        while ( offset < text.size() )
        {
            const uint8 byte = static_cast<uint8>( text[offset] );
            if ( Internal::isPlain( byte, bAttribute ) )
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
            append( Internal::getEscape( byte ) );
            ++offset;
            runBegin = offset;
        }
        append( text.substr( runBegin ) );
    }

    void XMLWriter::closeStartTag()
    {
        if ( _bStartTagOpen == false )
            return;
        append( ">" );
        _bStartTagOpen = false;
    }

    void XMLWriter::fail()
    {
        SW_ASSERT( false && "XMLWriter: name or call order does not form valid XML" );
        _bFailed = true;
    }
} // namespace sw
