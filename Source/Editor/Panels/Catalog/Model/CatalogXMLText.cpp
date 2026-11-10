#include "pch.h"

#include "Editor/Panels/Catalog/Model/CatalogXMLText.h"

#include "Core/String/StringBuilder.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLWriter.h"

namespace sw::editor
{
    namespace
    {
        struct CatalogXMLTextInternal
        {
            static constexpr string_view kCrLf = "\r\n";

            static bool isSpace( utf8 ch ) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; }

            static bool isNameChar( utf8 ch ) { return isSpace( ch ) == false && ch != '/' && ch != '>' && ch != '=' && ch != '<' && ch != '"' && ch != '\''; }

            static bool startsWithAt( string_view text, size_t position, string_view literal ) { return text.compare( position, literal.size(), literal ) == 0; }

            static uint32 computeLineNumber( string_view text, size_t offset )
            {
                uint32 line = 1;
                for ( size_t index = 0; index < offset && index < text.size(); ++index )
                {
                    if ( text[index] == '\n' )
                        ++line;
                }
                return line;
            }

            /** @brief 읽기 오류입니다 — 문장은 무엇이 틀렸는지(리터럴), 대상은 `line N`. */
            static Error fail( string_view text, size_t offset, const utf8* pWhat )
            {
                StringBuilder<constant::buffer::kMax64> lineBuilder;
                lineBuilder.appendFormat( "line %#", computeLineNumber( text, offset ) );
                return Error{ ErrorKind::InvalidArgument, pWhat, lineBuilder.view() };
            }

            /** @brief @p offset 가 있는 줄의 앞쪽 공백(들여쓰기)입니다. */
            static string readLineIndent( string_view text, size_t offset )
            {
                size_t lineStart = offset;
                while ( lineStart > 0 && text[lineStart - 1] != '\n' )
                {
                    --lineStart;
                }
                size_t indentEnd = lineStart;
                while ( indentEnd < text.size() && ( text[indentEnd] == ' ' || text[indentEnd] == '\t' ) )
                {
                    ++indentEnd;
                }
                return string( text.substr( lineStart, indentEnd - lineStart ) );
            }

            static string_view detectNewline( string_view text ) { return text.find( kCrLf ) != string_view::npos ? kCrLf : string_view( "\n" ); }

            static string makeAttributeText( string_view name, string_view value )
            {
                string result = " ";
                result.append( name.data(), name.size() );
                result += "=\"";
                XMLWriter::appendEscaped( result, value, true );
                result += "\"";
                return result;
            }

            static string joinPieces( string_view head, string_view middle, string_view tail )
            {
                string result;
                result.reserve( head.size() + middle.size() + tail.size() );
                result.append( head.data(), head.size() );
                result.append( middle.data(), middle.size() );
                result.append( tail.data(), tail.size() );
                return result;
            }

            enum class MarkupSkip : uint8
            {
                NotMarkup, ///< 건너뛸 마크업이 아니다
                Skipped,   ///< 주석 · CDATA · 처리 지시문 · 선언을 건너뛰었다(끝나지 않은 마크업은 오류다)
            };

            /** @brief @p lessThan 의 `<` 가 주석 · CDATA · 처리 지시문 · 선언이면 끝까지 건너뛰어 @p outPosition 을 그 다음으로 둡니다. 순서가 곧 의미입니다(`<!--` · `<![CDATA[` 가 `<!` 보다 앞). */
            static expected<MarkupSkip> skipNonElementMarkup( string_view text, size_t lessThan, size_t& outPosition )
            {
                struct Rule
                {
                    string_view _prefix;
                    string_view _terminator;
                    const utf8* _pError;
                };
                static constexpr Rule kArrRule[] = {
                    {     "<!--", "-->",                "unterminated comment"},
                    {"<![CDATA[", "]]>",          "unterminated CDATA section"},
                    {       "<?",  "?>", "unterminated processing instruction"},
                    {       "<!",   ">",            "unterminated declaration"},
                };
                for ( const Rule& rule : kArrRule )
                {
                    if ( startsWithAt( text, lessThan, rule._prefix ) == false )
                        continue;
                    const size_t endPosition = text.find( rule._terminator, lessThan + rule._prefix.size() );
                    if ( endPosition == string_view::npos )
                        return unexpected( fail( text, lessThan, rule._pError ) );
                    outPosition = endPosition + rule._terminator.size();
                    return MarkupSkip::Skipped;
                }
                return MarkupSkip::NotMarkup;
            }

