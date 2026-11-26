#include "pch.h"

#include "Programs/ReflectionParser/SourceTokens.h"

#include "Core/String/StringUtil.h"

#include "Programs/ReflectionParser/CursorReader.h"
#include "Programs/ReflectionParser/ParserDefines.h"

namespace sw
{
    namespace
    {
        struct SourceTokensInternal
        {
            /** @brief 커서 범위의 토큰 철자 · 종류입니다. */
            struct SourceToken
            {
                string      _spelling;
                CXTokenKind _kind;
            };

            /** @brief 위치를 매크로를 쓴 자리로 옮깁니다. 매크로 정의 안(명령줄 `-D` 버퍼)의 위치로는 소스 토큰을 읽을 수 없습니다. */
            static CXSourceLocation getExpansionPoint( const CXTranslationUnit translationUnit, const CXSourceLocation location )
            {
                CXFile file   = nullptr;
                uint32 line   = 0;
                uint32 column = 0;
                clang_getExpansionLocation( location, &file, &line, &column, nullptr );
                return ( file != nullptr ) ? clang_getLocation( translationUnit, file, line, column ) : location;
            }

            /**
             * @brief 커서 범위의 소스 토큰입니다.
             * @details 범위의 시작이 애노테이션 매크로(`PROPERTY(...)` 가 전개한 `__attribute__`) 안이면 그 위치는 매크로 정의 쪽이라, 범위를
             *          그대로 토큰으로 읽으면 선언의 토큰이 하나도 나오지 않는다. 양 끝을 전개 위치로 옮겨 읽는다.
             */
            static vector<SourceToken> tokenizeCursor( const CXCursor cursor )
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

            /** @brief 여는 괄호류면 +1, 닫는 괄호류면 -1 입니다. `<` · `>` 도 셉니다(템플릿 인자 안의 쉼표를 가르지 않게). */
            static int32 getBracketDelta( const string_view spelling ) noexcept
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

            /** @brief 토큰을 C++ 식 글로 잇습니다. 이름 · 숫자끼리만 띄웁니다(`Mode::Fast` · `-1` · `sw::string("a b")`). */
            static string joinTokens( const vector<SourceToken>& listToken, const size_t begin, const size_t end )
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

            /** @brief 괄호 밖(깊이 0)에서 처음 나오는 식별자 @p name 의 자리입니다. 없으면 @p listToken 의 크기입니다. */
            static size_t findTopLevelName( const vector<SourceToken>& listToken, const string& name )
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

            /** @brief 타입 이름을 이루는 낱말(이름 · `int` 같은 키워드)인지 봅니다. 한정자(`const` · `struct` …)는 아닙니다. */
            static bool isTypeWord( const SourceToken& token ) noexcept
            {
                if ( token._kind == CXToken_Identifier )
                    return true;
                if ( token._kind != CXToken_Keyword )
                    return false;
                const string_view spelling = token._spelling;
                return spelling != "const" && spelling != "volatile" && spelling != "struct" && spelling != "class" && spelling != "enum" &&
                       spelling != "typename";
            }

            /**
             * @brief 인자 선언 토큰 한 덩이(`const DamageEvent& damage`)에서 이름을 꺼냅니다. 이름 없이 적었으면 빈 문자열입니다.
             * @details 마지막 토큰이 이름이고 그 앞에 타입이 남아야 이름이다 — `int32` · `sw::TagID` · `const Foo` 는 이름이 없다.
             */
            static string findParameterName( const vector<SourceToken>& listToken, const size_t begin, const size_t end )
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
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool SourceTokens::isSpelledInSource( const CXCursor cursor, const string& name )
    {
        for ( const SourceTokensInternal::SourceToken& token : SourceTokensInternal::tokenizeCursor( cursor ) )
        {
            if ( token._kind == CXToken_Identifier && token._spelling == name )
                return true;
        }
        return false;
    }

    string SourceTokens::readDefaultArgument( const CXCursor parameterCursor )
    {
        const vector<SourceTokensInternal::SourceToken> listToken = SourceTokensInternal::tokenizeCursor( parameterCursor );
        int32                                           depth     = 0;
        for ( size_t tokenIndex = 0; tokenIndex < listToken.size(); ++tokenIndex )
        {
            const string& spelling = listToken[tokenIndex]._spelling;
            if ( depth == 0 && spelling == "=" )
                return SourceTokensInternal::joinTokens( listToken, tokenIndex + 1, listToken.size() );
            depth += SourceTokensInternal::getBracketDelta( spelling );
        }
        return {};
    }

    bool SourceTokens::hasStringLiteralDefault( const CXCursor fieldCursor, const string& name )
    {
        const vector<SourceTokensInternal::SourceToken> listToken = SourceTokensInternal::tokenizeCursor( fieldCursor );
        const size_t                                    nameIndex = SourceTokensInternal::findTopLevelName( listToken, name );
        if ( nameIndex + 1 >= listToken.size() || ( listToken[nameIndex + 1]._spelling != "=" && listToken[nameIndex + 1]._spelling != "{" ) )
            return false;
        for ( size_t tokenIndex = nameIndex + 2; tokenIndex < listToken.size(); ++tokenIndex )
        {
            const SourceTokensInternal::SourceToken& token = listToken[tokenIndex];
            if ( token._kind != CXToken_Literal )
                continue;
            // 접두사(`u8`)가 붙어도 따옴표부터 본다. `""` 은 비어 있어 데이터를 가리키지 않는다.
            const size_t quote = token._spelling.find( '"' );
            if ( quote != string::npos && token._spelling.size() - quote > 2 )
                return true;
        }
        return false;
    }

    vector<string> SourceTokens::readEventParameterNames( const CXCursor fieldCursor )
    {
        vector<SourceTokensInternal::SourceToken> listToken = SourceTokensInternal::tokenizeCursor( fieldCursor );
        auto                                      findStart = [&listToken]() -> size_t
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
                listToken   = SourceTokensInternal::tokenizeCursor( aliasDeclaration );
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
            const int32   delta    = SourceTokensInternal::getBracketDelta( spelling );
            if ( depth == 1 && ( spelling == "," || ( delta < 0 && spelling == ")" ) ) )
            {
                if ( tokenIndex > chunkStart )
                    listName.push_back( SourceTokensInternal::findParameterName( listToken, chunkStart, tokenIndex ) );
                chunkStart = tokenIndex + 1;
            }
            depth += delta;
            if ( depth == 0 )
                break;
        }
        return listName;
    }
} // namespace sw
