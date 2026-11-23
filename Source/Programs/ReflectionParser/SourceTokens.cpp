#include "pch.h"

#include "Programs/ReflectionParser/SourceTokens.h"

#include "Core/String/StringUtil.h"

#include "Programs/ReflectionParser/CursorReader.h"
#include "Programs/ReflectionParser/ParserDefines.h"

namespace sw
{
    struct SourceTokens::SourceToken
    {
        string      _spelling;
        CXTokenKind _kind;
    };

    CXSourceLocation SourceTokens::getExpansionPoint( const CXTranslationUnit translationUnit, const CXSourceLocation location )
    {
        CXFile file   = nullptr;
        uint32 line   = 0;
        uint32 column = 0;
        clang_getExpansionLocation( location, &file, &line, &column, nullptr );
        return ( file != nullptr ) ? clang_getLocation( translationUnit, file, line, column ) : location;
    }

    vector<SourceTokens::SourceToken> SourceTokens::tokenizeCursor( const CXCursor cursor )
    {
        vector<SourceToken>     listToken;
        const CXTranslationUnit translationUnit = clang_Cursor_getTranslationUnit( cursor );
        const CXSourceRange     extent          = clang_getCursorExtent( cursor );
        const CXSourceRange     sourceRange     = clang_getRange( getExpansionPoint( translationUnit, clang_getRangeStart( extent ) ),
                                                                  getExpansionPoint( translationUnit, clang_getRangeEnd( extent ) ) );
        CXToken*                pToken          = nullptr;
        uint32                  tokenCount      = 0;
        clang_tokenize( translationUnit, sourceRange, &pToken, &tokenCount );
        listToken.reserve( tokenCount );
        for ( uint32 tokenIndex = 0; tokenIndex < tokenCount; ++tokenIndex )
        {
            listToken.push_back( SourceToken{ CursorReader::takeString( clang_getTokenSpelling( translationUnit, pToken[tokenIndex] ) ),
                                              clang_getTokenKind( pToken[tokenIndex] ) } );
        }
        clang_disposeTokens( translationUnit, pToken, tokenCount );
        return listToken;
    }

    bool SourceTokens::isSpelledInSource( const CXCursor cursor, const string& name )
    {
        for ( const SourceToken& token : tokenizeCursor( cursor ) )
        {
            if ( token._kind == CXToken_Identifier && token._spelling == name )
                return true;
        }
        return false;
    }

    int32 SourceTokens::getBracketDelta( const string_view spelling ) noexcept
    {
        const bool bOpeningBracket = spelling == "(" || spelling == "[" || spelling == "{" || spelling == "<";
        if ( bOpeningBracket )
            return 1;
        const bool bClosingBracket = spelling == ")" || spelling == "]" || spelling == "}" || spelling == ">";
        if ( bClosingBracket )
            return -1;
        if ( spelling == ">>" )
            return -2;
        return 0;
    }

    string SourceTokens::joinTokens( const vector<SourceToken>& listToken, const size_t begin, const size_t end )
    {
        string text;
        for ( size_t tokenIndex = begin; tokenIndex < end; ++tokenIndex )
        {
            const SourceToken& token     = listToken[tokenIndex];
            const bool         bWordLike = token._kind == CXToken_Identifier || token._kind == CXToken_Keyword || token._kind == CXToken_Literal;
            if ( bWordLike && tokenIndex > begin )
            {
                const CXTokenKind previousKind = listToken[tokenIndex - 1]._kind;
                const bool        bNeedsSpace  = previousKind == CXToken_Identifier || previousKind == CXToken_Keyword || previousKind == CXToken_Literal;
                if ( bNeedsSpace )
                    text.push_back( ' ' );
            }
            text += token._spelling;
        }
        return text;
    }

    string SourceTokens::readDefaultArgument( const CXCursor parameterCursor )
    {
        const vector<SourceToken> listToken = tokenizeCursor( parameterCursor );
        int32                     depth     = 0;
        for ( size_t tokenIndex = 0; tokenIndex < listToken.size(); ++tokenIndex )
        {
            const string& spelling = listToken[tokenIndex]._spelling;
            if ( depth == 0 && spelling == "=" )
                return joinTokens( listToken, tokenIndex + 1, listToken.size() );
            depth += getBracketDelta( spelling );
        }
        return {};
    }