            /** @brief `</이름>` 을 읽어 열려 있는 요소를 닫습니다. @p lessThan 은 `</` 의 `<` 위치이고, 성공하면 @p outPosition 이 `>` 다음입니다. */
            static expected<void> closeElement( string_view text, size_t lessThan, vector<CatalogXMLElement>& listElement, vector<int32>& listOpen, size_t& outPosition )
            {
                const size_t greaterThan = text.find( '>', lessThan + 2 );
                if ( greaterThan == string_view::npos )
                    return unexpected( fail( text, lessThan, "unterminated closing tag" ) );
                size_t nameEnd = greaterThan;
                while ( nameEnd > lessThan + 2 && isSpace( text[nameEnd - 1] ) )
                {
                    --nameEnd;
                }
                const string_view name = text.substr( lessThan + 2, nameEnd - ( lessThan + 2 ) );
                if ( listOpen.empty() )
                    return unexpected( fail( text, lessThan, "closing tag without an opening tag" ) );
                CatalogXMLElement& open = listElement[static_cast<size_t>( listOpen.back() )];
                if ( string_view( open._name ) != name )
                    return unexpected( fail( text, lessThan, "closing tag does not match the open element" ) );
                open._closeBegin = static_cast<uint32>( lessThan );
                open._end        = static_cast<uint32>( greaterThan + 1 );
                listOpen.pop_back();
                outPosition = greaterThan + 1;
                return {};
            }

            static void skipSpaces( string_view text, size_t& inoutCursor )
            {
                while ( inoutCursor < text.size() && isSpace( text[inoutCursor] ) )
                {
                    ++inoutCursor;
                }
            }

            /** @brief 시작 태그 안의 속성 하나(`이름 = "값"`)를 @p inoutCursor 에서 읽습니다. */
            [[nodiscard]] static expected<void> readAttribute( string_view text, size_t& inoutCursor, CatalogXMLAttribute& outAttribute )
            {
                const size_t size       = text.size();
                size_t       cursor     = inoutCursor;
                outAttribute._nameBegin = static_cast<uint32>( cursor );
                while ( cursor < size && isNameChar( text[cursor] ) )
                {
                    ++cursor;
                }
                if ( cursor == outAttribute._nameBegin )
                    return unexpected( fail( text, cursor, "unexpected character in a start tag" ) );
                outAttribute._name = string( text.substr( outAttribute._nameBegin, cursor - outAttribute._nameBegin ) );
                skipSpaces( text, cursor );
                if ( cursor >= size || text[cursor] != '=' )
                    return unexpected( fail( text, cursor, "attribute without a value" ) );
                ++cursor;
                skipSpaces( text, cursor );
                if ( cursor >= size || ( text[cursor] != '"' && text[cursor] != '\'' ) )
                    return unexpected( fail( text, cursor, "attribute value must be quoted" ) );
                const utf8   quote      = text[cursor];
                const size_t valueBegin = cursor + 1;
                const size_t valueEnd   = text.find( quote, valueBegin );
                if ( valueEnd == string_view::npos )
                    return unexpected( fail( text, cursor, "unterminated attribute value" ) );
                outAttribute._valueBegin = static_cast<uint32>( valueBegin );
                outAttribute._valueEnd   = static_cast<uint32>( valueEnd );
                outAttribute._value      = XMLDocument::unescapeString( text.substr( valueBegin, valueEnd - valueBegin ) );
                inoutCursor              = valueEnd + 1;
                return {};
            }

