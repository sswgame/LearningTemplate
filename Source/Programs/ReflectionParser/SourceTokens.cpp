#include "pch.h"

#include "Programs/ReflectionParser/SourceTokens.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Programs/ReflectionParser/CursorReader.h"
#include "Programs/ReflectionParser/ParserDefines.h"

namespace sw
{
    namespace
    {
        struct SourceTokensInternal
        {
            /** @brief 토큰화 누계입니다. 프로세스 하나가 파서 한 번이라 지우지 않는다. 더하기만 해서 워커끼리 겹쳐도 값이 맞다. */
            static atomic<int64> s_callCount;
            static atomic<int64> s_tokenCount;
            static atomic<int64> s_nanoseconds;
        };

        atomic<int64> SourceTokensInternal::s_callCount{ 0 };
        atomic<int64> SourceTokensInternal::s_tokenCount{ 0 };
        atomic<int64> SourceTokensInternal::s_nanoseconds{ 0 };
    } // namespace

    SourceTokenStats SourceTokens::getStats()
    {
        SourceTokenStats stats;
        stats._callCount   = SourceTokensInternal::s_callCount.load( std::memory_order_relaxed );
        stats._tokenCount  = SourceTokensInternal::s_tokenCount.load( std::memory_order_relaxed );
        stats._nanoseconds = SourceTokensInternal::s_nanoseconds.load( std::memory_order_relaxed );
        return stats;
    }

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
        const int64             startNanoseconds = MonotonicClock::nowNanoseconds();
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
        SourceTokensInternal::s_callCount.fetch_add( 1, std::memory_order_relaxed );
        SourceTokensInternal::s_tokenCount.fetch_add( tokenCount, std::memory_order_relaxed );
        SourceTokensInternal::s_nanoseconds.fetch_add( MonotonicClock::nowNanoseconds() - startNanoseconds, std::memory_order_relaxed );
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
