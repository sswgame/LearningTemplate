#include "pch.h"

#include "Core/String/MarkupToken.h"

namespace sw
{
    namespace
    {
        struct MarkupTokenInternal
        {
            static bool isASCIILetter( utf8 character ) { return ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' ); }
            static bool isNameCharacter( utf8 character ) { return isASCIILetter( character ) || ( '0' <= character && character <= '9' ) || character == '_'; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool MarkupToken::readTag( string_view markup, size_t start, MarkupToken& outToken )
    {
        size_t     cursor = start + 1;
        const bool bClose = cursor < markup.size() && markup[cursor] == '/';
        if ( bClose )
            ++cursor;
        const size_t nameStart = cursor;
        if ( cursor >= markup.size() || MarkupTokenInternal::isASCIILetter( markup[cursor] ) == false )
            return false;
        while ( cursor < markup.size() && MarkupTokenInternal::isNameCharacter( markup[cursor] ) )
        {
            ++cursor;
        }
        const size_t nameEnd      = cursor;
        size_t       valueStart   = cursor;
        size_t       valueEnd     = cursor;
        const bool   bHasTagValue = bClose == false && cursor < markup.size() && markup[cursor] == '=';
        if ( bHasTagValue )
        {
            valueStart = cursor + 1;
            cursor     = valueStart;
            while ( true )
            {
                const bool bInsideTagValue = cursor < markup.size() && markup[cursor] != ']' && markup[cursor] != '[';
                if ( bInsideTagValue == false )
                    break;
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

    bool MarkupToken::isEscapedBracket( string_view markup, size_t position )
    {
        return position + 1 < markup.size() && markup[position] == '[' && markup[position + 1] == '[';
    }

    bool MarkupToken::read( string_view markup, size_t& inoutOffset, MarkupToken& outToken )
    {
        if ( inoutOffset >= markup.size() )
            return false;
        outToken           = MarkupToken{};
        const size_t start = inoutOffset;
        if ( isEscapedBracket( markup, start ) )
        {
            outToken._offset = start;
            outToken._text   = markup.substr( start, 2 );
            outToken._kind   = MarkupTokenKind::EscapedBracket;
            inoutOffset      = start + 2;
            return true;
        }
        if ( markup[start] == '[' && readTag( markup, start, outToken ) )
        {
            inoutOffset = start + outToken._text.size();
            return true;
        }
        // 글 — 다음 `[[` 나 태그 형태 `[` 앞까지(지금 위치의 형태가 아닌 `[` 는 글에 넣는다).
        size_t      cursor = start + 1;
        MarkupToken probe{};
        while ( cursor < markup.size() )
        {
            const bool bStopsText = markup[cursor] == '[' && ( isEscapedBracket( markup, cursor ) || readTag( markup, cursor, probe ) );
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

    void MarkupToken::collectTagSequence( string_view markup, vector<string>& outListTag )
    {
        outListTag.clear();
        size_t      offset = 0;
        MarkupToken token{};
        while ( read( markup, offset, token ) )
        {
            if ( token._kind == MarkupTokenKind::OpenTag )
                outListTag.emplace_back( token._name );
            else if ( token._kind == MarkupTokenKind::CloseTag )
                outListTag.push_back( "/" + string( token._name ) );
        }
    }

    bool MarkupToken::hasSameTags( string_view sourceMarkup, string_view translatedMarkup )
    {
        vector<string> listSource;
        vector<string> listTranslated;
        collectTagSequence( sourceMarkup, listSource );
        collectTagSequence( translatedMarkup, listTranslated );
        return listSource == listTranslated;
    }

    bool MarkupToken::hasMarkup( string_view markup )
    {
        size_t      offset = 0;
        MarkupToken token{};
        while ( read( markup, offset, token ) )
        {
            if ( token._kind != MarkupTokenKind::Text )
                return true;
        }
        return false;
    }
} // namespace sw
