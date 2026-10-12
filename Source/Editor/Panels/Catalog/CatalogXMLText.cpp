#include "pch.h"

#include "Editor/Panels/Catalog/CatalogXMLText.h"

#include "Core/String/StringBuilder.h"

#include "Engine/Serialization/XML/XMLDocument.h"

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

            static bool fail( string_view text, size_t offset, string_view what, string& outError )
            {
                StringBuilder<constant::kMaxBuffer256> message;
                message.appendFormat( "line %#: %#", computeLineNumber( text, offset ), what );
                outError = string( message.view() );
                return false;
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
                result += XMLDocument::escapeString( value );
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
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    int32 CatalogXMLElement::findAttribute( string_view name ) const
    {
        for ( size_t index = 0; index < _listAttribute.size(); ++index )
        {
            if ( string_view( _listAttribute[index]._name ) == name )
                return static_cast<int32>( index );
        }
        return -1;
    }
} // namespace sw::editor

namespace sw::editor
{
    bool CatalogXMLText::parse( string_view text, string& outError )
    {
        _listElement.clear();
        _rootIndex = -1;
        outError.clear();

        vector<int32> listOpen;
        size_t        position = 0;
        const size_t  size     = text.size();
        while ( position < size )
        {
            const size_t lessThan = text.find( '<', position );
            if ( lessThan == string_view::npos )
                break;

            if ( CatalogXMLTextInternal::startsWithAt( text, lessThan, "<!--" ) )
            {
                const size_t endPosition = text.find( "-->", lessThan + 4 );
                if ( endPosition == string_view::npos )
                    return CatalogXMLTextInternal::fail( text, lessThan, "unterminated comment", outError );
                position = endPosition + 3;
                continue;
            }
            if ( CatalogXMLTextInternal::startsWithAt( text, lessThan, "<![CDATA[" ) )
            {
                const size_t endPosition = text.find( "]]>", lessThan + 9 );
                if ( endPosition == string_view::npos )
                    return CatalogXMLTextInternal::fail( text, lessThan, "unterminated CDATA section", outError );
                position = endPosition + 3;
                continue;
            }
            if ( CatalogXMLTextInternal::startsWithAt( text, lessThan, "<?" ) )
            {
                const size_t endPosition = text.find( "?>", lessThan + 2 );
                if ( endPosition == string_view::npos )
                    return CatalogXMLTextInternal::fail( text, lessThan, "unterminated processing instruction", outError );
                position = endPosition + 2;
                continue;
            }
            if ( CatalogXMLTextInternal::startsWithAt( text, lessThan, "<!" ) )
            {
                const size_t endPosition = text.find( '>', lessThan + 2 );
                if ( endPosition == string_view::npos )
                    return CatalogXMLTextInternal::fail( text, lessThan, "unterminated declaration", outError );
                position = endPosition + 1;
                continue;
            }
            if ( CatalogXMLTextInternal::startsWithAt( text, lessThan, "</" ) )
            {
                const size_t greaterThan = text.find( '>', lessThan + 2 );
                if ( greaterThan == string_view::npos )
                    return CatalogXMLTextInternal::fail( text, lessThan, "unterminated closing tag", outError );
                size_t nameEnd = greaterThan;
                while ( nameEnd > lessThan + 2 && CatalogXMLTextInternal::isSpace( text[nameEnd - 1] ) )
                {
                    --nameEnd;
                }
                const string_view name = text.substr( lessThan + 2, nameEnd - ( lessThan + 2 ) );
                if ( listOpen.empty() )
                    return CatalogXMLTextInternal::fail( text, lessThan, "closing tag without an opening tag", outError );
                CatalogXMLElement& open = _listElement[static_cast<size_t>( listOpen.back() )];
                if ( string_view( open._name ) != name )
                    return CatalogXMLTextInternal::fail( text, lessThan, "closing tag does not match the open element", outError );
                open._closeBegin = static_cast<uint32>( lessThan );
                open._end        = static_cast<uint32>( greaterThan + 1 );
                listOpen.pop_back();
                position = greaterThan + 1;
                continue;
            }

            CatalogXMLElement element;
            element._begin         = static_cast<uint32>( lessThan );
            size_t       cursor    = lessThan + 1;
            const size_t nameBegin = cursor;
            while ( cursor < size && CatalogXMLTextInternal::isNameChar( text[cursor] ) )
            {
                ++cursor;
            }
            if ( cursor == nameBegin )
                return CatalogXMLTextInternal::fail( text, lessThan, "element without a name", outError );
            element._name = string( text.substr( nameBegin, cursor - nameBegin ) );

            bool bTagEnded = false;
            while ( bTagEnded == false )
            {
                while ( cursor < size && CatalogXMLTextInternal::isSpace( text[cursor] ) )
                {
                    ++cursor;
                }
                if ( cursor >= size )
                    return CatalogXMLTextInternal::fail( text, lessThan, "unterminated start tag", outError );
                const utf8 ch = text[cursor];
                if ( ch == '>' )
                {
                    element._startTagEnd = static_cast<uint32>( cursor + 1 );
                    bTagEnded            = true;
                    continue;
                }
                if ( ch == '/' )
                {
                    if ( cursor + 1 >= size || text[cursor + 1] != '>' )
                        return CatalogXMLTextInternal::fail( text, cursor, "stray '/' in a start tag", outError );
                    element._bSelfClosing = true;
                    element._startTagEnd  = static_cast<uint32>( cursor + 2 );
                    bTagEnded             = true;
                    continue;
                }

                CatalogXMLAttribute attribute;
                attribute._nameBegin = static_cast<uint32>( cursor );
                while ( cursor < size && CatalogXMLTextInternal::isNameChar( text[cursor] ) )
                {
                    ++cursor;
                }
                if ( cursor == attribute._nameBegin )
                    return CatalogXMLTextInternal::fail( text, cursor, "unexpected character in a start tag", outError );
                attribute._name = string( text.substr( attribute._nameBegin, cursor - attribute._nameBegin ) );
                while ( cursor < size && CatalogXMLTextInternal::isSpace( text[cursor] ) )
                {
                    ++cursor;
                }
                if ( cursor >= size || text[cursor] != '=' )
                    return CatalogXMLTextInternal::fail( text, cursor, "attribute without a value", outError );
                ++cursor;
                while ( cursor < size && CatalogXMLTextInternal::isSpace( text[cursor] ) )
                {
                    ++cursor;
                }
                if ( cursor >= size || ( text[cursor] != '"' && text[cursor] != '\'' ) )
                    return CatalogXMLTextInternal::fail( text, cursor, "attribute value must be quoted", outError );
                const utf8   quote      = text[cursor];
                const size_t valueBegin = cursor + 1;
                const size_t valueEnd   = text.find( quote, valueBegin );
                if ( valueEnd == string_view::npos )
                    return CatalogXMLTextInternal::fail( text, cursor, "unterminated attribute value", outError );
                attribute._valueBegin = static_cast<uint32>( valueBegin );
                attribute._valueEnd   = static_cast<uint32>( valueEnd );
                attribute._value      = XMLDocument::unescapeString( text.substr( valueBegin, valueEnd - valueBegin ) );
                cursor                = valueEnd + 1;
                element._listAttribute.push_back( std::move( attribute ) );
            }

            element._parent = listOpen.empty() ? -1 : listOpen.back();
            if ( element._parent < 0 && _rootIndex >= 0 )
                return CatalogXMLTextInternal::fail( text, lessThan, "more than one root element", outError );
            const bool   bSelfClosing = element._bSelfClosing;
            const uint32 startTagEnd  = element._startTagEnd;
            if ( bSelfClosing )
            {
                element._closeBegin = startTagEnd;
                element._end        = startTagEnd;
            }
            const int32 index = static_cast<int32>( _listElement.size() );
            _listElement.push_back( std::move( element ) );
            const int32 parentIndex = _listElement[static_cast<size_t>( index )]._parent;
            if ( parentIndex >= 0 )
                _listElement[static_cast<size_t>( parentIndex )]._listChild.push_back( index );
            else
                _rootIndex = index;
            if ( bSelfClosing == false )
                listOpen.push_back( index );
            position = startTagEnd;
        }

        if ( listOpen.empty() == false )
        {
            const CatalogXMLElement& open = _listElement[static_cast<size_t>( listOpen.back() )];
            return CatalogXMLTextInternal::fail( text, open._begin, "element is never closed", outError );
        }
        if ( _rootIndex < 0 )
        {
            outError = "no root element";
            return false;
        }
        return true;
    }