            /** @brief `<이름 속성…>` 또는 `<이름 속성…/>` 시작 태그를 읽어 @p outElement 에 채웁니다. */
            [[nodiscard]] static expected<void> readStartTag( string_view text, size_t lessThan, CatalogXMLElement& outElement )
            {
                const size_t size      = text.size();
                outElement._begin      = static_cast<uint32>( lessThan );
                size_t       cursor    = lessThan + 1;
                const size_t nameBegin = cursor;
                while ( cursor < size && isNameChar( text[cursor] ) )
                {
                    ++cursor;
                }
                if ( cursor == nameBegin )
                    return unexpected( fail( text, lessThan, "element without a name" ) );
                outElement._name = string( text.substr( nameBegin, cursor - nameBegin ) );

                while ( true )
                {
                    skipSpaces( text, cursor );
                    if ( cursor >= size )
                        return unexpected( fail( text, lessThan, "unterminated start tag" ) );
                    const utf8 ch = text[cursor];
                    if ( ch == '>' )
                    {
                        outElement._startTagEnd = static_cast<uint32>( cursor + 1 );
                        return {};
                    }
                    if ( ch == '/' )
                    {
                        if ( cursor + 1 >= size || text[cursor + 1] != '>' )
                            return unexpected( fail( text, cursor, "stray '/' in a start tag" ) );
                        outElement._bSelfClosing = true;
                        outElement._startTagEnd  = static_cast<uint32>( cursor + 2 );
                        return {};
                    }

                    CatalogXMLAttribute  attribute;
                    const expected<void> read = readAttribute( text, cursor, attribute );
                    if ( read.has_value() == false )
                        return read;
                    outElement._listAttribute.push_back( std::move( attribute ) );
                }
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    optional<uint32> CatalogXMLElement::findAttribute( string_view name ) const
    {
        for ( size_t index = 0; index < _listAttribute.size(); ++index )
        {
            if ( string_view( _listAttribute[index]._name ) == name )
                return static_cast<uint32>( index );
        }
        return nullopt;
    }
} // namespace sw::editor

namespace sw::editor
{
    expected<void> CatalogXMLText::parse( string_view text )
    {
        // 읽다가 실패하면 앞에서 채운 요소와 루트 번호가 남아 `isParsed()` 가 참이 된다 — 실패하면 모두 비워 "깨진 문서는 테이블이 없다" 를 지킨다.
        const expected<void> parsed = parseInternal( text );
        if ( parsed.has_value() )
            return parsed;
        _listElement.clear();
        _rootIndex.reset();
        return parsed;
    }

