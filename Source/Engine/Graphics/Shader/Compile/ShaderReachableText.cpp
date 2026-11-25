#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderReachableText.h"

#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    namespace
    {
        struct ShaderReachableTextInternal
        {
            static constexpr uint32 kNoName = 0xFFFFFFFFu;

            enum class TokenKind : uint8
            {
                Identifier,
                Number,
                String,
                Directive, ///< `#line` 이 아닌 전처리 지시 한 줄(`#pragma`)
                Punctuation
            };

            struct Token
            {
                string_view _text;
                TokenKind   _kind{ TokenKind::Punctuation };
                uint8       _bAttached{ SW_FALSE }; ///< 앞 토큰에 빈칸 없이 붙었다 — `a++b` 와 `a+ +b` 는 다른 글이다
            };

            /** @brief 토큰 [_begin, _end) 조각 하나입니다. 무리마다 이름 하나라도 쓰여야 남습니다(무리가 없으면 늘 남는다). */
            struct Piece
            {
                uint32         _begin{ 0 };
                uint32         _end{ 0 };
                vector<uint32> _listNameAny;                  ///< 첫째 무리의 이름들
                uint32         _ownerName{ kNoName };         ///< 둘째 무리 — 구조체 이름 하나
                uint8          _bContributesNames{ SW_TRUE }; ///< 남으면 안의 이름이 쓰인 것이 된다(이름공간의 여닫는 괄호는 아니다)
            };

            /** @brief 문장 하나를 읽은 결과입니다. */
            struct Statement
            {
                uint32 _end{ 0 };              ///< 문장 다음 자리
                uint32 _firstParen{ kNoName }; ///< 바깥 깊이의 첫 여는 괄호(초기화 식 앞)
                uint32 _structOpen{ kNoName }; ///< 이름 있는 구조체 정의(`struct X {…};`)면 여는 중괄호
                uint8  _bFunctionBody{ SW_FALSE };
                uint8  _bAssignment{ SW_FALSE }; ///< 바깥 깊이에 `=` 이 있다
                uint8  _bClosed{ SW_FALSE };     ///< 문장 끝을 찾았다
            };

            static bool isIdentifierStart( utf8 character ) { return ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' ) || character == '_'; }
            static bool isDigit( utf8 character ) { return '0' <= character && character <= '9'; }
            static bool isIdentifierPart( utf8 character ) { return isIdentifierStart( character ) || isDigit( character ); }
            static bool isConstantBufferWord( string_view text ) { return text == "cbuffer" || text == "tbuffer"; }
            static bool isStructWord( string_view text ) { return text == "struct" || text == "class"; }

            /** @brief 글 하나를 토큰 · 조각 · 이름 표로 나누는 상태입니다. 한 번의 `make` 가 하나를 씁니다. */
            class Analyzer
            {
            public:
                explicit Analyzer( string_view text )
                    : _text{ text }
                {
                    readTokens();
                }

                string makeReachable( string_view entryPoint )
                {
                    if ( parse( 0, static_cast<uint32>( _listToken.size() ), kNoName ) == false )
                    {
                        _listPiece.clear();
                        addPiece( 0, static_cast<uint32>( _listToken.size() ), {}, kNoName, SW_FALSE );
                    }
                    markReachable( internName( entryPoint ) );
                    string result;
                    result.reserve( _text.size() / 2 );
                    for ( size_t pieceIndex = 0; pieceIndex < _listPiece.size(); ++pieceIndex )
                    {
                        if ( _listKept[pieceIndex] == 0 )
                            continue;
                        for ( uint32 tokenIndex = _listPiece[pieceIndex]._begin; tokenIndex < _listPiece[pieceIndex]._end; ++tokenIndex )
                        {
                            if ( _listToken[tokenIndex]._bAttached == SW_FALSE )
                                result.push_back( ' ' );
                            result.append( _listToken[tokenIndex]._text );
                        }
                        result.push_back( '\n' );
                    }
                    return result;
                }

            private:
                void readTokens()
                {
                    _listToken.reserve( _text.size() / 4 );
                    size_t index      = 0;
                    bool   bLineStart = true;
                    uint8  bAttached  = SW_FALSE;
                    while ( index < _text.size() )
                    {
                        const utf8 character = _text[index];
                        if ( character == '\n' || character == ' ' || character == '\t' || character == '\r' )
                        {
                            bLineStart = bLineStart || character == '\n';
                            bAttached  = SW_FALSE;
                            ++index;
                            continue;
                        }
                        const size_t begin = index;
                        if ( bLineStart && character == '#' )
                        {
                            while ( index < _text.size() && _text[index] != '\n' )
                                ++index;
                            addDirective( _text.substr( begin, index - begin ) );
                            bAttached = SW_FALSE;
                            continue;
                        }
                        bLineStart           = false;
                        const TokenKind kind = skipToken( index );
                        _listToken.push_back( Token{ _text.substr( begin, index - begin ), kind, bAttached } );
                        bAttached = SW_TRUE;
                    }
                }

                /** @brief @p inoutIndex 에서 시작하는 토큰을 지나고 그 종류를 돌려줍니다. */
                TokenKind skipToken( size_t& inoutIndex ) const
                {
                    const size_t begin     = inoutIndex;
                    const utf8   character = _text[inoutIndex];
                    const bool   bFraction = character == '.' && inoutIndex + 1 < _text.size() && isDigit( _text[inoutIndex + 1] );
                    if ( isIdentifierStart( character ) )
                    {
                        while ( inoutIndex < _text.size() && isIdentifierPart( _text[inoutIndex] ) )
                            ++inoutIndex;
                        return TokenKind::Identifier;
                    }
                    if ( isDigit( character ) || bFraction )
                    {
                        while ( inoutIndex < _text.size() && ( isIdentifierPart( _text[inoutIndex] ) || _text[inoutIndex] == '.' || isExponentSign( begin, inoutIndex ) ) )
                            ++inoutIndex;
                        return TokenKind::Number;
                    }
                    ++inoutIndex;
                    if ( character != '"' )
                        return TokenKind::Punctuation;
                    while ( inoutIndex < _text.size() && _text[inoutIndex] != '"' && _text[inoutIndex] != '\n' )
                        ++inoutIndex;
                    inoutIndex += inoutIndex < _text.size() && _text[inoutIndex] == '"' ? 1 : 0;
                    return TokenKind::String;
                }

                /** @brief 숫자 [@p begin, @p index) 바로 뒤 글자가 지수의 부호(`1e-5`)면 true 입니다. */
                bool isExponentSign( size_t begin, size_t index ) const
                {
                    const bool bSign = _text[index] == '+' || _text[index] == '-';
                    return bSign && index > begin && ( _text[index - 1] == 'e' || _text[index - 1] == 'E' );
                }

                void addDirective( string_view line )
                {
                    while ( line.empty() == false && ( line.back() == '\r' || line.back() == ' ' || line.back() == '\t' ) )
                        line.remove_suffix( 1 );
                    size_t name = 1;
                    while ( name < line.size() && ( line[name] == ' ' || line[name] == '\t' ) )
                        ++name;
                    if ( line.substr( name, 4 ) != "line" )
                        _listToken.push_back( Token{ line, TokenKind::Directive, SW_FALSE } );
                }

                bool isPunctuation( uint32 tokenIndex, utf8 character ) const
                {
                    return _listToken[tokenIndex]._kind == TokenKind::Punctuation && _listToken[tokenIndex]._text[0] == character;
                }
                bool        isIdentifier( uint32 tokenIndex ) const { return _listToken[tokenIndex]._kind == TokenKind::Identifier; }
                string_view getText( uint32 tokenIndex ) const { return _listToken[tokenIndex]._text; }

                /** @brief 이름의 번호입니다(처음이면 만든다). */
                uint32 internName( string_view name ) { return _mapName.emplace( name, static_cast<uint32>( _mapName.size() ) ).first->second; }

                /** @brief 여는 중괄호 @p openIndex 와 짝인 닫는 중괄호 다음 자리입니다. 짝이 없으면 @p end 보다 큽니다. */
                uint32 findBlockEnd( uint32 openIndex, uint32 end ) const
                {
                    uint32 depth = 1;
                    for ( uint32 index = openIndex + 1; index < end; ++index )
                    {
                        if ( isPunctuation( index, '{' ) )
                            ++depth;
                        else if ( isPunctuation( index, '}' ) && --depth == 0 )
                            return index + 1;
                    }
                    return end + 1;
                }

                /** @brief 문장의 앞 낱말 셋 중 하나가 묶음 선언(`struct` · `cbuffer` …)이면 true 입니다(앞에 `static` 같은 것이 올 수 있다). */
                bool hasAggregateHead( uint32 begin, uint32 end ) const
                {
                    for ( uint32 index = begin; index < end && index < begin + 3; ++index )
                    {
                        const string_view text = getText( index );
                        if ( isStructWord( text ) || isConstantBufferWord( text ) || text == "enum" )
                            return true;
                    }
                    return false;
                }

                /** @brief 전역 선언 [@p begin, @p end) 가 선언하는 이름을 @p outListName 에 담습니다. 선언 이름을 모르는 꼴이면 비워 둡니다(늘 남는다). */
                void collectDeclaredNames( uint32 begin, uint32 end, vector<uint32>& outListName )
                {
                    static constexpr string_view kArrAlwaysKept[] = { "struct", "class", "enum", "[", "typedef", "using", "export", "extern", "template", "interface" };
                    const string_view            head             = getText( begin );
                    for ( const string_view kept : kArrAlwaysKept )
                    {
                        if ( head == kept )
                            return;
                    }
                    const bool  bConstantBuffer = isConstantBufferWord( head );
                    const int32 nameDepth       = bConstantBuffer ? 1 : 0; // 상수 버퍼의 멤버는 전역 이름이다
                    if ( bConstantBuffer && begin + 1 < end )
                        outListName.push_back( internName( getText( begin + 1 ) ) );
                    int32 depth = 0;
                    for ( uint32 index = begin; index < end; ++index )
                    {
                        const Token& token = _listToken[index];
                        if ( token._kind == TokenKind::Punctuation )
                        {
                            const utf8 character = token._text[0];
                            depth += character == '(' || character == '[' || character == '{' ? 1 : 0;
                            depth -= character == ')' || character == ']' || character == '}' ? 1 : 0;
                            continue;
                        }
                        const bool bName = token._kind == TokenKind::Identifier && depth == nameDepth && index + 1 < end && _listToken[index + 1]._kind == TokenKind::Punctuation;
                        if ( bName && string_view( ";:[=," ).find( getText( index + 1 )[0] ) != string_view::npos )
                            outListName.push_back( internName( token._text ) );
                    }
                }

                void addPiece( uint32 begin, uint32 end, vector<uint32> listNameAny, uint32 ownerName, uint8 bContributesNames = SW_TRUE )
                {
                    _listPiece.push_back( Piece{ begin, end, std::move( listNameAny ), ownerName, bContributesNames } );
                }

                /** @brief @p begin 에서 시작하는 문장의 끝과 꼴을 봅니다. */
                Statement readStatement( uint32 begin, uint32 end, bool bInStruct ) const
                {
                    Statement  statement;
                    int32      parenDepth   = 0;
                    int32      bracketDepth = 0;
                    uint32     index        = begin;
                    const bool bAggregate   = hasAggregateHead( begin, end );
                    while ( index < end && statement._bClosed == SW_FALSE )
                    {
                        const utf8 character = _listToken[index]._kind == TokenKind::Punctuation ? _listToken[index]._text[0] : utf8( 0 );
                        const bool bOuter    = parenDepth == 0 && bracketDepth == 0;
                        ++index;
                        switch ( character )
                        {
                            case '(':
                            {
                                if ( bOuter && statement._firstParen == kNoName && statement._bAssignment == SW_FALSE )
                                    statement._firstParen = index - 1;
                                ++parenDepth;
                                break;
                            }
                            case ')':
                            {
                                --parenDepth;
                                break;
                            }
                            case '[':
                            {
                                ++bracketDepth;
                                break;
                            }
                            case ']':
                            {
                                --bracketDepth;
                                break;
                            }
                            case '=':
                            {
                                if ( bOuter )
                                    statement._bAssignment = SW_TRUE;
                                break;
                            }
                            case ';':
                            {
                                if ( bOuter )
                                    statement._bClosed = SW_TRUE;
                                break;
                            }
                            case '{':
                            {
                                if ( parenDepth != 0 )
                                    break;
                                const uint32 open = index - 1;
                                index             = findBlockEnd( open, end );
                                if ( index > end )
                                    return statement; // 짝이 없다 — 닫히지 않은 문장이다
                                const bool bSemicolon = index < end && isPunctuation( index, ';' );
                                const bool bFunction  = statement._firstParen != kNoName && statement._bAssignment == SW_FALSE && bAggregate == false;
                                const bool bStruct    = bInStruct == false && isStructWord( getText( begin ) ) && bSemicolon && isIdentifier( begin + 1 );
                                if ( bFunction )
                                {
                                    statement._bFunctionBody = SW_TRUE;
                                    statement._bClosed       = SW_TRUE;
                                }
                                else if ( bStruct || isConstantBufferWord( getText( begin ) ) )
                                {
                                    statement._structOpen = bStruct ? open : kNoName;
                                    statement._bClosed    = SW_TRUE;
                                    index += bSemicolon ? 1 : 0;
                                }
                                break;
                            }
                            default:
                            {
                                break;
                            }
                        }
                    }
                    statement._end = index;
                    return statement;
                }

                /** @brief 문장이 함수(정의 · 선언)면 이름 토큰의 자리, 아니면 `kNoName` 입니다. */
                uint32 findFunctionName( uint32 begin, const Statement& statement ) const
                {
                    const uint32 paren = statement._firstParen;
                    if ( paren == kNoName || paren <= begin || statement._bAssignment == SW_TRUE || isIdentifier( paren - 1 ) == false )
                        return kNoName;
                    const string_view name       = getText( paren - 1 );
                    const bool        bPlacement = name == "register" || name == "packoffset"; // 자원의 자리 적기다
                    const bool        bPrototype = statement._bFunctionBody == SW_FALSE && isPunctuation( statement._end - 1, ';' ) && isConstantBufferWord( getText( begin ) ) == false;
                    return bPlacement == false && ( statement._bFunctionBody == SW_TRUE || bPrototype ) ? paren - 1 : kNoName;
                }

                /** @brief 토큰 [@p index, @p end) 를 조각으로 나눕니다. 구조체 안이면 @p structName 이 그 이름입니다. 괄호가 맞지 않으면 false 입니다. */
                [[nodiscard]] bool parse( uint32 index, uint32 end, uint32 structName )
                {
                    while ( index < end )
                    {
                        if ( _listToken[index]._kind == TokenKind::Directive )
                        {
                            addPiece( index, index + 1, {}, kNoName );
                            ++index;
                            continue;
                        }
                        if ( structName == kNoName && getText( index ) == "namespace" )
                        {
                            uint32 open = index;
                            while ( open < end && isPunctuation( open, '{' ) == false )
                                ++open;
                            const uint32 blockEnd = open < end ? findBlockEnd( open, end ) : end + 1;
                            if ( blockEnd > end )
                                return false;
                            addPiece( index, open + 1, {}, kNoName, SW_FALSE );
                            if ( parse( open + 1, blockEnd - 1, kNoName ) == false )
                                return false;
                            addPiece( blockEnd - 1, blockEnd, {}, kNoName, SW_FALSE );
                            index = blockEnd;
                            continue;
                        }
                        const Statement statement = readStatement( index, end, structName != kNoName );
                        if ( statement._bClosed == SW_FALSE )
                            return false;
                        if ( statement._structOpen != kNoName )
                        {
                            const uint32 name = internName( getText( index + 1 ) );
                            addPiece( index, statement._structOpen + 1, { name }, kNoName );
                            if ( parse( statement._structOpen + 1, statement._end - 2, name ) == false )
                                return false;
                            addPiece( statement._end - 2, statement._end, { name }, kNoName );
                        }
                        else
                        {
                            addStatementPiece( index, statement, structName );
                        }
                        index = statement._end;
                    }
                    return true;
                }

                void addStatementPiece( uint32 begin, const Statement& statement, uint32 structName )
                {
                    const uint32 nameIndex = findFunctionName( begin, statement );
                    if ( nameIndex != kNoName )
                    {
                        // 구조체 밖의 정의(`반환 X::이름(`)는 X 도 쓰여야 한다.
                        const bool bQualified = nameIndex >= begin + 3 && isPunctuation( nameIndex - 1, ':' ) && isPunctuation( nameIndex - 2, ':' ) && isIdentifier( nameIndex - 3 );
                        uint32     owner      = structName;
                        if ( owner == kNoName && bQualified )
                            owner = internName( getText( nameIndex - 3 ) );
                        addPiece( begin, statement._end, { internName( getText( nameIndex ) ) }, owner );
                        return;
                    }
                    vector<uint32> listName;
                    if ( structName != kNoName )
                        listName.push_back( structName );
                    else
                        collectDeclaredNames( begin, statement._end, listName );
                    addPiece( begin, statement._end, std::move( listName ), kNoName );
                }

                /** @brief 남으려면 채워야 하는 무리의 비트입니다(1 = 첫째 무리, 2 = 구조체 이름). */
                uint8 getRequiredGroups( size_t pieceIndex ) const
                {
                    const Piece& piece = _listPiece[pieceIndex];
                    return static_cast<uint8>( ( piece._listNameAny.empty() ? 0 : 1 ) | ( piece._ownerName == kNoName ? 0 : 2 ) );
                }

                /** @brief 조각을 남기고 안의 이름을 쓰인 것으로 적습니다. */
                void keepPiece( size_t pieceIndex )
                {
                    _listKept[pieceIndex] = 1;
                    const uint32 begin    = _listPiece[pieceIndex]._begin;
                    const uint32 end      = _listPiece[pieceIndex]._bContributesNames == SW_TRUE ? _listPiece[pieceIndex]._end : begin;
                    for ( uint32 tokenIndex = begin; tokenIndex < end; ++tokenIndex )
                    {
                        const Token& token = _listToken[tokenIndex];
                        if ( token._kind == TokenKind::Identifier )
                            useName( token._text );
                        else if ( token._kind == TokenKind::String || token._kind == TokenKind::Directive )
                            useNamesInText( token._text );
                    }
                }

                void useNamesInText( string_view text )
                {
                    size_t index = 0;
                    while ( index < text.size() )
                    {
                        const size_t begin = index;
                        while ( index < text.size() && isIdentifierPart( text[index] ) )
                            ++index;
                        if ( index > begin )
                            useName( text.substr( begin, index - begin ) );
                        else
                            ++index;
                    }
                }

                void useName( string_view text )
                {
                    const uint32 name = internName( text );
                    if ( name >= _listUsed.size() )
                        _listUsed.resize( name + 1, 0 );
                    if ( _listUsed[name] != 0 )
                        return;
                    _listUsed[name] = 1;
                    _listPendingName.push_back( name );
                }

                /** @brief 이름 @p entryName 에서 시작해 남는 조각을 `_listKept` 에 적습니다. 이름이 쓰이면 그 이름을 기다리던 무리를 채우고, 다 찬 조각을 남긴다. */
                void markReachable( uint32 entryName )
                {
                    vector<vector<uint32>> listWaiting( _mapName.size() ); // 이름마다 기다리는 조각 * 2 + 무리(0 첫째, 1 구조체)
                    for ( uint32 pieceIndex = 0; pieceIndex < static_cast<uint32>( _listPiece.size() ); ++pieceIndex )
                    {
                        for ( const uint32 name : _listPiece[pieceIndex]._listNameAny )
                            listWaiting[name].push_back( pieceIndex * 2 );
                        if ( _listPiece[pieceIndex]._ownerName != kNoName )
                            listWaiting[_listPiece[pieceIndex]._ownerName].push_back( pieceIndex * 2 + 1 );
                    }
                    vector<uint8> listFilled( _listPiece.size(), 0 );
                    _listKept.assign( _listPiece.size(), 0 );
                    _listPendingName.push_back( entryName );
                    for ( size_t pieceIndex = 0; pieceIndex < _listPiece.size(); ++pieceIndex )
                    {
                        if ( getRequiredGroups( pieceIndex ) == 0 )
                            keepPiece( pieceIndex );
                    }
                    while ( _listPendingName.empty() == false )
                    {
                        const uint32 name = _listPendingName.back();
                        _listPendingName.pop_back();
                        if ( name >= listWaiting.size() )
                            continue; // 조각을 나눈 뒤에 처음 나온 이름이다 — 선언이 없다(내장 함수 · 지역 변수)
                        for ( const uint32 waiting : listWaiting[name] )
                        {
                            const size_t pieceIndex = waiting / 2;
                            listFilled[pieceIndex]  = static_cast<uint8>( listFilled[pieceIndex] | ( 1u << ( waiting % 2 ) ) );
                            if ( _listKept[pieceIndex] == 0 && listFilled[pieceIndex] == getRequiredGroups( pieceIndex ) )
                                keepPiece( pieceIndex );
                        }
                    }
                }

            private:
                string_view                        _text;
                vector<Token>                      _listToken;
                vector<Piece>                      _listPiece;
                vector<uint8>                      _listKept;        ///< 조각마다 남는지(`markReachable`)
                vector<uint8>                      _listUsed;        ///< 이름 번호마다 쓰였는지
                vector<uint32>                     _listPendingName; ///< 쓰였으나 아직 조각을 깨우지 않은 이름
                unordered_map<string_view, uint32> _mapName;         ///< 이름 → 번호(글은 `_text` 와 진입점 이름을 가리킨다)
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    string ShaderReachableText::make( string_view preprocessed, string_view entryPoint )
    {
        return ShaderReachableTextInternal::Analyzer{ preprocessed }.makeReachable( entryPoint );
    }
} // namespace sw