    bool CatalogXMLText::setAttributeValue( string_view text, const CatalogXMLElement& element, string_view name, string_view value, string& outText, string& outError )
    {
        outError.clear();
        const int32 attributeIndex = element.findAttribute( name );
        if ( attributeIndex >= 0 )
        {
            const CatalogXMLAttribute& attribute = element._listAttribute[static_cast<size_t>( attributeIndex )];
            const string               escaped   = XMLDocument::escapeString( value );
            outText                              = CatalogXMLTextInternal::joinPieces( text.substr( 0, attribute._valueBegin ), escaped, text.substr( attribute._valueEnd ) );
            return true;
        }
        if ( name.empty() )
        {
            outError = "attribute name is empty";
            return false;
        }
        size_t insertAt = element._begin + 1 + element._name.size();
        if ( element._listAttribute.empty() == false )
            insertAt = static_cast<size_t>( element._listAttribute.back()._valueEnd ) + 1;
        outText = CatalogXMLTextInternal::joinPieces( text.substr( 0, insertAt ), CatalogXMLTextInternal::makeAttributeText( name, value ), text.substr( insertAt ) );
        return true;
    }

    bool CatalogXMLText::removeAttribute( string_view text, const CatalogXMLElement& element, string_view name, string& outText, string& outError )
    {
        outError.clear();
        const int32 attributeIndex = element.findAttribute( name );
        if ( attributeIndex < 0 )
        {
            outError = "the element has no such attribute";
            return false;
        }
        const CatalogXMLAttribute& attribute = element._listAttribute[static_cast<size_t>( attributeIndex )];
        size_t                     begin     = attribute._nameBegin;
        while ( begin > 0 && CatalogXMLTextInternal::isSpace( text[begin - 1] ) )
        {
            --begin;
        }
        outText = CatalogXMLTextInternal::joinPieces( text.substr( 0, begin ), string_view{}, text.substr( static_cast<size_t>( attribute._valueEnd ) + 1 ) );
        return true;
    }