    size_t SourceTokens::findTopLevelName( const vector<SourceToken>& listToken, const string& name )
    {
        int32 depth = 0;
        for ( size_t tokenIndex = 0; tokenIndex < listToken.size(); ++tokenIndex )
        {
            const SourceToken& token = listToken[tokenIndex];
            if ( depth == 0 && token._kind == CXToken_Identifier && token._spelling == name )
                return tokenIndex;
            depth += getBracketDelta( token._spelling );
        }
        return listToken.size();
    }

    bool SourceTokens::hasStringLiteralDefault( const CXCursor fieldCursor, const string& name )
    {
        const vector<SourceToken> listToken = tokenizeCursor( fieldCursor );
        const size_t              nameIndex = findTopLevelName( listToken, name );
        if ( nameIndex + 1 >= listToken.size() || ( listToken[nameIndex + 1]._spelling != "=" && listToken[nameIndex + 1]._spelling != "{" ) )
            return false;
        for ( size_t tokenIndex = nameIndex + 2; tokenIndex < listToken.size(); ++tokenIndex )
        {
            const SourceToken& token = listToken[tokenIndex];
            if ( token._kind != CXToken_Literal )
                continue;
            // 접두사(`u8`)가 붙어도 따옴표부터 본다. `""` 은 비어 있어 데이터를 가리키지 않는다.
            const size_t quote = token._spelling.find( '"' );
            if ( quote != string::npos && token._spelling.size() - quote > 2 )
                return true;
        }
        return false;
    }

    bool SourceTokens::isTypeWord( const SourceToken& token ) noexcept
    {
        if ( token._kind == CXToken_Identifier )
            return true;
        if ( token._kind != CXToken_Keyword )
            return false;
        const string_view spelling = token._spelling;
        return spelling != "const" && spelling != "volatile" && spelling != "struct" && spelling != "class" && spelling != "enum" &&
               spelling != "typename";
    }

    string SourceTokens::findParameterName( const vector<SourceToken>& listToken, const size_t begin, const size_t end )
    {
        if ( end <= begin + 1 )
            return {};
        const SourceToken& last = listToken[end - 1];
        if ( last._kind != CXToken_Identifier || listToken[end - 2]._spelling == "::" )
            return {};
        for ( size_t tokenIndex = begin; tokenIndex + 1 < end; ++tokenIndex )
        {
            if ( isTypeWord( listToken[tokenIndex] ) )
                return last._spelling;
        }
        return {};
    }

    vector<string> SourceTokens::readEventParameterNames( const CXCursor fieldCursor )
    {
        vector<SourceToken> listToken = tokenizeCursor( fieldCursor );
        auto                findStart = [&listToken]() -> size_t
        {
            for ( size_t tokenIndex = 0; tokenIndex + 1 < listToken.size(); ++tokenIndex )
            {
                if ( listToken[tokenIndex]._spelling == annotation::kEventTemplateLeaf && listToken[tokenIndex + 1]._spelling == "<" )
                    return tokenIndex + 2;
            }
            return listToken.size();
        };
        size_t cursorIndex = findStart();
        if ( cursorIndex == listToken.size() )
        {
            const CXCursor aliasDeclaration = clang_getTypeDeclaration( clang_getCursorType( fieldCursor ) );
            if ( clang_Cursor_isNull( aliasDeclaration ) == 0 )
            {
                listToken   = tokenizeCursor( aliasDeclaration );
                cursorIndex = findStart();
            }
        }

        // 반환 타입 뒤의 첫 `(` 부터 짝이 맞는 `)` 까지가 인자 목록이다.
        while ( cursorIndex < listToken.size() && listToken[cursorIndex]._spelling != "(" )
        {
            ++cursorIndex;
        }
        vector<string> listName;
        if ( cursorIndex >= listToken.size() )
            return listName;

        int32  depth      = 0;
        size_t chunkStart = cursorIndex + 1;
        for ( size_t tokenIndex = cursorIndex; tokenIndex < listToken.size(); ++tokenIndex )
        {
            const string& spelling = listToken[tokenIndex]._spelling;
            const int32   delta    = getBracketDelta( spelling );
            if ( depth == 1 && ( spelling == "," || ( delta < 0 && spelling == ")" ) ) )
            {
                if ( tokenIndex > chunkStart )
                    listName.push_back( findParameterName( listToken, chunkStart, tokenIndex ) );
                chunkStart = tokenIndex + 1;
            }
            depth += delta;
            if ( depth == 0 )
                break;
        }
        return listName;
    }
} // namespace sw