    expected<void> CatalogXMLText::parseInternal( string_view text )
    {
        using Internal = CatalogXMLTextInternal;
        _listElement.clear();
        _rootIndex.reset();

        vector<int32> listOpen;
        size_t        position = 0;
        const size_t  size     = text.size();
        while ( position < size )
        {
            const size_t lessThan = text.find( '<', position );
            if ( lessThan == string_view::npos )
                break;

            const expected<Internal::MarkupSkip> skip = Internal::skipNonElementMarkup( text, lessThan, position );
            if ( skip.has_value() == false )
                return unexpected( skip.error() );
            if ( *skip == Internal::MarkupSkip::Skipped )
                continue;
            if ( Internal::startsWithAt( text, lessThan, "</" ) )
            {
                const expected<void> closed = Internal::closeElement( text, lessThan, _listElement, listOpen, position );
                if ( closed.has_value() == false )
                    return closed;
                continue;
            }

            CatalogXMLElement    element;
            const expected<void> started = Internal::readStartTag( text, lessThan, element );
            if ( started.has_value() == false )
                return started;

            element._parent = listOpen.empty() ? optional<int32>{} : optional<int32>{ listOpen.back() };
            if ( element._parent.has_value() == false && _rootIndex.has_value() )
                return unexpected( Internal::fail( text, lessThan, "more than one root element" ) );
            const bool   bSelfClosing = element._bSelfClosing;
            const uint32 startTagEnd  = element._startTagEnd;
            if ( bSelfClosing )
            {
                element._closeBegin = startTagEnd;
                element._end        = startTagEnd;
            }
            const int32 index = static_cast<int32>( _listElement.size() );
            _listElement.push_back( std::move( element ) );
            const optional<int32> parentIndex = _listElement[static_cast<size_t>( index )]._parent;
            if ( parentIndex.has_value() )
                _listElement[static_cast<size_t>( *parentIndex )]._listChild.push_back( index );
            else
                _rootIndex = index;
            if ( bSelfClosing == false )
                listOpen.push_back( index );
            position = startTagEnd;
        }

        if ( listOpen.empty() == false )
        {
            const CatalogXMLElement& open = _listElement[static_cast<size_t>( listOpen.back() )];
            return unexpected( Internal::fail( text, open._begin, "element is never closed" ) );
        }
        if ( _rootIndex.has_value() == false )
            return unexpected( Error{ ErrorKind::NotFound, "no root element" } );
        return {};
    }

    expected<string> CatalogXMLText::setAttributeValue( string_view text, const CatalogXMLElement& element, string_view name, string_view value )
    {
        const optional<uint32> attributeIndex = element.findAttribute( name );
        if ( attributeIndex.has_value() )
        {
            const CatalogXMLAttribute& attribute = element._listAttribute[*attributeIndex];
            string                     escaped;
            XMLWriter::appendEscaped( escaped, value, true );
            return CatalogXMLTextInternal::joinPieces( text.substr( 0, attribute._valueBegin ), escaped, text.substr( attribute._valueEnd ) );
        }
        if ( name.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "attribute name is empty" } );
        size_t insertAt = element._begin + 1 + element._name.size();
        if ( element._listAttribute.empty() == false )
            insertAt = static_cast<size_t>( element._listAttribute.back()._valueEnd ) + 1;
        return CatalogXMLTextInternal::joinPieces( text.substr( 0, insertAt ), CatalogXMLTextInternal::makeAttributeText( name, value ), text.substr( insertAt ) );
    }

    expected<string> CatalogXMLText::removeAttribute( string_view text, const CatalogXMLElement& element, string_view name )
    {
        const optional<uint32> attributeIndex = element.findAttribute( name );
        if ( attributeIndex.has_value() == false )
            return unexpected( Error{ ErrorKind::NotFound, "the element has no such attribute", name } );
        const CatalogXMLAttribute& attribute = element._listAttribute[*attributeIndex];
        size_t                     begin     = attribute._nameBegin;
        while ( begin > 0 && CatalogXMLTextInternal::isSpace( text[begin - 1] ) )
        {
            --begin;
        }
        return CatalogXMLTextInternal::joinPieces( text.substr( 0, begin ), string_view{}, text.substr( static_cast<size_t>( attribute._valueEnd ) + 1 ) );
    }

    expected<string> CatalogXMLText::appendChildElement( string_view text, const CatalogXMLElement& parent, const vector<CatalogXMLElement>& listElement, string_view name,
                                                         const vector<CatalogXMLNewAttribute>& listAttribute )
    {
        if ( name.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "element name is empty" } );
        string child = "<";
        child.append( name.data(), name.size() );
        for ( const CatalogXMLNewAttribute& attribute : listAttribute )
        {
            child += CatalogXMLTextInternal::makeAttributeText( attribute._name, attribute._value );
        }
        child += " />";
        return appendChildText( text, parent, listElement, child );
    }

