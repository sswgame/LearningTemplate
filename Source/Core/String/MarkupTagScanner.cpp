#include "pch.h"

#include "Core/String/MarkupTagScanner.h"

namespace sw
{
    namespace
    {
        struct MarkupTagScannerInternal
        {
            static bool isAsciiLetter( utf8 character ) { return ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' ); }
            static bool isNameCharacter( utf8 character ) { return isAsciiLetter( character ) || ( '0' <= character && character <= '9' ) || character == '_'; }

            /**
             * @brief @p start 의 `[` 가 태그 모양인지 보고, 그렇다면 토큰을 채웁니다(`_offset` · `_text` · 이름 · 값 · 종류).
             * @return 태그 모양이 아니면 false(그 `[` 는 글자다).
             */
            [[nodiscard]] static bool readTag( string_view markup, size_t start, MarkupToken& outToken )
            {
                size_t     cursor = start + 1;
                const bool bClose = cursor < markup.size() && markup[cursor] == '/';
                if ( bClose )
                    ++cursor;
                const size_t nameStart = cursor;
                if ( cursor >= markup.size() || isAsciiLetter( markup[cursor] ) == false )
                    return false;
                while ( cursor < markup.size() && isNameCharacter( markup[cursor] ) )
                {
                    ++cursor;
                }
                const size_t nameEnd    = cursor;
                size_t       valueStart = cursor;
                size_t       valueEnd   = cursor;
                if ( bClose == false && cursor < markup.size() && markup[cursor] == '=' )
                {
                    valueStart = cursor + 1;
                    cursor     = valueStart;
                    while ( cursor < markup.size() && markup[cursor] != ']' && markup[cursor] != '[' )
                    {
                        ++cursor;
                    }
                    valueEnd = cursor;
                }
                if ( cursor >= markup.size() || markup[cursor] != ']' )
                    return false;
                outToken._offset = start;
                outToken._text   = markup.substr( start, cursor + 1 - start );
                outToken._name   = markup.substr( nameStart, nameEnd - nameStart );
                outToken._value  = markup.substr( valueStart, valueEnd - valueStart );
                outToken._kind   = bClose ? MarkupTokenKind::CloseTag : MarkupTokenKind::OpenTag;
                return true;
            }

            static bool isEscapedBracket( string_view markup, size_t position )
            {
                return position + 1 < markup.size() && markup[position] == '[' && markup[position + 1] == '[';
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool MarkupTagScanner::readToken( string_view markup, size_t& inoutOffset, MarkupToken& outToken )
    {
        using Internal = MarkupTagScannerInternal;
        if ( inoutOffset >= markup.size() )
            return false;
        outToken           = MarkupToken{};
        const size_t start = inoutOffset;
        if ( Internal::isEscapedBracket( markup, start ) )
        {
            outToken._offset = start;
            outToken._text   = markup.substr( start, 2 );
            outToken._kind   = MarkupTokenKind::EscapedBracket;
            inoutOffset      = start + 2;
            return true;
        }
        if ( markup[start] == '[' && Internal::readTag( markup, start, outToken ) )
        {
            inoutOffset = start + outToken._text.size();
            return true;
        }
        // 글 — 다음 `[[` 나 태그 모양 `[` 앞까지(지금 자리의 모양이 아닌 `[` 는 글에 넣는다).
        size_t      cursor = start + 1;
        MarkupToken probe{};
        while ( cursor < markup.size() )
        {
            const bool bStopsText = markup[cursor] == '[' && ( Internal::isEscapedBracket( markup, cursor ) || Internal::readTag( markup, cursor, probe ) );
            if ( bStopsText )
                break;
            ++cursor;
        }
        outToken._offset = start;
        outToken._text   = markup.substr( start, cursor - start );
        outToken._kind   = MarkupTokenKind::Text;
        inoutOffset      = cursor;
        return true;
    }

    void MarkupTagScanner::collectTagSequence( string_view markup, vector<string>& outListTag )
    {
        outListTag.clear();
        size_t      offset = 0;
        MarkupToken token{};
        while ( readToken( markup, offset, token ) )
        {
            if ( token._kind == MarkupTokenKind::OpenTag )
                outListTag.emplace_back( token._name );
            else if ( token._kind == MarkupTokenKind::CloseTag )
                outListTag.push_back( "/" + string( token._name ) );
        }
    }

    bool MarkupTagScanner::hasSameTags( string_view sourceMarkup, string_view translatedMarkup )
    {
        vector<string> listSource;
        vector<string> listTranslated;
        collectTagSequence( sourceMarkup, listSource );
        collectTagSequence( translatedMarkup, listTranslated );
        return listSource == listTranslated;
    }

    bool MarkupTagScanner::hasMarkup( string_view markup )
    {
        size_t      offset = 0;
        MarkupToken token{};
        while ( readToken( markup, offset, token ) )
        {
            if ( token._kind != MarkupTokenKind::Text )
                return true;
        }
        return false;
    }
} // namespace sw