    bool CatalogXMLText::appendChildElement( string_view text, const CatalogXMLElement& parent, const vector<CatalogXMLElement>& listElement, string_view name,
                                             const vector<CatalogXMLNewAttribute>& listAttribute, string& outText, string& outError )
    {
        outError.clear();
        if ( name.empty() )
        {
            outError = "element name is empty";
            return false;
        }
        const string_view newline      = CatalogXMLTextInternal::detectNewline( text );
        const string      parentIndent = CatalogXMLTextInternal::readLineIndent( text, parent._begin );
        string            indent       = parentIndent + "\t";
        if ( parent._listChild.empty() == false )
            indent = CatalogXMLTextInternal::readLineIndent( text, listElement[static_cast<size_t>( parent._listChild.back() )]._begin );

        string child = "<";
        child.append( name.data(), name.size() );
        for ( const CatalogXMLNewAttribute& attribute : listAttribute )
        {
            child += CatalogXMLTextInternal::makeAttributeText( attribute._name, attribute._value );
        }
        child += " />";

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
            outText = CatalogXMLTextInternal::joinPieces( text.substr( 0, tagTail ), middle, text.substr( parent._startTagEnd ) );
            return true;
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
            outText = CatalogXMLTextInternal::joinPieces( text.substr( 0, lineStart ), middle, text.substr( lineStart ) );
            return true;
        }
        middle.append( newline.data(), newline.size() );
        middle += indent;
        middle += child;
        middle.append( newline.data(), newline.size() );
        middle += parentIndent;
        outText = CatalogXMLTextInternal::joinPieces( text.substr( 0, parent._closeBegin ), middle, text.substr( parent._closeBegin ) );
        return true;
    }

    bool CatalogXMLText::removeElement( string_view text, const CatalogXMLElement& element, string& outText, string& outError )
    {
        outError.clear();
        if ( element._parent < 0 )
        {
            outError = "the root element cannot be removed";
            return false;
        }
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
            outText = CatalogXMLTextInternal::joinPieces( text.substr( 0, begin ), string_view{}, text.substr( end ) );
            return true;
        }
        outText = CatalogXMLTextInternal::joinPieces( text.substr( 0, element._begin ), string_view{}, text.substr( element._end ) );
        return true;
    }
} // namespace sw::editor