    string CatalogXMLText::appendChildText( string_view text, const CatalogXMLElement& parent, const vector<CatalogXMLElement>& listElement, string_view childText )
    {
        const string_view newline      = CatalogXMLTextInternal::detectNewline( text );
        const string      parentIndent = CatalogXMLTextInternal::readLineIndent( text, parent._begin );
        string            indent       = parentIndent + "\t";
        if ( parent._listChild.empty() == false )
            indent = CatalogXMLTextInternal::readLineIndent( text, listElement[static_cast<size_t>( parent._listChild.back() )]._begin );
        const string child{ childText };

        if ( parent._bSelfClosing )
        {
            size_t tagTail = static_cast<size_t>( parent._startTagEnd ) - 2; // `/>` 의 `/`
            while ( tagTail > 0 && CatalogXMLTextInternal::isSpace( text[tagTail - 1] ) )
            {
                --tagTail;
            }
            string middle = ">";
            middle.append( newline.data(), newline.size() );
            middle += indent;
            middle += child;
            middle.append( newline.data(), newline.size() );
            middle += parentIndent;
            middle += "</";
            middle += parent._name;
            middle += ">";
            return CatalogXMLTextInternal::joinPieces( text.substr( 0, tagTail ), middle, text.substr( parent._startTagEnd ) );
        }

        size_t lineStart = parent._closeBegin;
        while ( lineStart > 0 && ( text[lineStart - 1] == ' ' || text[lineStart - 1] == '\t' ) )
        {
            --lineStart;
        }
        const bool bCloseTagOwnsLine = lineStart == 0 || text[lineStart - 1] == '\n';
        string     middle;
        if ( bCloseTagOwnsLine )
        {
            middle = indent;
            middle += child;
            middle.append( newline.data(), newline.size() );
            return CatalogXMLTextInternal::joinPieces( text.substr( 0, lineStart ), middle, text.substr( lineStart ) );
        }
        middle.append( newline.data(), newline.size() );
        middle += indent;
        middle += child;
        middle.append( newline.data(), newline.size() );
        middle += parentIndent;
        return CatalogXMLTextInternal::joinPieces( text.substr( 0, parent._closeBegin ), middle, text.substr( parent._closeBegin ) );
    }

    expected<string> CatalogXMLText::removeElement( string_view text, const CatalogXMLElement& element )
    {
        if ( element._parent.has_value() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the root element cannot be removed" } );
        size_t begin = element._begin;
        while ( begin > 0 && ( text[begin - 1] == ' ' || text[begin - 1] == '\t' ) )
        {
            --begin;
        }
        size_t end = element._end;
        while ( end < text.size() && ( text[end] == ' ' || text[end] == '\t' ) )
        {
            ++end;
        }
        const bool bLineStart = begin == 0 || text[begin - 1] == '\n';
        const bool bLineEnd   = end >= text.size() || text[end] == '\n' || text[end] == '\r';
        if ( bLineStart && bLineEnd )
        {
            if ( end < text.size() && text[end] == '\r' )
                ++end;
            if ( end < text.size() && text[end] == '\n' )
                ++end;
            return CatalogXMLTextInternal::joinPieces( text.substr( 0, begin ), string_view{}, text.substr( end ) );
        }
        return CatalogXMLTextInternal::joinPieces( text.substr( 0, element._begin ), string_view{}, text.substr( element._end ) );
    }

    expected<string> CatalogXMLText::duplicateElement( string_view text, const CatalogXMLElement& element )
    {
        if ( element._parent.has_value() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the root element cannot be duplicated" } );
        const string_view newline = CatalogXMLTextInternal::detectNewline( text );
        string            middle;
        middle.append( newline.data(), newline.size() );
        middle += CatalogXMLTextInternal::readLineIndent( text, element._begin );
        const string_view original = text.substr( element._begin, element._end - element._begin );
        middle.append( original.data(), original.size() );
        return CatalogXMLTextInternal::joinPieces( text.substr( 0, element._end ), middle, text.substr( element._end ) );
    }
} // namespace sw::editor
