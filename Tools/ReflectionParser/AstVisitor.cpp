#include "pch.h"

#include "ReflectionParser/AstVisitor.h"

#include "Core/Common/Types.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Common/Common.h"
#include "Engine/Reflection/ReflectUnits.h"
#include "Engine/Reflection/ReflectionEnumNames.h"

#include "ReflectionParser/AnnotationApply.h"
#include "ReflectionParser/AnnotationFields.h"
#include "ReflectionParser/ContainerTypeMap.h"
#include "ReflectionParser/ParserConfig.h"
#include "ReflectionParser/ParserDefines.h"
#include "ReflectionParser/ParserUtil.h"
#include "ReflectionParser/TypeNameMap.h"

SW_LOG_CALLER( "AstVisitor" );
namespace sw
{
    namespace
    {
        struct AstVisitorInternal
        {
            // ------------------------------------------------------------------------------
            // A) libclang 문자열 · 이름
            // ------------------------------------------------------------------------------
            /** @brief libclang 문자열(CXString)을 string 으로 복사한 뒤 해제합니다. */
            static string takeString( const CXString cxString )
            {
                string      result;
                const utf8* pText = clang_getCString( cxString );
                if ( pText != nullptr )
                    result = pText;
                clang_disposeString( cxString );
                return result;
            }

            /** @brief CXString 을 힙 할당 없이 비교한 뒤 해제합니다. */
            static bool isStringEqual( const CXString cxString, const string_view target )
            {
                const utf8* pText  = clang_getCString( cxString );
                const bool  bEqual = ( pText != nullptr && string_view( pText ) == target );
                clang_disposeString( cxString );
                return bEqual;
            }

            static string getCursorSpelling( const CXCursor cursor )
            {
                return takeString( clang_getCursorSpelling( cursor ) );
            }

            /** @brief 커서의 네임스페이스 포함 이름(FQN)을 만듭니다. 이름 없는 네임스페이스는 건너뜁니다. */
            static string makeFullyQualifiedName( const CXCursor cursor )
            {
                vector<string> listPart;
                CXCursor       current = cursor;
                while ( clang_getCursorKind( current ) != CXCursor_TranslationUnit )
                {
                    string spelling = getCursorSpelling( current );
                    if ( spelling.empty() == false )
                        listPart.push_back( std::move( spelling ) );
                    current = clang_getCursorSemanticParent( current );
                }

                StringBuilder<constant::kMaxBuffer1024> fqn;
                for ( auto it = listPart.rbegin(); it != listPart.rend(); ++it )
                {
                    if ( fqn.size() > 0 )
                        fqn.append( "::" );
                    fqn.append( *it );
                }
                return string( fqn.view() );
            }

            /** @brief 오류 메시지에 쓸 `타입::멤버` 이름입니다. */
            static string makeMemberOwnerName( const string_view ownerFQN, const string_view memberName )
            {
                StringBuilder<constant::kMaxBuffer1024> b;
                b.appendFormat( "%#::%#", ownerFQN, memberName );
                return string( b.view() );
            }

            // ------------------------------------------------------------------------------
            // B) 애노테이션 찾기 — 자식 속성(AnnotateAttr)이 원본이고, 소스 텍스트는 그것이 빠졌을 때의 폴백이다
            //
            // 묻는 쪽은 애노테이션 한 벌(`ReflectAnnotationDesc` — 접두사와 매크로 철자)을 넘긴다. 자식 속성 검색과 소스 폴백을
            // 호출부마다 조합하지 않는다.
            // ------------------------------------------------------------------------------
            struct AttrSearch
            {
                string_view _prefix;
                string      _spelling;
            };

            static CXChildVisitResult attrSearchVisitor( CXCursor cursor, CXCursor, CXClientData data )
            {
                const CXCursorKind kind = clang_getCursorKind( cursor );
                if ( kind != CXCursor_AnnotateAttr && kind != CXCursor_UnexposedAttr )
                    return CXChildVisit_Continue;

                AttrSearch*    pSearch    = static_cast<AttrSearch*>( data );
                const CXString cxSpelling = clang_getCursorSpelling( cursor );
                const utf8*    pText      = clang_getCString( cxSpelling );
                const bool     bMatch     = pText != nullptr && string_view( pText ).find( pSearch->_prefix ) != string_view::npos;
                if ( bMatch )
                    pSearch->_spelling = pText;
                clang_disposeString( cxSpelling );
                return bMatch ? CXChildVisit_Break : CXChildVisit_Continue;
            }

            /** @brief 자식 속성 중 접두사를 담은 첫 애노테이션의 철자입니다. 없으면 빈 문자열입니다. */
            static string findAnnotateAttr( const CXCursor cursor, const string_view prefix )
            {
                AttrSearch search{ prefix, {} };
                clang_visitChildren( cursor, attrSearchVisitor, &search );
                return std::move( search._spelling );
            }

            /** @brief 파일 내용 캐시입니다. 한 헤더의 여러 커서가 디스크를 거듭 읽지 않게 합니다(스레드마다 한 벌). */
            static const string& getCachedFileContent( const string& path )
            {
                thread_local unordered_map<string, string> s_mapPathToContent;
                const auto                                 it = s_mapPathToContent.find( path );
                if ( it != s_mapPathToContent.end() )
                    return it->second;

                // 못 읽으면 빈 글로 캐시한다 — 그 헤더의 애노테이션을 찾지 못하니 알린다.
                string content;
                if ( FileUtil::readTextFile( path, content ) == false )
                    SW_LOG_ERROR( "Could not read '%#' - annotations in it are not seen", path );
                return s_mapPathToContent.emplace( path, std::move( content ) ).first->second;
            }

            /** @brief 커서 바로 앞의 소스 텍스트 창입니다. 길이는 parser_config 의 `tuning.source_lookback_bytes`(기본 512)입니다. */
            struct SourceWindow
            {
                const string* _pContent;
                size_t        _windowStart;
                string_view   _window;

                SourceWindow()
                    : _pContent{ nullptr }
                    , _windowStart{ 0 }
                    , _window{}
                {
                }

                bool isValid() const noexcept { return _pContent != nullptr; }
            };

            static SourceWindow makeSourceWindow( const CXCursor cursor, const ParserConfig& config )
            {
                SourceWindow result;
                CXFile       file   = nullptr;
                uint32       offset = 0;
                clang_getFileLocation( clang_getCursorLocation( cursor ), &file, nullptr, nullptr, &offset );
                if ( file == nullptr )
                    return result;

                const string& content = getCachedFileContent( takeString( clang_getFileName( file ) ) );
                if ( content.empty() || offset > content.size() )
                    return result;

                const size_t lookback = config._sourceLookbackBytes;
                result._pContent      = &content;
                result._windowStart   = ( offset > lookback ) ? ( offset - lookback ) : 0;
                result._window        = string_view( content.data() + result._windowStart, offset - result._windowStart );
                return result;
            }

            /** @brief 검색 구간 안에서 주석 밖에 있는 문자열을 뒤에서부터 찾습니다. */
            static size_t rfindOutsideComments( const string_view window, const string_view searchStr )
            {
                size_t searchEnd = string::npos;
                while ( true )
                {
                    const size_t pos = window.rfind( searchStr, searchEnd );
                    if ( pos == string_view::npos )
                        return string::npos;

                    bool bInLineComment  = false;
                    bool bInBlockComment = false;
                    for ( size_t index = 0; index < pos; ++index )
                    {
                        if ( bInLineComment == false && bInBlockComment == false )
                        {
                            if ( window[index] == '/' && index + 1 < pos )
                            {
                                if ( window[index + 1] == '/' )
                                {
                                    bInLineComment = true;
                                    ++index;
                                }
                                else if ( window[index + 1] == '*' )
                                {
                                    bInBlockComment = true;
                                    ++index;
                                }
                            }
                        }
                        else if ( bInLineComment )
                        {
                            if ( window[index] == '\n' )
                                bInLineComment = false;
                        }
                        else if ( bInBlockComment )
                        {
                            const bool bBlockCommentEnd = window[index] == '*' && index + 1 < pos && window[index + 1] == '/';
                            if ( bBlockCommentEnd )
                            {
                                bInBlockComment = false;
                                ++index;
                            }
                        }
                    }

                    if ( bInLineComment == false && bInBlockComment == false )
                        return pos;
                    if ( pos == 0 )
                        return string::npos;
                    searchEnd = pos - 1;
                }
            }

            /**
             * @brief 매크로와 커서 사이에 다른 선언의 경계(`;` `{` `}`)가 없는지 봅니다. 있으면 그 매크로는 앞 선언의 것입니다.
             * @details 따옴표 안(애노테이션 문자열)은 경계가 아닙니다 — 따옴표 안도 세면 `Tooltip = "…; …"` 하나로 그 타입의
             *          `REFLECT()` 가 "앞 선언의 것" 이 되어, 멤버마다 "REFLECT() 가 없다" 는 엉뚱한 오류로 빌드가 선다.
             */
            static bool hasNoDeclarationBoundary( const string_view text )
            {
                bool bInQuote = false;
                for ( size_t index = 0; index < text.size(); ++index )
                {
                    const utf8 character = text[index];
                    if ( bInQuote )
                    {
                        if ( character == '\\' )
                            ++index; // 이스케이프한 글자는 따옴표를 닫지 않는다
                        else if ( character == '"' )
                            bInQuote = false;
                        continue;
                    }
                    const bool bStatementBoundary = character == ';' || character == '{' || character == '}';
                    if ( character == '"' )
                        bInQuote = true;
                    else if ( bStatementBoundary )
                        return false;
                }
                return true;
            }

            /**
             * @brief AST 에서 속성 커서가 빠졌을 때, 커서 앞 소스 창에 그 애노테이션(매크로 또는 `annotate("…`)이 있는지 봅니다.
             * @details 기본 매크로만 봅니다. `ENUM;BitFlag` 같은 세부 태그에는 폴백하지 않습니다.
             */
            static bool hasSourceAnnotation( const CXCursor cursor, const ReflectAnnotationDesc& desc, const ParserConfig& config )
            {
                const SourceWindow source = makeSourceWindow( cursor, config );
                if ( source.isValid() == false )
                    return false;

                const size_t macroPos = rfindOutsideComments( source._window, desc._pMacroOpen );
                if ( macroPos != string_view::npos && hasNoDeclarationBoundary( source._window.substr( macroPos ) ) )
                    return true;

                StringBuilder<constant::kMaxBuffer128> needle;
                needle.appendFormat( "annotate(\"%#", desc._pPrefix );
                const size_t annotatePos = rfindOutsideComments( source._window, needle.view() );
                return annotatePos != string_view::npos && hasNoDeclarationBoundary( source._window.substr( annotatePos ) );
            }

            /**
             * @brief 커서 앞의 소스에서 매크로 괄호 `(...)` 안을 읽어 `PREFIX;args` 꼴의 애노테이션 철자를 다시 만듭니다.
             * @return 매크로가 없거나 앞 선언의 것이면 빈 문자열
             */
            static string extractSourceAnnotation( const CXCursor cursor, const ReflectAnnotationDesc& desc, const ParserConfig& config )
            {
                const SourceWindow source = makeSourceWindow( cursor, config );
                if ( source.isValid() == false )
                    return {};

                const string_view macroOpen = desc._pMacroOpen;
                const size_t      macroPos  = rfindOutsideComments( source._window, macroOpen );
                if ( macroPos == string_view::npos )
                    return {};

                // 매크로와 커서 사이에 경계가 있으면 앞 선언의 것이다(`hasSourceAnnotation` 과 같은 판정 — 따옴표 안은 보지 않는다).
                if ( hasNoDeclarationBoundary( source._window.substr( macroPos ) ) == false )
                    return {};

                // 매크로 괄호 깊이를 따라 읽는다. 인자는 창 끝(커서)을 넘어 끝날 수 있어 파일 내용에서 읽는다.
                const string& content   = *source._pContent;
                const size_t  argsStart = source._windowStart + macroPos + macroOpen.size();
                size_t        charIndex = argsStart;
                int32         depth     = 1;
                bool          bInQuote  = false;
                while ( charIndex < content.size() && depth > 0 )
                {
                    const utf8 character = content[charIndex++];
                    if ( bInQuote )
                    {
                        if ( character == '\\' )
                            ++charIndex; // 이스케이프한 글자(`\"`)는 따옴표를 닫지 않는다
                        else if ( character == '"' )
                            bInQuote = false;
                        continue;
                    }
                    if ( character == '"' )
                        bInQuote = true;
                    else if ( character == '(' )
                        ++depth;
                    else if ( character == ')' )
                        --depth;
                }
                if ( depth != 0 || charIndex <= argsStart )
                    return {}; // 괄호가 닫히지 않았다(파일 끝) — 컴파일러가 먼저 알린다

                StringBuilder<constant::kMaxBuffer1024> b;
                b.append( desc._pPrefix );
                b.append( string_view( content.data() + argsStart, charIndex - argsStart - 1 ) );
                return string( b.view() );
            }

            /** @brief 그 애노테이션이 붙어 있는지 — 자식 속성, 없으면 소스 창. */
            static bool hasAnnotation( const CXCursor cursor, const ReflectAnnotationDesc& desc, const ParserConfig& config )
            {
                return findAnnotateAttr( cursor, desc._pPrefix ).empty() == false || hasSourceAnnotation( cursor, desc, config );
            }

            /** @brief 애노테이션 철자 — 자식 속성, 없으면 소스에서 다시 만든다. 없으면 빈 문자열입니다. */
            static string readAnnotation( const CXCursor cursor, const ReflectAnnotationDesc& desc, const ParserConfig& config )
            {
                string spelling = findAnnotateAttr( cursor, desc._pPrefix );
                if ( spelling.empty() )
                    spelling = extractSourceAnnotation( cursor, desc, config );
                return spelling;
            }

            /**
             * @brief 애노테이션 토큰을 DTO 에 넣고, AnnotationMeta.txt 에 없는 토큰이 있으면 알립니다.
             * @details 모르는 토큰을 **조용히 버리지 않습니다** — 예: 에디터 색 선택기 요청의 철자는 `Meta = "Color"` 인데 `PROPERTY( …, Color, … )`
             *          로 적으면 버려지고, 멤버 이름에 color 가 들어 있으면 인스펙터의 이름 휴리스틱이 증상을 가립니다.
             * @return 모르는 토큰이 없으면 true
             */
            template <typename TParsed>
            [[nodiscard]] static bool applyAnnotation( const string_view spelling, TParsed& target, const ParserSession& session,
                                                       const string_view owner )
            {
                vector<string> listUnknownToken;
                sw::AnnotationApply::apply( spelling, target, session._annotationMeta, listUnknownToken );

                const ReflectAnnotationDesc& desc = *getAnnotationScope<TParsed>()._pDesc;
                for ( const string& token : listUnknownToken )
                {
                    SW_LOG_ERROR( "ERROR: %#() on '%#' has an unknown token '%#'. Spellings are listed in AnnotationMeta.txt [%#]; "
                                  "a free-form editor hint is written Meta = \"Key\" or Meta = \"Key=Value\".",
                                  desc._pMacroName, owner, token, desc._pScope );
                }
                return listUnknownToken.empty();
            }

            // ------------------------------------------------------------------------------
            // C) 컨테이너 타입 트리 (Vector/Map 중첩)
            // ------------------------------------------------------------------------------
            /** @brief `T<A,B>` 에서 최외곽 템플릿 인자를 나눕니다. */
            static vector<string> extractTemplateArgs( const string_view typeStr )
            {
                const size_t start              = typeStr.find( '<' );
                const size_t end                = typeStr.rfind( '>' );
                const bool   bInvalidAngleRange = start == string_view::npos || end == string_view::npos || end <= start;
                if ( bInvalidAngleRange )
                    return {};
                return ParserUtil::splitCommaRespectingAngles( typeStr.substr( start + 1, end - start - 1 ) );
            }

            /** @brief 컨테이너 어노테이션을 찾을 기본 템플릿 · 레코드 커서입니다. 선언이 없으면 null 커서입니다. */
            static CXCursor findContainerDeclaration( const CXType type )
            {
                const CXType canonical = clang_getCanonicalType( type );
                CXCursor     result    = clang_getTypeDeclaration( canonical );
                if ( clang_Cursor_isNull( result ) )
                    return result;

                const CXCursor specialized = clang_getSpecializedCursorTemplate( result );
                if ( clang_Cursor_isNull( specialized ) == 0 )
                    result = specialized;
                return result;
            }

            /**
             * @brief 타입 선언의 REFLECT_CONTAINER(Kind [, WrapperStem]) 를 조회합니다.
             * @return 어노테이션된 컨테이너 메타를 찾으면 true
             */
            static bool findReflectContainer( const CXType type, const ParserConfig& config, ContainerKind& outKind, string& outWrapperStem )
            {
                const CXCursor declaration = findContainerDeclaration( type );
                if ( clang_Cursor_isNull( declaration ) )
                    return false;

                const ReflectAnnotationDesc& desc     = annotation::kReflectContainer;
                const string                 spelling = readAnnotation( declaration, desc, config );
                if ( spelling.empty() )
                    return false;

                const vector<string> listToken =
                    sw::AnnotationApply::splitAnnotationArgs( sw::AnnotationApply::annotationArgumentText( spelling, desc._pPrefix ) );
                if ( listToken.empty() )
                    return false;
                if ( tryParseContainerKind( listToken[0], outKind ) == false || outKind == ContainerKind::None )
                    return false;

                outWrapperStem = ( listToken.size() >= 2 ) ? listToken[1] : string( defaultContainerWrapperStem( outKind ) );
                return true;
            }

            /** @brief 노드를 그 종류 · 래퍼의 컨테이너로 표시합니다. */
            static void markContainer( ParsedContainerNode& node, const ContainerKind kind, const string& wrapperStem )
            {
                node._bIsContainer  = SW_TRUE;
                node._containerKind = kind;
                node._containerType = wrapperStem;
            }

            /** @brief 맵/시퀀스 템플릿 인자로 키·원소·중첩 노드를 채웁니다. */
            static shared_ptr<ParsedContainerNode> makeNestedContainer( const CXType type, const int32 numClangArgs, const int32 index,
                                                                        const string& spellingFallback, const ParserSession& session )
            {
                shared_ptr<ParsedContainerNode> nested;
                if ( 0 <= index && index < numClangArgs )
                {
                    const CXType argType = clang_Type_getTemplateArgumentAsType( type, static_cast<uint32>( index ) );
                    if ( argType.kind != CXType_Invalid )
                        nested = makeContainerFromType( argType, session );
                }
                if ( ( nested == nullptr || nested->_bIsContainer == SW_FALSE ) && spellingFallback.empty() == false )
                    nested = makeContainerFromSpelling( spellingFallback, session );
                if ( nested != nullptr && nested->_bIsContainer == SW_FALSE )
                    nested.reset();
                return nested;
            }

            /**
             * @brief 타입을 사용자 별칭을 벗긴 표기로 바꿉니다. 템플릿 인자 안의 별칭도 끝까지 벗깁니다(`vector<AccountID>` 는 `vector<uint64>`).
             * @details 원소 이름만 벗기고 컨테이너 이름을 적힌 대로 두면 프로퍼티의 기록 타입 이름(`vector<AccountID>`)이 별칭을 쓰지 않던 빌드(`vector<uint64>`)와 달라
             *          그 빌드가 쓴 레코드를 읽지 못합니다. 그래서 프로퍼티 이름, 원소 이름, 키 이름이 모두 이 함수를 거칩니다.
             *          인자 중 하나라도 타입이 아니면(`std::array<int32, 4>` 의 4) 인자를 다시 짜지 않습니다.
             * @return 별칭을 하나라도 벗겼으면 true 입니다. 거짓이면 @p outSpelling 은 clang 의 표기 그대로입니다.
             */
            [[nodiscard]] static bool tryResolveAliasSpelling( const CXType type, const ParserSession& session, string& outSpelling )
            {
                const CXType resolved = resolvePropertyAlias( type, session );
                const bool   bChanged = clang_equalTypes( type, resolved ) == 0;
                outSpelling           = takeString( clang_getTypeSpelling( resolved ) );
                // 컨테이너의 인자만 다시 짠다. 알려진 이름(`string` 은 `basic_string<utf8>`)의 인자를 벗기면 `string<char>` 같은 모르는 이름이 된다.
                ContainerKind kind = ContainerKind::None;
                string        wrapperStem;
                const bool    bContainer = session._containerTypeMap.match( outSpelling ) != nullptr || findReflectContainer( resolved, session._config, kind, wrapperStem );
                const int32   argCount   = clang_Type_getNumTemplateArguments( resolved );
                if ( bContainer == false || argCount <= 0 )
                    return bChanged;

                vector<string> listArgSpelling;
                bool           bArgChanged = false;
                for ( int32 argIndex = 0; argIndex < argCount; ++argIndex )
                {
                    const CXType argType = clang_Type_getTemplateArgumentAsType( resolved, static_cast<uint32>( argIndex ) );
                    if ( argType.kind == CXType_Invalid )
                        return bChanged;
                    string argSpelling;
                    if ( tryResolveAliasSpelling( argType, session, argSpelling ) )
                        bArgChanged = true;
                    listArgSpelling.push_back( argSpelling );
                }
                if ( bArgChanged == false )
                    return bChanged;

                string rebuilt = outSpelling.substr( 0, outSpelling.find( '<' ) ) + "<";
                for ( size_t argIndex = 0; argIndex < listArgSpelling.size(); ++argIndex )
                {
                    rebuilt += ( argIndex == 0 ? "" : ", " ) + listArgSpelling[argIndex];
                }
                outSpelling = rebuilt + ">";
                return true;
            }

            /**
             * @brief 템플릿 인자 하나(원소 · 키)의 타입 이름입니다. 인자 안의 사용자 별칭을 벗깁니다(`tryResolveAliasSpelling`).
             * @details 적힌 이름만 쓰면 `vector<AccountID>` 의 원소가 모르는 이름 `AccountID` 로 등록되고, 자기 기술형 바이너리가 그 원소를 읽지 못합니다.
             *          별칭이 없는 인자는 적힌 이름 그대로입니다. clang 타입이 없으면(표기만으로 만든 노드) 적힌 이름을 씁니다.
             */
            static string resolveTemplateArgName( const CXType type, const int32 numClangArgs, const int32 index, const string& spelling,
                                                  const ParserSession& session )
            {
                if ( 0 <= index && index < numClangArgs )
                {
                    const CXType argType = clang_Type_getTemplateArgumentAsType( type, static_cast<uint32>( index ) );
                    string       resolvedSpelling;
                    if ( argType.kind != CXType_Invalid && tryResolveAliasSpelling( argType, session, resolvedSpelling ) )
                        return session._typeNameMap.normalize( resolvedSpelling );
                }
                return session._typeNameMap.normalize( spelling );
            }

            static void fillContainerArgs( ParsedContainerNode& node, const string& typeSpelling, const CXType type,
                                           const ParserSession& session )
            {
                const vector<string> listArg      = extractTemplateArgs( typeSpelling );
                const int32          numClangArgs = ( type.kind == CXType_Invalid ) ? 0 : clang_Type_getNumTemplateArguments( type );

                if ( node._containerKind == ContainerKind::Map )
                {
                    if ( listArg.size() >= 2 )
                    {
                        node._keyTypeName     = resolveTemplateArgName( type, numClangArgs, 0, listArg[0], session );
                        node._elementTypeName = resolveTemplateArgName( type, numClangArgs, 1, listArg[1], session );
                        node._elementNested   = makeNestedContainer( type, numClangArgs, 1, listArg[1], session );
                    }
                }
                else if ( listArg.empty() == false )
                {
                    node._elementTypeName = resolveTemplateArgName( type, numClangArgs, 0, listArg[0], session );
                    node._elementNested   = makeNestedContainer( type, numClangArgs, 0, listArg[0], session );
                }
            }

            /** @brief 표기 문자열만으로 컨테이너 노드를 만듭니다 (clang 타입 없음). */
            static shared_ptr<ParsedContainerNode> makeContainerFromSpelling( const string& typeSpelling, const ParserSession& session )
            {
                shared_ptr<ParsedContainerNode> node = make_shared<ParsedContainerNode>();
                const ContainerTypeRule*        rule = session._containerTypeMap.match( typeSpelling );
                if ( rule == nullptr )
                    return node;

                markContainer( *node, rule->_kind, rule->_type );
                node->_typeName = rule->_match;
                CXType invalid{};
                invalid.kind = CXType_Invalid;
                fillContainerArgs( *node, typeSpelling, invalid, session );
                return node;
            }

            /**
             * @brief clang 타입(또는 표기 폴백)으로 컨테이너 트리를 만듭니다.
             * @details 타입 선언의 `REFLECT_CONTAINER` 가 이기고, 없으면 ReflectBuiltins 의 표기 규칙을 봅니다.
             */
            static shared_ptr<ParsedContainerNode> makeContainerFromType( const CXType type, const ParserSession& session )
            {
                shared_ptr<ParsedContainerNode> node = make_shared<ParsedContainerNode>();
                if ( type.kind == CXType_Invalid )
                    return node;

                // C 고정 배열(`int32 _arr[4]`)은 `std::array` 와 같은 고정 시퀀스다 — 래퍼(`ArrayWrapper`)가 둘을 같이 다룬다.
                if ( type.kind == CXType_ConstantArray )
                {
                    const CXType elementType = clang_getArrayElementType( type );
                    markContainer( *node, ContainerKind::Sequence, string( annotation::kFixedArrayWrapperStem ) );
                    node->_typeName        = annotation::kFixedArrayTypeName;
                    node->_elementTypeName = session._typeNameMap.normalize( takeString( clang_getTypeSpelling( elementType ) ) );
                    node->_elementNested   = makeContainerFromType( elementType, session );
                    if ( node->_elementNested != nullptr && node->_elementNested->_bIsContainer == SW_FALSE )
                        node->_elementNested.reset();
                    return node;
                }

                const string             spelling = takeString( clang_getTypeSpelling( type ) );
                const ContainerTypeRule* rule     = session._containerTypeMap.match( spelling );

                ContainerKind kind = ContainerKind::None;
                string        wrapperStem;
                if ( findReflectContainer( type, session._config, kind, wrapperStem ) )
                    markContainer( *node, kind, wrapperStem );
                else if ( rule != nullptr )
                    markContainer( *node, rule->_kind, rule->_type );
                else
                    return node;

                if ( rule != nullptr )
                    node->_typeName = rule->_match;
                fillContainerArgs( *node, spelling, type, session );
                return node;
            }

            /**
             * @brief 프로퍼티 타입이 사용자 별칭(`using` · `typedef`)이면 **알려진 이름**이 나올 때까지 벗겨 그 타입을 돌려줍니다.
             * @details 적힌 이름만 보면 `using ScoreList = sw::vector<int32>;` · `using Health = int32;` 로 적은 프로퍼티가 모르는 타입 이름
             *          (`ScoreList` · `Health`)으로 남는다 — 컨테이너로도 스칼라로도 읽히지 않아 직렬화가 그 값을 쓰지 못한다(JSON 에는 `"null"`).
             *          한 겹씩 벗기다가 컨테이너 · 표에 있는 이름(`int32` · `string` · `float3` …)이 나오면 거기서 멈추고, 별칭이 아닌 타입(구조체 ·
             *          enum)에 닿으면 그것을 쓴다. 그래서 `int32` · `sw::string` 처럼 이미 알려진 별칭은 이름 그대로다.
             */
            static CXType resolvePropertyAlias( const CXType type, const ParserSession& session )
            {
                CXType resolved = type; // 반환은 이 하나다(-Wnrvo)
                CXType current  = type;
                for ( int32 depth = 0; depth < 8 && current.kind != CXType_Invalid; ++depth )
                {
                    const string  spelling = takeString( clang_getTypeSpelling( current ) );
                    ContainerKind kind     = ContainerKind::None;
                    string        wrapperStem;
                    const CXType  named         = current.kind == CXType_Elaborated ? clang_Type_getNamedType( current ) : current;
                    const bool    bResolvedType = session._containerTypeMap.match( spelling ) != nullptr || findReflectContainer( current, session._config, kind, wrapperStem ) ||
                                               session._typeNameMap.isKnown( spelling ) || named.kind != CXType_Typedef;
                    if ( bResolvedType )
                    {
                        resolved = current;
                        break;
                    }
                    current = clang_getTypedefDeclUnderlyingType( clang_getTypeDeclaration( named ) );
                }
                return resolved;
            }

            /** @brief 필드 타입의 컨테이너 트리를 프로퍼티에 복사합니다. */
            static void fillContainerDetails( ParsedPropertyInfo& prop, const CXType fieldType, const ParserSession& session )
            {
                prop._containerTree = makeContainerFromType( fieldType, session );
                if ( prop._containerTree == nullptr || prop._containerTree->_bIsContainer == SW_FALSE )
                    return;

                prop._bIsContainer    = SW_TRUE;
                prop._containerKind   = prop._containerTree->_containerKind;
                prop._containerType   = prop._containerTree->_containerType;
                prop._elementTypeName = prop._containerTree->_elementTypeName;
                prop._keyTypeName     = prop._containerTree->_keyTypeName;
            }

            // ------------------------------------------------------------------------------
            // C-2) 고정 배치형 코덱. 필드 타입을 인코딩 표(`Source/Engine/Serialization/Fixed/README.md` 3.4)에 맞춰 분류합니다
            //
            // 선언(clang 타입)에서 읽습니다. 다른 헤더의 열거형 · 구조체도 이 번역 단위 안에 선언이 있으므로 같은 길로 읽습니다.
            // 표 밖의 타입은 오류가 아닙니다. 그 타입의 코덱이 없을 뿐이고, 코덱이 필요한 등록 API 가 이유와 함께 멈춥니다.
            // ------------------------------------------------------------------------------
            static constexpr int32 kMaxFixedTypeDepth = 4;

            static shared_ptr<ParsedFixedType> makeIneligibleFixedType( shared_ptr<ParsedFixedType> type, const string_view reason )
            {
                type->_kind   = FixedValueKind::Ineligible;
                type->_reason = string( reason );
                return type;
            }

            static bool isFixedIntegerKind( const CXTypeKind kind ) noexcept
            {
                return kind == CXType_Char_S || kind == CXType_SChar || kind == CXType_Char_U || kind == CXType_UChar || kind == CXType_Short ||
                       kind == CXType_UShort || kind == CXType_Int || kind == CXType_UInt || kind == CXType_Long || kind == CXType_ULong ||
                       kind == CXType_LongLong || kind == CXType_ULongLong;
            }

            static bool isFixedPointerKind( const CXTypeKind kind ) noexcept
            {
                return kind == CXType_Pointer || kind == CXType_LValueReference || kind == CXType_RValueReference || kind == CXType_MemberPointer ||
                       kind == CXType_BlockPointer || kind == CXType_ObjCObjectPointer;
            }

            /** @brief 문자 하나가 8 비트인 문자열(`sw::string` · `std::string`)인지 봅니다. 넓은 문자열은 표 밖입니다. */
            static bool isFixedStringType( const string_view normalizedName, const string_view canonicalSpelling ) noexcept
            {
                return normalizedName == "string" || StringUtil::startsWith( canonicalSpelling, "sw::basic_string<char>" ) ||
                       StringUtil::startsWith( canonicalSpelling, "std::basic_string<char," ) || canonicalSpelling == "std::basic_string<char>";
            }

            /** @brief 열거형 선언에서 열거자 · `ENUM( Count, Flags )` 를 읽습니다. 다른 헤더의 열거형이면 그 헤더의 수집이 애노테이션 오류를 알립니다. */
            static shared_ptr<ParsedFixedEnum> collectFixedEnum( const CXCursor declaration, const ParserSession& session )
            {
                shared_ptr<ParsedFixedEnum> result = make_shared<ParsedFixedEnum>();
                result->_fullyQualifiedName        = makeFullyQualifiedName( declaration );
                result->_underlyingTypeName =
                    session._typeNameMap.normalize( takeString( clang_getTypeSpelling( clang_getCanonicalType( clang_getEnumDeclIntegerType( declaration ) ) ) ) );
                clang_visitChildren( declaration, enumeratorCollectorVisitor, &result->_listEnumerator );

                string spelling = extractSourceAnnotation( declaration, annotation::kEnum, session._config );
                if ( spelling.empty() )
                    spelling = findAnnotateAttr( declaration, annotation::kEnumPrefix );
                if ( spelling.empty() )
                    return result;
                ParsedEnumInfo annotated;
                vector<string> listUnknownToken; // 그 열거형의 헤더를 수집할 때 알립니다. 여기서 다시 알리지 않습니다
                sw::AnnotationApply::apply( spelling, annotated, session._annotationMeta, listUnknownToken );
                result->_countEnumerator = string( ParserUtil::scopeLeaf( annotated._countEnumerator ) );
                result->_bIsBitFlag      = annotated._bIsBitFlag;
                return result;
            }

            /**
             * @brief 필드(또는 원소) 타입 하나를 분류합니다. 컨테이너는 원소를 재귀로 분류합니다.
             * @param type 사용자 별칭을 벗긴 선언 타입
             */
            static shared_ptr<ParsedFixedType> classifyFixedType( const CXType type, const ParserSession& session, const int32 depth )
            {
                shared_ptr<ParsedFixedType> result            = make_shared<ParsedFixedType>();
                const CXType                canonical         = clang_getCanonicalType( type );
                const string                canonicalSpelling = takeString( clang_getTypeSpelling( canonical ) );
                result->_typeName                             = session._typeNameMap.normalize( takeString( clang_getTypeSpelling( type ) ) );
                if ( depth > kMaxFixedTypeDepth )
                    return makeIneligibleFixedType( result, "containers nested too deeply" );

                const CXTypeKind kind = canonical.kind;
                if ( kind == CXType_Bool )
                {
                    result->_kind = FixedValueKind::Bool;
                    return result;
                }
                if ( isFixedIntegerKind( kind ) )
                {
                    result->_kind = FixedValueKind::Integer;
                    return result;
                }
                if ( kind == CXType_Float || kind == CXType_Double )
                {
                    result->_kind = ( kind == CXType_Float ) ? FixedValueKind::Float32 : FixedValueKind::Float64;
                    return result;
                }
                if ( kind == CXType_Enum )
                {
                    result->_kind = FixedValueKind::Enum;
                    result->_enum = collectFixedEnum( clang_getTypeDeclaration( canonical ), session );
                    return result;
                }
                if ( kind == CXType_ConstantArray )
                {
                    result->_kind    = FixedValueKind::FixedArray;
                    result->_element = classifyFixedType( clang_getArrayElementType( canonical ), session, depth + 1 );
                    return result;
                }
                if ( isFixedPointerKind( kind ) )
                    return makeIneligibleFixedType( result, "pointer or reference" );
                if ( kind != CXType_Record )
                    return makeIneligibleFixedType( result, "type '" + result->_typeName + "' is outside the fixed layout encoding table" );

                if ( isFixedStringType( result->_typeName, canonicalSpelling ) )
                {
                    result->_kind = FixedValueKind::String;
                    return result;
                }
                if ( canonicalSpelling == "sw::DynamicBitset" )
                {
                    result->_kind = FixedValueKind::Bitset;
                    return result;
                }
                for ( const FixedMathTypeDef& math : fixedlayout::kArrMathType )
                {
                    if ( result->_typeName == math._pName )
                    {
                        result->_kind = FixedValueKind::Math;
                        return result;
                    }
                }
                // 압축 타입은 인코딩 표 밖이다. 적격이 아닌 것으로 조용히 넘기지 않도록 표시를 달아 코덱 생성기가 오류로 멈추게 한다.
                if ( session._typeNameMap.isPacked( result->_typeName ) )
                {
                    result->_bPacked = SW_TRUE;
                    return makeIneligibleFixedType( result, "packed type '" + result->_typeName + "' has no fixed layout encoding" );
                }
                // `sw::vector` 는 자체 클래스라 정규 이름이 `sw::vector<…>` 입니다(`SW_ENABLE_STL_CONTAINER` 면 `std::vector` 별칭입니다).
                const bool bVector = StringUtil::startsWith( canonicalSpelling, "std::vector<" ) || StringUtil::startsWith( canonicalSpelling, "sw::vector<" );
                const bool bArray  = StringUtil::startsWith( canonicalSpelling, "std::array<" ) || StringUtil::startsWith( canonicalSpelling, "sw::array<" );
                if ( bVector || bArray )
                {
                    result->_kind    = bVector ? FixedValueKind::Vector : FixedValueKind::FixedArray;
                    result->_element = classifyFixedType( clang_Type_getTemplateArgumentAsType( canonical, 0 ), session, depth + 1 );
                    return result;
                }
                const CXCursor declaration = clang_getTypeDeclaration( canonical );
                if ( isReflectedRecord( declaration, session._config ) )
                {
                    result->_kind               = FixedValueKind::Reflect;
                    result->_fullyQualifiedName = makeFullyQualifiedName( declaration );
                    return result;
                }
                return makeIneligibleFixedType( result, "type '" + result->_typeName + "' is outside the fixed layout encoding table" );
            }

            // ------------------------------------------------------------------------------
            // D) REFLECT 타입의 멤버 — 베이스 · PROPERTY · FUNCTION · 생성자 · BODY 마커
            // ------------------------------------------------------------------------------
            /** @brief REFLECT 타입 하나의 멤버를 한 번의 자식 순회로 모으는 상태입니다. */
            struct MemberCollector
            {
                ParsedTypeInfo*               _pType;
                const ParserSession*          _pSession;
                vector<ParsedMethodSignature> _listMethodSignature; ///< FUNCTION 여부와 무관하게 이 타입이 선언한 메서드(애노테이션이 이름으로 가리킬 때 대조)
                uint32                        _baseCount;
                uint8                         _bSkipConstructors : 1; ///< Abstract / Static 타입은 생성할 수 없다(Unreal UCLASS(Abstract))
                uint8                         _bBodyFound        : 1;
                uint8                         _bHasError         : 1;
                uint8                         _bBusBody          : 1; ///< `kBusVersion` 멤버가 있다(서버 버스 본문 타입)
                uint8                         _bExported         : 1; ///< 레코드에 dllexport · dllimport · visibility 속성이 있다(모듈 내보내기 매크로)
                [[maybe_unused]] uint8        _reserved          : 3;

                MemberCollector( ParsedTypeInfo& type, const ParserSession& session )
                    : _pType{ &type }
                    , _pSession{ &session }
                    , _listMethodSignature{}
                    , _baseCount{ 0 }
                    , _bSkipConstructors{ SW_FALSE }
                    , _bBodyFound{ SW_FALSE }
                    , _bHasError{ SW_FALSE }
                    , _bBusBody{ SW_FALSE }
                    , _bExported{ SW_FALSE }
                    , _reserved{ 0 }
                {
                    if ( type._bAbstract == SW_TRUE || type._bStatic == SW_TRUE )
                        _bSkipConstructors = SW_TRUE;
                }
            };

            /**
             * @brief 베이스 지정자(`: public X`)가 가리키는 **클래스** 선언입니다. 별칭(`using` · `typedef`)은 풀어 냅니다.
             * @details 적힌 타입의 선언을 바로 물으면 `using Base = Component; struct X : Base` 의 답은 별칭 선언이다 — 그러면 부모 FQN 이
             *          별칭 이름(`sw::Base`)이 되어 실행 중에 부모를 못 찾고(상속 병합 · 캐스트가 사슬 중간에서 멈춘다), 컴포넌트 판별도 별칭에서
             *          멈춰 팩토리가 생기지 않는다. 정규 타입(canonical)은 별칭을 벗긴 레코드 타입이다.
             */
            static CXCursor getBaseClassDeclaration( const CXCursor baseSpecifier )
            {
                return clang_getTypeDeclaration( clang_getCanonicalType( clang_getCursorType( baseSpecifier ) ) );
            }

            /**
             * @brief 레코드 선언에 `REFLECT()` 가 붙었는지 봅니다. 다른 헤더의 선언도 봅니다.
             * @details clang_getTypeDeclaration 이 반환하는 커서는 AnnotateAttr 자식 순회에서 빠지는 경우가 있어 정의 커서를 보고,
             *          (다른 어노테이션 검사와 마찬가지로) 소스 텍스트 폴백까지 함께 검사합니다.
             */
            static bool isReflectedRecord( const CXCursor declaration, const ParserConfig& config )
            {
                if ( clang_Cursor_isNull( declaration ) != 0 )
                    return false;
                const CXCursor definition   = clang_getCursorDefinition( declaration );
                const CXCursor searchCursor = clang_Cursor_isNull( definition ) == 0 ? definition : declaration;
                return hasAnnotation( searchCursor, annotation::kReflect, config );
            }

            /**
             * @brief 베이스 클래스 FQN을 부모로 기록합니다. ParsedTypeInfo 는 부모 하나만 담습니다(단일 상속 체인).
             * @details PropertyInfo 의 오프셋 접근과 캐스트는 "리플렉션 부모는 항상 파생 객체의 byte offset 0 에 있다"는 전제로 동작합니다.
             *          비가상 베이스가 하나뿐이면 C++ ABI 가 offset 0 을 보장하지만, 두 번째 이후 베이스와 가상 베이스는 그렇지 않습니다.
             *          `castTo` 는 리플렉션이 파생이라고 답하고 C++ 상속 관계가 보이지 않으면 `reinterpret_cast` 로 내려가므로(`ReflectionCast.h`)
             *          그런 베이스를 거치면 포인터 보정 없이 잘못된 주소가 됩니다. 그래서 `REFLECT` 타입은 비가상 베이스를 하나까지만 받고,
             *          베이스가 `REFLECT()` 인지와 상관없이 둘째 베이스와 가상 베이스를 생성 오류로 막습니다. 인터페이스는 컴포넌트로 나눠 조합합니다.
             */
            static void collectBase( const CXCursor cursor, MemberCollector& collector )
            {
                const CXCursor baseDecl = getBaseClassDeclaration( cursor );
                const string   baseFQN  = ( clang_Cursor_isNull( baseDecl ) == 0 ) ? makeFullyQualifiedName( baseDecl ) : string{};

                ++collector._baseCount;
                collector._pType->_baseCount = collector._baseCount;
                const bool bVirtualBase      = clang_isVirtualBase( cursor ) != 0;
                if ( collector._baseCount == 1 && bVirtualBase == false )
                {
                    collector._pType->_parentFQN        = baseFQN;
                    collector._pType->_bParentReflected = isReflectedRecord( baseDecl, collector._pSession->_config ) ? SW_TRUE : SW_FALSE;
                    return;
                }

                CXFile file = nullptr;
                uint32 line = 0;
                clang_getExpansionLocation( clang_getCursorLocation( cursor ), &file, &line, nullptr, nullptr );
                const string path = file != nullptr ? takeString( clang_getFileName( file ) ) : string{};
                SW_LOG_ERROR( "ERROR: %#(%#): REFLECT type '%#' has %# base '%#'. A REFLECT type takes at most one non-virtual base: castTo "
                              "reinterpret_casts along the reflected parent chain, so any other base gets the wrong address. "
                              "Compose the extra behaviour as a component or member instead.",
                              path, line, collector._pType->_fullyQualifiedName, bVirtualBase ? "a virtual" : "a second", baseFQN );
                collector._bHasError = SW_TRUE;
            }

            // ------------------------------------------------------------------------------
            // 토큰 — 인자 이름 · 기본 인자처럼 AST 타입에 남지 않는 것을 소스 토큰에서 읽는다
            // ------------------------------------------------------------------------------
            /** @brief 커서 범위의 토큰 철자 · 종류입니다. */
            struct SourceToken
            {
                string      _spelling;
                CXTokenKind _kind;
            };

            /** @brief 위치를 매크로를 **쓴** 자리로 옮깁니다. 매크로 정의 안(명령줄 `-D` 버퍼)의 위치로는 소스 토큰을 읽을 수 없습니다. */
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
                    listToken.push_back( SourceToken{ takeString( clang_getTokenSpelling( translationUnit, pToken[tokenIndex] ) ),
                                                      clang_getTokenKind( pToken[tokenIndex] ) } );
                }
                clang_disposeTokens( translationUnit, pToken, tokenCount );
                return listToken;
            }

            /** @brief 커서 선언의 소스 토큰에 식별자 @p name 이 그대로 적혀 있는지 봅니다(매크로가 만든 이름이면 false). */
            [[nodiscard]] static bool isSpelledInSource( const CXCursor cursor, const string& name )
            {
                for ( const SourceToken& token : tokenizeCursor( cursor ) )
                {
                    if ( token._kind == CXToken_Identifier && token._spelling == name )
                        return true;
                }
                return false;
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

            /**
             * @brief 인자 선언 하나의 기본 인자(`= 식`)를 C++ 글 그대로 읽습니다. 없으면 빈 문자열입니다.
             * @details 기본 인자는 타입에 남지 않아 AST 의 인자 커서 범위를 토큰으로 읽는다. 괄호 밖의 첫 `=` 뒤가 식이다.
             */
            static string readDefaultArgument( const CXCursor parameterCursor )
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

            /**
             * @brief `MulticastDelegate<void( int32 amount, TagID tag )>` 의 인자 이름들을 소스 토큰에서 읽습니다.
             * @details 함수 타입의 인자 이름은 타입에 남지 않는다. 필드 선언에 시그니처가 없으면(별칭 `using DamagedEvent = …` 으로 적었으면)
             *          그 별칭 선언을 읽는다. 못 찾으면 빈 목록 — 이름은 표시용이라 없어도 묶기 · 호출은 된다.
             */
            static vector<string> readEventParameterNames( const CXCursor fieldCursor )
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

            /** @brief 인자 이름 · 정규 타입 · 기본 인자를 채웁니다. */
            static void fillParameters( const CXCursor cursor, ParsedFunctionInfo& method, const ParserSession& session )
            {
                const int32 numArgs = clang_Cursor_getNumArguments( cursor );
                for ( int32 argIndex = 0; argIndex < numArgs; ++argIndex )
                {
                    const CXCursor      argCursor = clang_Cursor_getArgument( cursor, static_cast<uint32>( argIndex ) );
                    ParsedParameterInfo parameter;
                    parameter._name         = getCursorSpelling( argCursor );
                    parameter._typeName     = session._typeNameMap.normalize( takeString( clang_getTypeSpelling( clang_getCursorType( argCursor ) ) ) );
                    parameter._defaultValue = readDefaultArgument( argCursor );
                    // 타입 핸들 인자는 썽크가 지역 변수로 만들 정규 철자를 함께 적는다(`resolveComponentHandleParameters`). const · 참조는 뗀다.
                    string canonical = takeString( clang_getTypeSpelling( clang_getCanonicalType( clang_getNonReferenceType( clang_getCursorType( argCursor ) ) ) ) );
                    if ( StringUtil::startsWith( canonical, "const " ) )
                        canonical.erase( 0, 6 );
                    parameter._canonicalTypeName = std::move( canonical );
                    method._listParameter.push_back( std::move( parameter ) );
                }
            }

            /** @brief 필드 타입이 리플렉션 이벤트(멀티캐스트 델리게이트)인지 봅니다. */
            static bool isEventType( const CXType fieldType )
            {
                const string canonical = takeString( clang_getTypeSpelling( clang_getCanonicalType( fieldType ) ) );
                return StringUtil::startsWith( canonical, annotation::kEventTemplatePrefix );
            }

            /**
             * @brief 이벤트 필드 타입의 함수 타입(`void( int32, const string& )`)을 **적힌 꼴로** 찾습니다.
             * @details 정규 타입에서 꺼내면 인자 타입이 펼쳐진 철자(`sw::basic_string<char>`)라 정규 이름(`string`)이 되지 않는다. 별칭
             *          (`using ScoreEvent = …`)은 벗겨 적힌 템플릿 인자를 읽고, 그래도 못 찾으면 정규 타입으로 물러선다.
             */
            static CXType findEventFunctionType( const CXType fieldType )
            {
                CXType current = fieldType;
                for ( int32 depth = 0; depth < 8; ++depth )
                {
                    const CXType named = ( current.kind == CXType_Elaborated ) ? clang_Type_getNamedType( current ) : current;
                    if ( named.kind == CXType_Typedef )
                    {
                        current = clang_getTypedefDeclUnderlyingType( clang_getTypeDeclaration( named ) );
                        continue;
                    }
                    const CXType written = clang_Type_getTemplateArgumentAsType( named, 0 );
                    if ( written.kind == CXType_FunctionProto )
                        return written;
                    break;
                }
                return clang_Type_getTemplateArgumentAsType( clang_getCanonicalType( fieldType ), 0 );
            }

            /** @brief `PROPERTY()` 가 붙은 멀티캐스트 델리게이트 필드를 이벤트로 수집합니다. */
            static void collectEvent( const CXCursor cursor, const string& spelling, const CXType fieldType, MemberCollector& collector )
            {
                const ParserSession& session = *collector._pSession;
                ParsedEventInfo      event;
                event._memberName             = getCursorSpelling( cursor );
                event._annotation._memberName = event._memberName;
                event._annotation._name       = event._memberName;
                event._annotation._typeName   = session._typeNameMap.normalize( takeString( clang_getTypeSpelling( fieldType ) ) );
                const string owner            = makeMemberOwnerName( collector._pType->_fullyQualifiedName, event._memberName );
                if ( applyAnnotation( spelling, event._annotation, session, owner ) == false )
                {
                    collector._bHasError = SW_TRUE;
                    return;
                }
                for ( const string& token : AnnotationApply::splitAnnotationArgs( AnnotationApply::annotationArgumentText( spelling, annotation::kPropertyPrefix ) ) )
                {
                    const size_t             eqPos    = token.find( '=' );
                    const AnnotationBinding* pBinding = ( eqPos == string::npos )
                                                          ? session._annotationMeta.findBare( annotation::kPropertyScope, token )
                                                          : session._annotationMeta.findKey( annotation::kPropertyScope, StringUtil::trim( string_view( token ).substr( 0, eqPos ) ) );
                    if ( pBinding != nullptr && AnnotationFields::acceptsOnEvent( pBinding->_field ) == false )
                    {
                        SW_LOG_ERROR( "ERROR: PROPERTY() on event '%#' has '%#'. An event (multicast delegate) takes display metadata only (%#).", owner, token,
                                      AnnotationFields::makeEventFieldList() );
                        collector._bHasError = SW_TRUE;
                        return;
                    }
                }

                // 함수 타입은 MulticastDelegate 의 첫 템플릿 인자다. 반환은 void 만 받는다 — broadcast 는 반환값을 모으지 않는다.
                const CXType functionType = findEventFunctionType( fieldType );
                const CXType resultType   = clang_getResultType( functionType );
                if ( functionType.kind != CXType_FunctionProto || resultType.kind != CXType_Void )
                {
                    SW_LOG_ERROR( "ERROR: event '%#' must be MulticastDelegate<void( ... )> - broadcast does not collect return values.", owner );
                    collector._bHasError = SW_TRUE;
                    return;
                }
                const vector<string> listName = readEventParameterNames( cursor );
                const int32          argCount = clang_getNumArgTypes( functionType );
                for ( int32 argIndex = 0; argIndex < argCount; ++argIndex )
                {
                    ParsedParameterInfo parameter;
                    parameter._typeName = session._typeNameMap.normalize( takeString( clang_getTypeSpelling( clang_getArgType( functionType, static_cast<uint32>( argIndex ) ) ) ) );
                    if ( static_cast<size_t>( argIndex ) < listName.size() )
                        parameter._name = listName[static_cast<size_t>( argIndex )];
                    event._listParameter.push_back( std::move( parameter ) );
                }
                collector._pType->_listEvent.push_back( std::move( event ) );
            }

            /** @brief @p spelling 에 식별자 `ComponentHandle` 이나 `TComponentHandle` 이 낱말로 들어 있으면 true 입니다(`vector<sw::ComponentHandle>` 같은 템플릿 인자 포함). */
            static bool spellsComponentHandle( const string& spelling )
            {
                const auto isIdentifierChar = []( const utf8 c )
                {
                    return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_';
                };
                const string kToken = "ComponentHandle";
                for ( size_t pos = spelling.find( kToken ); pos != string::npos; pos = spelling.find( kToken, pos + 1 ) )
                {
                    size_t begin = pos;
                    if ( begin > 0 && spelling[begin - 1] == 'T' )
                        --begin; // TComponentHandle
                    const size_t end         = pos + kToken.size();
                    const bool   bWordBefore = begin == 0 || isIdentifierChar( spelling[begin - 1] ) == false;
                    const bool   bWordAfter  = end >= spelling.size() || isIdentifierChar( spelling[end] ) == false;
                    if ( bWordBefore && bWordAfter )
                        return true;
                }
                return false;
            }

            /**
             * @brief 정규화한 표기 @p name 이 타입 핸들(`TComponentHandle<대상>`)이면 true 이고, 대상 타입 이름을 @p outTarget 에 넣습니다.
             */
            [[nodiscard]] static bool tryReadTypedHandleTarget( const string& name, string& outTarget )
            {
                constexpr string_view kPrefix = "TComponentHandle<";
                string_view           view    = StringUtil::trim( name );
                if ( StringUtil::startsWith( view, kPrefix ) == false || view.back() != '>' )
                    return false;
                view.remove_prefix( kPrefix.size() );
                view.remove_suffix( 1 );
                outTarget = string{ StringUtil::trim( view ) };
                return outTarget.empty() == false;
            }

            /** @brief @p text 안의 @p from 을 모두 @p to 로 바꿉니다. */
            static void replaceAllText( string& text, const string& from, const string& to )
            {
                for ( size_t pos = text.find( from ); pos != string::npos; pos = text.find( from, pos + to.size() ) )
                {
                    text.replace( pos, from.size(), to );
                }
            }

            /**
             * @brief PROPERTY 에 컴포넌트 핸들이 있으면 저장할 수 있는 모양인지 확인하고 내장 `ComponentHandle` 로 정규화합니다. 아니면 오류를 남기고 false 입니다.
             * @details 저장할 수 있는 모양은 **타입 핸들 `TComponentHandle<T>` 한 값**, 또는 **그 시퀀스(한 겹, `vector` 등)** 입니다. 정규화하면 값 · 원소의 타입 이름은
             *          내장 `ComponentHandle` 이 되어 직렬화기 · 인스펙터가 기존 처리기를 그대로 쓰고, 대상 `T` 는 `_handleTargetTypeName` 에 실려
             *          읽은 뒤의 연결이 타입을 확인한다. 맵 · 중첩 컨테이너 · 타입 없는 `ComponentHandle` 은 대상 타입을 확인할 자리가 없어 생성 오류다
             *          (`docs/09_Decisions.md` 5-6 타입 핸들 직렬화). 리플렉션 구조체의 필드도 이 검사를 지난다.
             */
            static bool resolveComponentHandleProperty( ParsedPropertyInfo& prop, const string& owner )
            {
                bool bHolds = spellsComponentHandle( prop._typeName ) || spellsComponentHandle( prop._elementTypeName ) || spellsComponentHandle( prop._keyTypeName );
                for ( const ParsedContainerNode* pNode = prop._containerTree.get(); pNode != nullptr && bHolds == false; pNode = pNode->_elementNested.get() )
                {
                    bHolds = spellsComponentHandle( pNode->_elementTypeName ) || spellsComponentHandle( pNode->_keyTypeName );
                }
                if ( bHolds == false )
                    return true;

                string               target;
                ParsedContainerNode* pTree         = prop._containerTree.get();
                const bool           bSingleValue  = prop._bIsContainer == SW_FALSE && tryReadTypedHandleTarget( prop._typeName, target );
                const bool           bFlatSequence = prop._bIsContainer == SW_TRUE && prop._containerKind == ContainerKind::Sequence && prop._keyTypeName.empty() &&
                                           ( pTree == nullptr || pTree->_elementNested == nullptr || pTree->_elementNested->_bIsContainer == SW_FALSE );
                // 집합(`set` · `unordered_set`)은 원소의 순서 · 해시가 있어야 컴파일되는데 핸들에는 둘 다 없다 — 생성 단계에서 막아 C++ 오류 대신 이 오류가 뜨게 한다.
                const bool bOrderedOrHashed   = prop._containerType == "Set" || prop._containerType == "UnorderedSet";
                const bool bSequenceOfHandles = bFlatSequence && bOrderedOrHashed == false && tryReadTypedHandleTarget( prop._elementTypeName, target );
                if ( bSingleValue == false && bSequenceOfHandles == false )
                {
                    SW_LOG_ERROR( "ERROR: PROPERTY() '%#' holds a component handle (%#) in a shape that cannot be saved. Use one TComponentHandle<T>, or a vector of "
                                  "TComponentHandle<T>. An untyped ComponentHandle, a map, a set and nested containers have no place to check the target type "
                                  "(see docs/09_Decisions.md 5-6, typed handle serialization).",
                                  owner, prop._typeName );
                    return false;
                }

                const string kCanonical = "ComponentHandle";
                if ( bSingleValue )
                {
                    prop._typeName = kCanonical;
                }
                else
                {
                    const string typedSpelling = prop._elementTypeName;
                    replaceAllText( prop._typeName, typedSpelling, kCanonical );
                    prop._elementTypeName = kCanonical;
                    if ( pTree != nullptr )
                        pTree->_elementTypeName = kCanonical;
                }
                prop._handleTargetTypeName = target;
                return true;
            }

            /**
             * @brief FUNCTION 인자에 타입 핸들(`TComponentHandle<T>`)이 있으면 내장 `ComponentHandle` 로 정규화하고 대상 `T` 를 싣습니다. 쓸 수 없는 모양이면 오류를 남기고 false 입니다.
             * @details 호출 쪽(인스펙터의 선택기 · 콘솔)은 타입 없는 핸들을 넘기고, 생성한 호출 썽크가 `ComponentHandleArgument::cast` 로 대상 타입을 확인해 바꾼다.
             *          확인에는 그 씬의 매니저가 들어서 **컴포넌트의 비정적 메서드**만 받는다(생성자 · 정적 함수 · 컴포넌트가 아닌 타입은 생성 오류다).
             *          컨테이너에 든 타입 핸들은 대상 타입을 확인할 자리가 없어 오류다. 타입 없는 `ComponentHandle` 은 예전처럼 그대로 둔다.
             */
            static bool resolveComponentHandleParameters( ParsedFunctionInfo& method, bool bComponentOwner, const string& owner )
            {
                for ( ParsedParameterInfo& parameter : method._listParameter )
                {
                    if ( parameter._typeName.find( "TComponentHandle" ) == string::npos )
                        continue;
                    string     target;
                    const bool bTyped = tryReadTypedHandleTarget( parameter._typeName, target );
                    if ( bTyped == false )
                    {
                        SW_LOG_ERROR( "ERROR: FUNCTION '%#' argument '%#' holds component handles in a container (%#). Pass one TComponentHandle<T> per argument.", owner,
                                      parameter._name, parameter._typeName );
                        return false;
                    }
                    const bool bMemberOfComponent = bComponentOwner && method._bStatic == SW_FALSE && method._bConstructor == SW_FALSE;
                    if ( bMemberOfComponent == false )
                    {
                        SW_LOG_ERROR( "ERROR: FUNCTION '%#' argument '%#' is a TComponentHandle<%#>. Typed handle arguments are checked against the scene of the "
                                      "called component, so only non-static member functions of a Component may take them.",
                                      owner, parameter._name, target );
                        return false;
                    }
                    parameter._handleTargetTypeName = target;
                    parameter._typeName             = "ComponentHandle";
                }
                return true;
            }

            /** @brief 한 범위의 자식에서 이름 조각(`A::B::kX`)을 차례로 따라가 변수 선언을 찾는 상태입니다(`constantSearchVisitor`). */
            struct ConstantSearch
            {
                const vector<string>* _pListPart;
                size_t                _partIndex;
                CXCursor              _found;
                bool                  _bFound;
            };

            static CXChildVisitResult constantSearchVisitor( CXCursor cursor, CXCursor, CXClientData data )
            {
                ConstantSearch*       pSearch  = static_cast<ConstantSearch*>( data );
                const vector<string>& listPart = *pSearch->_pListPart;
                if ( isStringEqual( clang_getCursorSpelling( cursor ), listPart[pSearch->_partIndex] ) == false )
                    return CXChildVisit_Continue;
                const CXCursorKind kind = clang_getCursorKind( cursor );
                if ( pSearch->_partIndex + 1 == listPart.size() )
                {
                    if ( kind != CXCursor_VarDecl )
                        return CXChildVisit_Continue;
                    pSearch->_found  = cursor;
                    pSearch->_bFound = true;
                    return CXChildVisit_Break;
                }
                const bool bScope = kind == CXCursor_Namespace || kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl;
                if ( bScope == false )
                    return CXChildVisit_Continue;
                // 타입은 정의 안을 본다(앞선 선언에는 자식이 없다). 네임스페이스는 블록마다 따로 열리므로 이 블록을 보고, 다른 블록은 바깥 순회가 만난다.
                const CXCursor definition = clang_getCursorDefinition( cursor );
                const CXCursor scope      = ( kind == CXCursor_Namespace || clang_Cursor_isNull( definition ) != 0 ) ? cursor : definition;
                ConstantSearch inner{ pSearch->_pListPart, pSearch->_partIndex + 1, clang_getNullCursor(), false };
                clang_visitChildren( scope, constantSearchVisitor, &inner );
                if ( inner._bFound == false )
                    return CXChildVisit_Continue;
                pSearch->_found  = inner._found;
                pSearch->_bFound = true;
                return CXChildVisit_Break;
            }

            /** @brief @p scope 의 자식에서 @p listPart 를 따라간 변수 선언을 찾습니다. 없으면 null 커서입니다. */
            static CXCursor findConstantIn( const CXCursor scope, const vector<string>& listPart )
            {
                ConstantSearch search{ &listPart, 0, clang_getNullCursor(), false };
                clang_visitChildren( scope, constantSearchVisitor, &search );
                return search._found;
            }

            /**
             * @brief 상수 이름(`kMaxSlotCount`, `LedgerConstant::kMaxMemoSize`)이 가리키는 변수 선언을 찾습니다. 없으면 null 커서입니다.
             * @details `MaxCount` 와 `Min` · `Max` 가 같은 규칙으로 씁니다. 이름 찾기는 C++ 를 좁게 따릅니다. 필드가 든 타입과 그 바깥 타입을 안쪽부터 보고,
             *          그다음 바깥 네임스페이스 경로를 안쪽부터 붙여 번역 단위 맨 위에서 찾습니다(네임스페이스는 여러 블록에 걸쳐 열리므로 맨 위에서 모든 블록을 봅니다).
             *          기반 클래스 · using 은 보지 않습니다.
             */
            static CXCursor findNamedConstant( const CXCursor memberCursor, const string_view name )
            {
                vector<string> listPart;
                string_view    rest = name;
                for ( size_t separator = rest.find( "::" ); separator != string_view::npos; separator = rest.find( "::" ) )
                {
                    listPart.emplace_back( rest.substr( 0, separator ) );
                    rest = rest.substr( separator + 2 );
                }
                listPart.emplace_back( rest );

                // 바깥 범위 사슬: 타입은 안쪽부터, 네임스페이스 이름은 바깥부터 모은다.
                vector<CXCursor> listRecord;
                vector<string>   listNamespace;
                CXCursor         scope = clang_getCursorSemanticParent( memberCursor );
                while ( clang_Cursor_isNull( scope ) == 0 && clang_getCursorKind( scope ) != CXCursor_TranslationUnit )
                {
                    if ( clang_getCursorKind( scope ) == CXCursor_Namespace )
                        listNamespace.insert( listNamespace.begin(), getCursorSpelling( scope ) );
                    else
                        listRecord.push_back( scope );
                    scope = clang_getCursorSemanticParent( scope );
                }

                CXCursor found = clang_getNullCursor();
                for ( const CXCursor record : listRecord )
                {
                    found = findConstantIn( record, listPart );
                    if ( clang_Cursor_isNull( found ) == 0 )
                        break;
                }
                const CXCursor translationUnit = clang_getTranslationUnitCursor( clang_Cursor_getTranslationUnit( memberCursor ) );
                for ( size_t depth = listNamespace.size() + 1; clang_Cursor_isNull( found ) != 0 && depth > 0; --depth )
                {
                    vector<string> listPath( listNamespace.begin(), listNamespace.begin() + static_cast<ptrdiff_t>( depth - 1 ) );
                    listPath.insert( listPath.end(), listPart.begin(), listPart.end() );
                    found = findConstantIn( translationUnit, listPath );
                }
                return found;
            }

            /**
             * @brief `MaxCount = 상수 이름` 의 상수를 선언에서 찾아 값을 `_maxCount` 에 넣습니다. 숫자로 적었으면(`_maxCountName` 이 비었으면) 할 일이 없습니다.
             * @details 상한의 기준을 C++ 상수 하나로 둡니다. 생성기가 그 값을 읽으므로 코덱 · 정규 문자열(스키마 해시) · `PropertyMetadata::_maxCount` 가
             *          모두 같은 수를 봅니다. 상수를 바꾸면 그 헤더를 읽는 생성물이 다시 만들어집니다(depfile). 이름 찾기는 `findNamedConstant` 입니다.
             *          찾지 못했거나 정수 상수가 아니거나 1 이상의 32 비트 값이 아니면 생성 오류입니다 — 상한을 조용히 빼면 그 필드는 코덱 없이 넘어갑니다.
             */
            [[nodiscard]] static bool resolveMaxCountName( const CXCursor memberCursor, ParsedPropertyInfo& prop, const string_view owner )
            {
                if ( prop._maxCountName.empty() )
                    return true;
                const CXCursor found = findNamedConstant( memberCursor, prop._maxCountName );
                if ( clang_Cursor_isNull( found ) != 0 )
                {
                    SW_LOG_ERROR( "ERROR: PROPERTY() '%#': MaxCount constant '%#' is not declared in this type or an enclosing scope", owner,
                                  prop._maxCountName.c_str() );
                    return false;
                }

                constexpr int64    kMaxCountLimit = static_cast<int64>( std::numeric_limits<uint32>::max() );
                int64              value          = 0;
                bool               bInteger       = false;
                const CXEvalResult result         = clang_Cursor_Evaluate( found );
                if ( result != nullptr )
                {
                    bInteger = clang_EvalResult_getKind( result ) == CXEval_Int;
                    if ( bInteger && clang_EvalResult_isUnsignedInt( result ) != 0 )
                    {
                        const uint64 unsignedValue = static_cast<uint64>( clang_EvalResult_getAsUnsigned( result ) );
                        value                      = unsignedValue > static_cast<uint64>( kMaxCountLimit ) ? kMaxCountLimit + 1 : static_cast<int64>( unsignedValue );
                    }
                    else if ( bInteger )
                    {
                        value = static_cast<int64>( clang_EvalResult_getAsLongLong( result ) );
                    }
                    clang_EvalResult_dispose( result );
                }
                const bool bWithinLimit = 1 <= value && value <= kMaxCountLimit;
                if ( bInteger == false || bWithinLimit == false )
                {
                    SW_LOG_ERROR( "ERROR: PROPERTY() '%#': MaxCount constant '%#' must be an integer constant between 1 and 4294967295", owner,
                                  prop._maxCountName.c_str() );
                    return false;
                }
                prop._maxCount     = static_cast<uint32>( value );
                prop._bHasMaxCount = SW_TRUE;
                return true;
            }

            /**
             * @brief `Min` · `Max` 글의 상수 이름 항(`kMaxLevel`, `-LedgerConstant::kMaxX`)을 상수의 값으로 바꾸고 숫자 경계를 채웁니다.
             * @details 필드 이름 항은 `_` 로 시작하므로(필드 이름 규칙) 그 밖의 이름이 상수입니다. 상수를 바꾸면 코덱 · 스키마 해시 · 인스펙터 범위가 함께 바뀝니다.
             *          정수나 실수 상수만 받고, 못 찾거나 숫자가 아니면 생성 오류입니다 — 경계를 조용히 빼면 범위 검사가 사라집니다.
             */
            [[nodiscard]] static bool resolveRangeConstants( const CXCursor memberCursor, ParsedPropertyInfo& prop, const string_view owner )
            {
                struct Side
                {
                    ParsedRangeExpr* _pExpr;
                    bool             _bMax;
                    string_view      _label;
                };
                const Side listSide[] = {
                    {&prop._minRangeExpr, false, "Min"},
                    {&prop._maxRangeExpr,  true, "Max"}
                };
                for ( const Side& side : listSide )
                {
                    string      resolved;
                    bool        bChanged = false;
                    string_view rest     = side._pExpr->_text;
                    for ( size_t comma = 0; comma != string_view::npos; )
                    {
                        comma                       = rest.find( ',' );
                        const string_view term      = StringUtil::trim( rest.substr( 0, comma ) );
                        rest                        = comma == string_view::npos ? string_view() : rest.substr( comma + 1 );
                        const bool        bNegative = term.empty() == false && term.front() == '-';
                        const string_view name      = bNegative ? StringUtil::trim( term.substr( 1 ) ) : term;
                        float32           number{ 0.0f };
                        const bool        bConstant = name.empty() == false && name.front() != '_' && StringUtil::parseFloat( term, number ) == false;
                        if ( resolved.empty() == false )
                            resolved += ", ";
                        if ( bConstant == false )
                        {
                            resolved += term;
                            continue;
                        }
                        const CXCursor found = findNamedConstant( memberCursor, name );
                        if ( clang_Cursor_isNull( found ) != 0 )
                        {
                            SW_LOG_ERROR( "ERROR: PROPERTY() '%#': %# constant '%#' is not declared in this type or an enclosing scope", owner, side._label, name );
                            return false;
                        }
                        float64            value  = 0.0;
                        bool               bValid = false;
                        const CXEvalResult result = clang_Cursor_Evaluate( found );
                        if ( result != nullptr )
                        {
                            const CXEvalResultKind kind = clang_EvalResult_getKind( result );
                            if ( kind == CXEval_Float )
                            {
                                value  = clang_EvalResult_getAsDouble( result );
                                bValid = true;
                            }
                            else if ( kind == CXEval_Int )
                            {
                                value  = clang_EvalResult_isUnsignedInt( result ) != 0 ? static_cast<float64>( clang_EvalResult_getAsUnsigned( result ) )
                                                                                       : static_cast<float64>( clang_EvalResult_getAsLongLong( result ) );
                                bValid = true;
                            }
                            clang_EvalResult_dispose( result );
                        }
                        if ( bValid == false )
                        {
                            SW_LOG_ERROR( "ERROR: PROPERTY() '%#': %# constant '%#' must be an integer or floating-point constant", owner, side._label, name );
                            return false;
                        }
                        utf8       buffer[40]{};
                        const auto converted = std::to_chars( buffer, buffer + sizeof( buffer ), bNegative ? -value : value );
                        resolved.append( buffer, converted.ptr );
                        bChanged = true;
                    }
                    if ( bChanged == false )
                        continue;
                    side._pExpr->_text = resolved;
                    float32 parsed{ 0.0f };
                    if ( StringUtil::parseFloat( resolved, parsed ) == false )
                        continue;
                    if ( side._bMax )
                    {
                        prop._maxRange     = parsed;
                        prop._bHasMaxRange = SW_TRUE;
                    }
                    else
                    {
                        prop._minRange     = parsed;
                        prop._bHasMinRange = SW_TRUE;
                    }
                }
                return true;
            }

            /** @brief 애노테이션이 상수 이름으로 적은 값(`MaxCount` · `Min` · `Max`)을 선언에서 찾아 값으로 풉니다. */
            [[nodiscard]] static bool resolvePropertyConstants( const CXCursor memberCursor, ParsedPropertyInfo& prop, const string_view owner )
            {
                return resolveMaxCountName( memberCursor, prop, owner ) && resolveRangeConstants( memberCursor, prop, owner );
            }

            /** @brief 필드 선언에서 PROPERTY 메타를 수집합니다. 멀티캐스트 델리게이트면 이벤트로 수집합니다. */
            static void collectField( const CXCursor cursor, MemberCollector& collector )
            {
                const string spelling = findAnnotateAttr( cursor, annotation::kPropertyPrefix );
                if ( spelling.empty() && hasSourceAnnotation( cursor, annotation::kProperty, collector._pSession->_config ) == false )
                    return;

                if ( isEventType( clang_getCursorType( cursor ) ) )
                {
                    const string eventSpelling = spelling.empty() ? extractSourceAnnotation( cursor, annotation::kProperty, collector._pSession->_config ) : spelling;
                    collectEvent( cursor, eventSpelling, clang_getCursorType( cursor ), collector );
                    return;
                }

                const ParserSession& session   = *collector._pSession;
                const CXType         fieldType = resolvePropertyAlias( clang_getCursorType( cursor ), session ); // 사용자 별칭은 벗긴다
                ParsedPropertyInfo   prop;
                prop._memberName = getCursorSpelling( cursor );
                prop._name       = prop._memberName;
                // 칸 이름이 매크로로 바뀌면(Windows CRT 의 `_stat` → `_stat64i32`) 리플렉션 이름 · 데이터 속성 이름이 소스와 조용히 어긋난다.
                // 선언의 소스 토큰에 그 이름이 없으면 매크로가 만든 이름이다 — 칸 이름을 바꾸게 멈춘다.
                if ( isSpelledInSource( cursor, prop._memberName ) == false )
                {
                    SW_LOG_ERROR( "ERROR: PROPERTY() '%#' in '%#' is renamed by a macro (the source spells another name) - rename the field",
                                  prop._memberName.c_str(), makeFullyQualifiedName( clang_getCursorSemanticParent( cursor ) ).c_str() );
                    collector._bHasError = SW_TRUE;
                    return;
                }
                // 템플릿 인자 안의 별칭까지 벗긴 이름이다. 별칭이 없으면 `fieldType` 의 표기 그대로다.
                string resolvedSpelling;
                (void)tryResolveAliasSpelling( clang_getCursorType( cursor ), session, resolvedSpelling ); // 벗겼든 아니든 표기를 쓴다
                prop._typeName = session._typeNameMap.normalize( resolvedSpelling );
                if ( clang_Cursor_isBitField( cursor ) != 0 )
                {
                    const int32 bitWidth = clang_getFieldDeclBitWidth( cursor );
                    if ( bitWidth != 1 )
                    {
                        SW_LOG_ERROR( "ERROR: PROPERTY() bitfield '%#' in '%#' has bit width %#. Only 1-bit bitfield boolean flags (e.g. uint8 _flag : 1;) are supported in reflection!",
                                      prop._name.c_str(), makeFullyQualifiedName( clang_getCursorSemanticParent( cursor ) ).c_str(), bitWidth );
                        collector._bHasError = SW_TRUE;
                        return;
                    }

                    // 자리는 측정하지 않는다 — 런타임이 그 구성의 레이아웃에서 찾는다(`PropertyInfo::resolveBitField`).
                    prop._bIsBitField = SW_TRUE;
                }

                const string owner = makeMemberOwnerName( collector._pType->_fullyQualifiedName, prop._memberName );
                if ( applyAnnotation( spelling, prop, session, owner ) == false || resolvePropertyConstants( cursor, prop, owner ) == false )
                {
                    collector._bHasError = SW_TRUE;
                    return;
                }
                fillContainerDetails( prop, fieldType, session );
                if ( resolveComponentHandleProperty( prop, owner ) == false )
                {
                    collector._bHasError = SW_TRUE;
                    return;
                }
                if ( isInterpAllowed( prop, owner ) == false || applyDisplayMeta( prop, fieldType, owner ) == false )
                {
                    collector._bHasError = SW_TRUE;
                    return;
                }
                fillFixedLayoutFacts( cursor, fieldType, prop, session );
                collector._pType->_listProperty.push_back( std::move( prop ) );
            }

            /**
             * @brief 고정 배치형 코덱이 쓰는 선언 사실(줄 · 공개 여부 · 인코딩 분류)을 채웁니다.
             * @details 비트필드는 1 비트 플래그만 받으므로(위에서 막았습니다) 정수 · bool 비트필드는 bool 로 씁니다.
             */
            static void fillFixedLayoutFacts( const CXCursor cursor, const CXType fieldType, ParsedPropertyInfo& prop, const ParserSession& session )
            {
                uint32 line = 0;
                clang_getExpansionLocation( clang_getCursorLocation( cursor ), nullptr, &line, nullptr, nullptr );
                prop._line      = line;
                prop._bPublic   = clang_getCXXAccessSpecifier( cursor ) == CX_CXXPublic ? SW_TRUE : SW_FALSE;
                prop._fixedType = classifyFixedType( fieldType, session, 0 );
                if ( prop._bIsBitField == SW_FALSE )
                    return;
                const FixedValueKind kind = prop._fixedType->_kind;
                if ( kind == FixedValueKind::Bool || kind == FixedValueKind::Integer )
                    prop._fixedType->_kind = FixedValueKind::Bool;
                else
                    prop._fixedType = makeIneligibleFixedType( prop._fixedType, "bit field of a non-integer type" );
            }

            /**
             * @brief 값 참조를 돌려주는 메서드에 붙은 PROPERTY 를 "값이 객체 밖에 있는 프로퍼티" 로 수집합니다.
             * @details 값은 객체 안의 필드가 아니라 메서드가 돌려주는 자리에 있습니다(씬 컴포넌트의 로컬 TRS 는 트랜스폼 저장소의 칸).
             *          코드젠은 오프셋 대신 그 메서드를 부르는 접근자를 `PropertyInfo::_pValueAccessor` 에 넣습니다. 그래서 직렬화기 ·
             *          인스펙터 · 비교 도구는 바뀐 것 없이 그 자리를 읽고 씁니다.
             *
             *          모양을 여기서 막습니다. 인자 없는 비정적 메서드여야 하고, 돌려주는 것은 const 가 아닌 lvalue 참조여야 합니다 —
             *          값으로 돌려주면 쓸 자리가 없습니다. 비트필드 · 컨테이너는 받지 않습니다(컨테이너 래퍼 코드젠이 필드 이름을 씁니다).
             */
            static void collectAccessorProperty( const CXCursor cursor, const string& spelling, MemberCollector& collector )
            {
                const ParserSession& session = *collector._pSession;
                ParsedPropertyInfo   prop;
                prop._memberName   = getCursorSpelling( cursor );
                prop._name         = prop._memberName;
                prop._bIsAccessor  = SW_TRUE;
                const string owner = makeMemberOwnerName( collector._pType->_fullyQualifiedName, prop._memberName );

                const CXType resultType = clang_getCursorResultType( cursor );
                const CXType valueType  = clang_getPointeeType( resultType );
                const bool   bShapeOk   = clang_CXXMethod_isStatic( cursor ) == 0 && clang_Cursor_getNumArguments( cursor ) == 0 &&
                                      resultType.kind == CXType_LValueReference && clang_isConstQualifiedType( valueType ) == 0;
                if ( bShapeOk == false )
                {
                    SW_LOG_ERROR( "ERROR: PROPERTY() on method '%#' needs a non-static method with no parameters that returns a non-const "
                                  "lvalue reference (T&) to the value. Put PROPERTY() on a field instead if the value lives in the object.",
                                  owner );
                    collector._bHasError = SW_TRUE;
                    return;
                }
                // 위에서 const 가 아님을 확인했으므로 철자에 한정자가 붙지 않는다. `clang_getUnqualifiedType` 은 libclang 16 부터라 리눅스 CI 의
                // libclang 에는 없다 — 쓰지 말 것.
                prop._typeName = session._typeNameMap.normalize( takeString( clang_getTypeSpelling( valueType ) ) );

                if ( applyAnnotation( spelling, prop, session, owner ) == false || resolvePropertyConstants( cursor, prop, owner ) == false )
                {
                    collector._bHasError = SW_TRUE;
                    return;
                }
                fillContainerDetails( prop, valueType, session );
                if ( prop._bIsContainer == SW_TRUE )
                {
                    SW_LOG_ERROR( "ERROR: PROPERTY() on method '%#' returns a container. Accessor properties support single values only.", owner );
                    collector._bHasError = SW_TRUE;
                    return;
                }
                if ( resolveComponentHandleProperty( prop, owner ) == false )
                {
                    collector._bHasError = SW_TRUE;
                    return;
                }
                if ( isInterpAllowed( prop, owner ) == false || applyDisplayMeta( prop, valueType, owner ) == false )
                {
                    collector._bHasError = SW_TRUE;
                    return;
                }
                // 값이 객체 밖에 있어 고정 배치형 코덱이 필드처럼 읽고 쓸 수 없습니다.
                prop._fixedType = makeIneligibleFixedType( make_shared<ParsedFixedType>(), "accessor property (the value lives outside the object)" );
                collector._pType->_listProperty.push_back( std::move( prop ) );
            }

            /** @brief 메서드 · 생성자에 붙은 애노테이션 셋을 한 번의 자식 순회로 봅니다. */
            struct MethodAnnotation
            {
                string                 _functionSpelling; ///< FUNCTION(...) 철자. 없으면 비어 있다
                string                 _propertySpelling; ///< PROPERTY(...) 철자. 값 참조를 돌려주는 메서드면 접근자 프로퍼티다
                uint8                  _bBody    : 1;     ///< REFLECT_BODY
                [[maybe_unused]] uint8 _reserved : 7;

                MethodAnnotation()
                    : _functionSpelling{}
                    , _propertySpelling{}
                    , _bBody{ SW_FALSE }
                    , _reserved{ 0 }
                {
                }
            };

            static CXChildVisitResult methodAnnotationVisitor( CXCursor cursor, CXCursor, CXClientData data )
            {
                const CXCursorKind kind = clang_getCursorKind( cursor );
                if ( kind != CXCursor_AnnotateAttr && kind != CXCursor_UnexposedAttr )
                    return CXChildVisit_Continue;

                MethodAnnotation* pAnnotation = static_cast<MethodAnnotation*>( data );
                const CXString    cxSpelling  = clang_getCursorSpelling( cursor );
                const utf8*       pText       = clang_getCString( cxSpelling );
                if ( pText != nullptr )
                {
                    const string_view spelling( pText );
                    if ( spelling.find( annotation::kReflectBodyPrefix ) != string_view::npos )
                        pAnnotation->_bBody = SW_TRUE;
                    if ( pAnnotation->_functionSpelling.empty() && spelling.find( annotation::kFunctionPrefix ) != string_view::npos )
                        pAnnotation->_functionSpelling = pText;
                    if ( pAnnotation->_propertySpelling.empty() && spelling.find( annotation::kPropertyPrefix ) != string_view::npos )
                        pAnnotation->_propertySpelling = pText;
                }
                clang_disposeString( cxSpelling );
                return CXChildVisit_Continue;
            }

            /** @brief REFLECT 타입은 사용자 · 암시 생성자를 자동 등록합니다(FUNCTION 불필요). 복사 · 이동 · 삭제된 것은 뺍니다. */
            static void collectConstructor( const CXCursor cursor, const string& functionSpelling, MemberCollector& collector )
            {
                if ( collector._bSkipConstructors == SW_TRUE )
                    return;
                if ( clang_CXXConstructor_isCopyConstructor( cursor ) || clang_CXXConstructor_isMoveConstructor( cursor ) )
                    return;
#if defined( CINDEX_VERSION_MINOR ) && ( CINDEX_VERSION_MINOR >= 63 || ( defined( CINDEX_VERSION_MAJOR ) && CINDEX_VERSION_MAJOR > 0 ) )
                if ( clang_CXXMethod_isDeleted( cursor ) != 0 )
                    return;
#endif

                ParsedFunctionInfo method;
                method._name           = annotation::kCtorLookupName;
                method._returnTypeName = annotation::kVoidTypeName;
                method._bConstructor   = SW_TRUE;
                method._category       = annotation::kConstructorCategory;
                fillParameters( cursor, method, *collector._pSession );

                const string owner = makeMemberOwnerName( collector._pType->_fullyQualifiedName, annotation::kCtorLookupName );
                if ( functionSpelling.empty() == false && applyAnnotation( functionSpelling, method, *collector._pSession, owner ) == false )
                {
                    collector._bHasError = SW_TRUE;
                    return;
                }
                if ( resolveComponentHandleParameters( method, false, owner ) == false ) // 생성자는 매니저가 없다 — 타입 핸들 인자는 오류다
                {
                    collector._bHasError = SW_TRUE;
                    return;
                }
                collector._pType->_listMethod.push_back( std::move( method ) );
            }

            /**
             * @brief 생성자를 하나도 선언하지 않은 타입(집합체 등)에 `$ctor()` 를 더합니다.
             * @details libclang 은 암시 생성자를 자식 커서로 내지 않아 `collectConstructor` 가 이 타입들을 놓친다. 그러면 `$ctor` 로 만들어 쓰는 곳(설정 창, 스키마 이행)이
             *          이 타입만 못 만든다. 기본 생성이 불가능한 타입(참조 멤버 · 기본 생성자 없는 베이스)인지는 파서가 알 수 없어, 생성 코드가 `if constexpr` 로 가린다.
             */
            static void synthesizeDefaultConstructor( MemberCollector& collector )
            {
                if ( collector._bSkipConstructors == SW_TRUE )
                    return;
                for ( const ParsedFunctionInfo& method : collector._pType->_listMethod )
                {
                    if ( method._bConstructor == SW_TRUE )
                        return;
                }

                ParsedFunctionInfo method;
                method._name           = annotation::kCtorLookupName;
                method._returnTypeName = annotation::kVoidTypeName;
                method._bConstructor   = SW_TRUE;
                method._bSynthesized   = SW_TRUE;
                method._category       = annotation::kConstructorCategory;
                collector._pType->_listMethod.push_back( std::move( method ) );
            }

            /** @brief FUNCTION 이 붙은 메서드를 수집합니다. */
            static void collectMethod( const CXCursor cursor, const string& functionSpelling, MemberCollector& collector )
            {
                // 순수 가상 함수는 실제 AnnotateAttr 가 있어야 한다. 소스 창 휴리스틱이
                // 앞 타입의 FUNCTION(...) 을 집어 `= 0` 메서드를 잘못 등록할 수 있다.
                const bool bHasAttr = functionSpelling.empty() == false;
                if ( bHasAttr == false )
                {
                    if ( clang_CXXMethod_isPureVirtual( cursor ) )
                        return;
                    if ( hasSourceAnnotation( cursor, annotation::kFunction, collector._pSession->_config ) == false )
                        return;
                }

                const ParserSession& session = *collector._pSession;
                ParsedFunctionInfo   method;
                method._name           = getCursorSpelling( cursor );
                method._returnTypeName = session._typeNameMap.normalize( takeString( clang_getTypeSpelling( clang_getCursorResultType( cursor ) ) ) );
                method._bStatic        = clang_CXXMethod_isStatic( cursor ) != 0 ? SW_TRUE : SW_FALSE;
                method._bConst         = clang_CXXMethod_isConst( cursor ) != 0 ? SW_TRUE : SW_FALSE;
                fillParameters( cursor, method, session );

                const string owner = makeMemberOwnerName( collector._pType->_fullyQualifiedName, method._name );
                if ( bHasAttr && applyAnnotation( functionSpelling, method, session, owner ) == false )
                {
                    collector._bHasError = SW_TRUE;
                    return;
                }
                const bool bComponentOwner = isDerivedFromComponent( clang_getCursorSemanticParent( cursor ), session._config );
                if ( resolveComponentHandleParameters( method, bComponentOwner, owner ) == false )
                {
                    collector._bHasError = SW_TRUE;
                    return;
                }
                collector._pType->_listMethod.push_back( std::move( method ) );
            }

            /** @brief 메서드의 이름 · 인자 타입을 적어 둡니다. FUNCTION 이 없어도 — `RepNotify` 처럼 애노테이션이 이름으로 가리킬 수 있다. */
            static void recordMethodSignature( const CXCursor cursor, const string& name, MemberCollector& collector )
            {
                ParsedMethodSignature signature;
                signature._name     = name;
                const int32 numArgs = clang_Cursor_getNumArguments( cursor );
                for ( int32 argIndex = 0; argIndex < numArgs; ++argIndex )
                {
                    const CXCursor argCursor = clang_Cursor_getArgument( cursor, static_cast<uint32>( argIndex ) );
                    signature._listParameterTypeName.push_back(
                        collector._pSession->_typeNameMap.normalize( takeString( clang_getTypeSpelling( clang_getCursorType( argCursor ) ) ) ) );
                }
                collector._listMethodSignature.push_back( std::move( signature ) );
            }

            /** @brief 검증 함수(`void fn( ValidationContext& )`)가 그 이름으로 선언됐는지 봅니다. */
            static bool isValidatorDeclared( const MemberCollector& collector, const string_view name )
            {
                for ( const ParsedMethodSignature& signature : collector._listMethodSignature )
                {
                    const bool bMatchesValidationSignature = signature._name == name && signature._listParameterTypeName.size() == 1 &&
                                                             signature._listParameterTypeName[0] == annotation::kValidationContextTypeName;
                    if ( bMatchesValidationSignature )
                        return true;
                }
                return false;
            }

            /** @brief 이름이 같은 메서드 중 인자 수가 @p maxParameterCount 이하인 첫 것입니다. 없으면 nullptr 입니다. */
            static const ParsedMethodSignature* findMethodSignature( const MemberCollector& collector, const string_view name, const size_t maxParameterCount )
            {
                for ( const ParsedMethodSignature& signature : collector._listMethodSignature )
                {
                    if ( signature._name == name && signature._listParameterTypeName.size() <= maxParameterCount )
                        return &signature;
                }
                return nullptr;
            }

            /**
             * @brief 프로퍼티 애노테이션이 이름으로 가리키는 메서드가 있고 모양이 맞는지 봅니다(`RepNotify`).
             * @details 같은 타입이 선언한 메서드만 찾는다 — 생성 코드는 그 메서드를 직접 부르므로 이름이 틀리면 컴파일 오류가 엉뚱한 .gen.cpp 에서 난다.
             *          여기서 그 헤더의 이름으로 멈춘다. `RepNotify` 는 `void fn()` 또는 `void fn( const T& )`(T = 프로퍼티 타입)이다.
             */
            static void validateMemberFunctions( MemberCollector& collector )
            {
                if ( collector._pType->_validate.empty() == false && isValidatorDeclared( collector, collector._pType->_validate ) == false )
                {
                    SW_LOG_ERROR( "ERROR: REFLECT( Validate = %# ) on '%#' needs a method of the same type: void %#( ValidationContext& context ) [const].",
                                  collector._pType->_validate, collector._pType->_fullyQualifiedName, collector._pType->_validate );
                    collector._bHasError = SW_TRUE;
                }
                for ( ParsedPropertyInfo& prop : collector._pType->_listProperty )
                {
                    if ( prop._validate.empty() == false && isValidatorDeclared( collector, prop._validate ) == false )
                    {
                        SW_LOG_ERROR( "ERROR: PROPERTY( Validate = %# ) on '%#' needs a method of the same type: void %#( ValidationContext& context ) [const].",
                                      prop._validate, makeMemberOwnerName( collector._pType->_fullyQualifiedName, prop._name ), prop._validate );
                        collector._bHasError = SW_TRUE;
                    }
                    if ( prop._repNotify.empty() )
                        continue;
                    const string                 owner      = makeMemberOwnerName( collector._pType->_fullyQualifiedName, prop._name );
                    const ParsedMethodSignature* pSignature = findMethodSignature( collector, prop._repNotify, 1 );
                    const bool                   bTakesOld  = pSignature != nullptr && pSignature->_listParameterTypeName.size() == 1;
                    if ( pSignature == nullptr || ( bTakesOld && pSignature->_listParameterTypeName[0] != prop._typeName ) )
                    {
                        SW_LOG_ERROR( "ERROR: PROPERTY( RepNotify = %# ) on '%#' needs a method of the same type named '%#' taking nothing or the old "
                                      "value (const %#&).",
                                      prop._repNotify, owner, prop._repNotify, prop._typeName );
                        collector._bHasError = SW_TRUE;
                        continue;
                    }
                    prop._bRepNotifyTakesOldValue = bTakesOld ? SW_TRUE : SW_FALSE;
                }
            }

            /**
             * @brief 뜻이 모순인 프로퍼티 플래그 조합을 멈춥니다.
             * @details `RuntimeState` 는 저작 저장만 빼고 스냅샷 · 세이브에는 쓰는 필드다. `Transient`(어디에도 쓰지 않는다)와 함께면 어느 쪽이 이기는지
             *          선언만 보고 알 수 없고, `Required`(데이터 파일이 반드시 적는다)와 함께면 저작 파일이 쓰지 않는 칸을 필수로 요구한다.
             */
            static void validatePropertyFlags( MemberCollector& collector )
            {
                for ( const ParsedPropertyInfo& prop : collector._pType->_listProperty )
                {
                    if ( prop._bRuntimeState == SW_FALSE )
                        continue;
                    const string owner = makeMemberOwnerName( collector._pType->_fullyQualifiedName, prop._name );
                    if ( prop._bTransient == SW_TRUE )
                    {
                        SW_LOG_ERROR( "ERROR: PROPERTY( RuntimeState, Transient ) on '%#' - RuntimeState is written to snapshots and saves, Transient is never "
                                      "written. Keep one.",
                                      owner );
                        collector._bHasError = SW_TRUE;
                    }
                    if ( prop._bRequired == SW_TRUE )
                    {
                        SW_LOG_ERROR( "ERROR: PROPERTY( RuntimeState, Required ) on '%#' - authoring files never write a RuntimeState field, so it cannot be "
                                      "required.",
                                      owner );
                        collector._bHasError = SW_TRUE;
                    }
                }
            }

            /**
             * @brief 프로퍼티의 옛 이름(`PROPERTY( Alias = … )`)이 같은 타입의 지금 이름이나 다른 옛 이름과 겹치지 않는지 봅니다.
             * @details 이름 해시는 대소문자를 가리지 않으므로 비교도 그렇게 한다. 겹치면 저장된 칸이 어느 프로퍼티로 읽힐지 선언만 보고 알 수 없다.
             *          읽기는 지금 이름을 먼저 보지만(`SerializerUtil::findPropertyIndexByNameHash`), 그 순서에 기대는 선언은 여기서 멈춘다.
             */
            static void validatePropertyRedirects( MemberCollector& collector )
            {
                const vector<ParsedPropertyInfo>& listProperty = collector._pType->_listProperty;
                vector<string_view>               listSeenAlias{};
                for ( const ParsedPropertyInfo& prop : listProperty )
                {
                    for ( const string& alias : prop._listAlias )
                    {
                        const string owner = makeMemberOwnerName( collector._pType->_fullyQualifiedName, prop._name );
                        for ( const ParsedPropertyInfo& other : listProperty )
                        {
                            if ( StringUtil::equals( alias, other._name, true ) )
                            {
                                SW_LOG_ERROR( "ERROR: PROPERTY( Alias = %# ) on '%#' is the current name of '%#' - an old name must not be a field that exists now.", alias,
                                              owner, other._name );
                                collector._bHasError = SW_TRUE;
                            }
                        }
                        for ( const string_view seen : listSeenAlias )
                        {
                            if ( StringUtil::equals( alias, seen, true ) )
                            {
                                SW_LOG_ERROR( "ERROR: PROPERTY( Alias = %# ) on '%#' is declared twice in '%#' - one old name reads into one field.", alias, owner,
                                              collector._pType->_fullyQualifiedName );
                                collector._bHasError = SW_TRUE;
                            }
                        }
                        listSeenAlias.push_back( alias );
                    }
                }
            }

            /** @brief 열거자 목록에서 이름(대소문자 무시)이 같은 열거자입니다. 없으면 nullptr 입니다. */
            static const ParsedEnumeratorInfo* findEnumeratorIgnoreCase( const ParsedEnumInfo& enumInfo, const string_view name )
            {
                for ( const ParsedEnumeratorInfo& enumerator : enumInfo._listEnumerator )
                {
                    if ( StringUtil::equals( enumerator._name, name, true ) )
                        return &enumerator;
                }
                return nullptr;
            }

            /**
             * @brief 열거자 리다이렉트(`ValueAlias` · `Removed`)를 검사합니다. 틀리면 오류를 남기고 false 입니다.
             * @details 항목마다 `옛 이름:대상` 꼴이어야 하고, 대상은 지금 열거자여야 하며(대소문자까지 같게 적는다. 생성 코드가 그 이름으로 값을 읽는다),
             *          옛 이름은 지금 열거자가 아니어야 하고, 두 목록을 합쳐 한 번만 적는다. 대상이 없는 리다이렉트를 버리면 선언한 이름 변경이 말없이 사라진다.
             */
            [[nodiscard]] static bool validateEnumeratorRedirects( const ParsedEnumInfo& enumInfo )
            {
                bool                                      bValid       = true;
                const vector<pair<string, string>>* const arrList[]    = { &enumInfo._listValueAlias, &enumInfo._listRemovedEnumerator };
                const utf8* const                         arrKeyName[] = { "ValueAlias", "Removed" };
                vector<string_view>                       listSeenOld{};
                for ( size_t listIndex = 0; listIndex < std::size( arrList ); ++listIndex )
                {
                    for ( const auto& [oldName, targetName] : *arrList[listIndex] )
                    {
                        const utf8* const pKey = arrKeyName[listIndex];
                        if ( oldName.empty() || targetName.empty() )
                        {
                            SW_LOG_ERROR( "ERROR: ENUM( %# ) on '%#' expects \"Old:Target\", got '%#:%#'.", pKey, enumInfo._fullyQualifiedName, oldName, targetName );
                            bValid = false;
                            continue;
                        }
                        const ParsedEnumeratorInfo* pTarget = findEnumeratorIgnoreCase( enumInfo, targetName );
                        if ( pTarget == nullptr || pTarget->_name != targetName )
                        {
                            SW_LOG_ERROR( "ERROR: ENUM( %# = \"%#:%#\" ) on '%#' - '%#' is not an enumerator of it (spell it as declared).", pKey, oldName, targetName,
                                          enumInfo._fullyQualifiedName, targetName );
                            bValid = false;
                        }
                        const ParsedEnumeratorInfo* pCurrent = findEnumeratorIgnoreCase( enumInfo, oldName );
                        if ( pCurrent != nullptr )
                        {
                            SW_LOG_ERROR( "ERROR: ENUM( %# = \"%#:%#\" ) on '%#' - '%#' is still an enumerator; an old name must be one that no longer exists.", pKey,
                                          oldName, targetName, enumInfo._fullyQualifiedName, pCurrent->_name );
                            bValid = false;
                        }
                        for ( const string_view seen : listSeenOld )
                        {
                            if ( StringUtil::equals( seen, oldName, true ) )
                            {
                                SW_LOG_ERROR( "ERROR: ENUM() on '%#' declares the old enumerator name '%#' twice (ValueAlias and Removed together count once).",
                                              enumInfo._fullyQualifiedName, oldName );
                                bValid = false;
                            }
                        }
                        listSeenOld.push_back( oldName );
                    }
                }
                return bValid;
            }

            /**
             * @brief 표시 메타를 검사하고 정리합니다 — `Units` 는 단위 표에 있어야 하고(커스텀 메타 `Units` 로 싣는다), 표에 있는 단위를 `Meta = "Units=…"` 로
             *        적으면 거절하고, `EditCondition` 은 네 꼴 중 하나여야 하며, C 고정 배열의 원소는 컨테이너일 수 없습니다.
             * @details 조건식이 가리키는 이름은 기반 클래스의 것일 수 있어 여기서는 꼴만 본다 — 이름은 등록된 뒤 `PropertyEditCondition::parse` 가 보고,
             *          `ReflectionDisplayMetaTest.EveryEditConditionResolves` 가 모든 타입을 대조한다.
             */
            [[nodiscard]] static bool applyDisplayMeta( ParsedPropertyInfo& prop, const CXType fieldType, const string_view owner )
            {
                // `Meta` 의 `Units` 는 철자 검사를 받지 않으므로 표에 없는 글자(`HP`)만 받는다 — 표에 있는 단위는 `Units = …` 로 적는다.
                for ( const auto& [key, value] : prop._listCustomMeta )
                {
                    if ( key == "Units" && ReflectUnitUtil::findUnit( value ) != nullptr )
                    {
                        SW_LOG_ERROR( "ERROR: '%#' writes Meta = \"Units=%#\" - a known unit is written PROPERTY( Units = %# ).", owner, value, value );
                        return false;
                    }
                }

                if ( prop._units.empty() == false )
                {
                    if ( ReflectUnitUtil::findUnit( prop._units ) == nullptr )
                    {
                        SW_LOG_ERROR( "ERROR: PROPERTY( Units = %# ) on '%#' - unknown unit. Known: %#. A free-form label is written Meta = \"Units=…\".",
                                      prop._units, owner, ReflectUnitUtil::makeUnitList() );
                        return false;
                    }
                    for ( const auto& [key, value] : prop._listCustomMeta )
                    {
                        if ( key == "Units" )
                        {
                            SW_LOG_ERROR( "ERROR: '%#' gives Units twice (Units = %# and Meta = \"Units=%#\").", owner, prop._units, value );
                            return false;
                        }
                    }
                    prop._listCustomMeta.emplace_back( "Units", prop._units );
                }

                if ( prop._editCondition.empty() == false && isEditConditionWellFormed( prop._editCondition ) == false )
                {
                    SW_LOG_ERROR( "ERROR: PROPERTY( EditCondition = \"%#\" ) on '%#' - use name, !name, name == Value or name != Value.", prop._editCondition, owner );
                    return false;
                }

                const bool bFixedArrayOfContainers = fieldType.kind == CXType_ConstantArray && prop._containerTree != nullptr && prop._containerTree->_elementNested != nullptr;
                if ( bFixedArrayOfContainers )
                {
                    SW_LOG_ERROR( "ERROR: PROPERTY() on '%#' is a fixed array of containers - use a container of containers (vector<array<…>>) instead.", owner );
                    return false;
                }
                return true;
            }

            /** @brief 식별자(영문 · 숫자 · `_`, 숫자로 시작하지 않는다)인지 봅니다. */
            static bool isIdentifier( const string_view text ) noexcept
            {
                if ( text.empty() || ( '0' <= text.front() && text.front() <= '9' ) )
                    return false;
                for ( const utf8 character : text )
                {
                    const bool bWord = ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' ) || ( '0' <= character && character <= '9' ) ||
                                       character == '_';
                    if ( bWord == false )
                        return false;
                }
                return true;
            }

            /** @brief `name` · `!name` · `name == Value` · `name != Value` 꼴인지 봅니다(값은 식별자 · 정수 · `Enum::Value`). */
            static bool isEditConditionWellFormed( const string_view text )
            {
                const string_view trimmed = StringUtil::trim( text );
                const size_t      opPos   = trimmed.find( "==" ) != string_view::npos ? trimmed.find( "==" ) : trimmed.find( "!=" );
                if ( opPos == string_view::npos )
                {
                    const string_view name = ( trimmed.empty() == false && trimmed.front() == '!' ) ? StringUtil::trim( trimmed.substr( 1 ) ) : trimmed;
                    return isIdentifier( name );
                }
                const string_view name  = StringUtil::trim( trimmed.substr( 0, opPos ) );
                string_view       value = StringUtil::trim( trimmed.substr( opPos + 2 ) );
                const size_t      scope = value.rfind( "::" );
                if ( scope != string_view::npos )
                    value = value.substr( scope + 2 );
                int64 number{ 0 };
                return isIdentifier( name ) && ( isIdentifier( value ) || StringUtil::parseInt64( value, number ) );
            }

            /** @brief `Interp` 는 시퀀서가 섞을 수 있는 타입에만 — 컨테이너 · 비트필드 · 그 밖의 타입은 그 헤더를 멈춥니다. */
            static bool isInterpAllowed( const ParsedPropertyInfo& prop, const string_view owner )
            {
                if ( prop._bInterp == SW_FALSE )
                    return true;
                bool bKnownType = false;
                for ( const utf8* pTypeName : annotation::kArrInterpTypeName )
                {
                    if ( prop._typeName == pTypeName )
                        bKnownType = true;
                }
                const bool bAnimatableProperty = bKnownType && prop._bIsContainer == SW_FALSE && prop._bIsBitField == SW_FALSE;
                if ( bAnimatableProperty )
                    return true;
                SW_LOG_ERROR( "ERROR: PROPERTY( Interp ) on '%#' (%#) - only a single number, float2/3/4 or quaternion can be animated by a value track.",
                              owner, prop._typeName );
                return false;
            }

            /** @brief 함수 꼴 멤버 하나 — 마커 · 생성자 · 메서드로 나눕니다. */
            static void collectFunctionMember( const CXCursor cursor, const CXCursorKind kind, MemberCollector& collector )
            {
                // REFLECT_BODY 마커 함수다. 리플렉트 FUNCTION 이 아니다.
                const string spelling = getCursorSpelling( cursor );
                if ( spelling == annotation::kReflectBodyMarkerFn )
                {
                    collector._bBodyFound = SW_TRUE;
                    return;
                }

                MethodAnnotation annotation;
                clang_visitChildren( cursor, methodAnnotationVisitor, &annotation );
                if ( annotation._bBody == SW_TRUE )
                    collector._bBodyFound = SW_TRUE;

                if ( kind == CXCursor_Constructor )
                {
                    collectConstructor( cursor, annotation._functionSpelling, collector );
                    return;
                }
                if ( kind == CXCursor_CXXMethod )
                {
                    recordMethodSignature( cursor, spelling, collector );
                    if ( annotation._propertySpelling.empty() == false )
                    {
                        if ( annotation._functionSpelling.empty() == false )
                        {
                            SW_LOG_ERROR( "ERROR: '%#' has both PROPERTY() and FUNCTION(). A method is either a value accessor (PROPERTY) or a callable (FUNCTION).",
                                          makeMemberOwnerName( collector._pType->_fullyQualifiedName, spelling ) );
                            collector._bHasError = SW_TRUE;
                            return;
                        }
                        collectAccessorProperty( cursor, annotation._propertySpelling, collector );
                        return;
                    }
                    collectMethod( cursor, annotation._functionSpelling, collector );
                    return;
                }

                // 멤버 함수 템플릿은 인자 타입이 정해지지 않아 등록할 수 없다. 클래스 템플릿처럼 알리고 건너뛴다.
                if ( kind == CXCursor_FunctionTemplate && annotation._functionSpelling.empty() == false )
                {
                    SW_LOG_WARNING( "FUNCTION on a member function template is not supported, skipping: %#",
                                    makeMemberOwnerName( collector._pType->_fullyQualifiedName, spelling ) );
                }
            }

            /**
             * @brief REFLECT 타입 멤버를 한 번의 자식 순회로 수집합니다.
             * @details 오류가 나도 순회를 멈추지 않는다 — 한 번의 실행이 그 타입의 오류를 모두 알린다. 오류 난 멤버는 수집 함수가 목록에
             *          넣기 전에 돌아오므로 반쯤 찬 DTO 가 코드젠에 들어가지 않고, `_bHasError` 가 헤더를 실패로 만든다.
             */
            static CXChildVisitResult memberCollectVisitor( CXCursor cursor, CXCursor, CXClientData data )
            {
                MemberCollector&   collector = *static_cast<MemberCollector*>( data );
                const CXCursorKind kind      = clang_getCursorKind( cursor );
                const bool         bFunction = kind == CXCursor_CXXMethod || kind == CXCursor_Constructor || kind == CXCursor_FunctionTemplate ||
                                       kind == CXCursor_FunctionDecl || kind == CXCursor_Destructor;
                const bool bExportAttr = kind == CXCursor_DLLExport || kind == CXCursor_DLLImport || kind == CXCursor_VisibilityAttr;
                const bool bBusVersion = kind == CXCursor_VarDecl && isStringEqual( clang_getCursorSpelling( cursor ), annotation::kBusVersionMember );
                if ( kind == CXCursor_CXXBaseSpecifier )
                    collectBase( cursor, collector );
                else if ( kind == CXCursor_FieldDecl )
                    collectField( cursor, collector );
                else if ( bFunction )
                    collectFunctionMember( cursor, kind, collector );
                else if ( bBusVersion )
                    collector._bBusBody = SW_TRUE;
                else if ( bExportAttr )
                    collector._bExported = SW_TRUE;
                return CXChildVisit_Continue;
            }

            /**
             * @brief 서버 버스 본문 타입(`kBusVersion`)이 모듈 내보내기 매크로를 달았는지 봅니다. 아니면 알리고 `_bHasError` 를 켭니다.
             * @details `ServerBusTopic<T>` 는 `T::StaticType()` 을 부르고, 그 정의는 `T` 를 선언한 모듈의 `.gen.cpp` 에 있다. 내보내기가 없으면
             *          다른 모듈(서버 키트)의 링크에서야 미해결 기호로 드러난다. C++ 형식으로는 막을 수 없다. 내보내기 여부를 보는 타입 특성이 없고,
             *          리눅스의 visibility 는 컴파일에 드러나지 않는다. 생성기는 AST 의 속성을 보므로 여기서 막는다.
             *          파서는 Base · 키트 매크로를 속성으로 읽는다(Windows 는 `parser_config` 가 `SW_GF_IMPORTS` · `SW_KIT_IMPORTS` 를 정의하고, 리눅스는 늘 visibility 다).
             *          모듈 경계를 넘지 않는 타깃(`--module-local`: 실행 파일 · 게임 모듈)은 보지 않는다.
             */
            static void validateBusBodyExport( MemberCollector& collector )
            {
                const bool bMissingExport = collector._bBusBody == SW_TRUE && collector._bExported == SW_FALSE;
                if ( bMissingExport == false || collector._pSession->_bModuleLocal )
                    return;
                SW_LOG_ERROR( "ERROR: '%#' declares '%#' (a server bus body type) but has no module export macro. "
                              "Another module's ServerBusTopic<T> calls T::StaticType(), which then fails to link. "
                              "Write the module's macro before the name: 'struct SW_KIT_API %#' (SW_GF_API in Base, SW_KIT_SERVER_API in a server kit).",
                              collector._pType->_fullyQualifiedName, annotation::kBusVersionMember, collector._pType->_name );
                collector._bHasError = SW_TRUE;
            }

            // ------------------------------------------------------------------------------
            // E) 컴포넌트 판별 · 열거자
            // ------------------------------------------------------------------------------
            struct ComponentSearch
            {
                const ParserConfig* _pConfig;
                bool                _bDerives;
            };

            /** @brief Component / SceneComponent 파생인지 베이스 체인을 검사합니다. */
            static CXChildVisitResult componentBaseVisitor( CXCursor cursor, CXCursor, CXClientData clientData )
            {
                if ( clang_getCursorKind( cursor ) != CXCursor_CXXBaseSpecifier )
                    return CXChildVisit_Continue;

                ComponentSearch* pSearch  = static_cast<ComponentSearch*>( clientData );
                const CXCursor   baseDecl = getBaseClassDeclaration( cursor );
                if ( clang_Cursor_isNull( baseDecl ) != 0 || isDerivedFromComponent( baseDecl, *pSearch->_pConfig ) == false )
                    return CXChildVisit_Continue;

                pSearch->_bDerives = true;
                return CXChildVisit_Break;
            }

            static bool isDerivedFromComponent( const CXCursor cursor, const ParserConfig& config )
            {
                // 같은 중간 베이스 클래스를 거듭 재귀 탐색하지 않게 한다(스레드마다 한 벌).
                thread_local unordered_map<string, bool> s_mapFqnToDerives;

                const string fqn = makeFullyQualifiedName( cursor );
                for ( const string& baseType : config._listComponentBaseType )
                {
                    if ( fqn == baseType )
                        return true;
                }

                const auto cacheIt = s_mapFqnToDerives.find( fqn );
                if ( cacheIt != s_mapFqnToDerives.end() )
                    return cacheIt->second;

                // 캐시 미스. 순환 참조를 막으려고 먼저 false 로 넣는다
                s_mapFqnToDerives[fqn] = false;

                ComponentSearch search{ &config, false };
                clang_visitChildren( cursor, componentBaseVisitor, &search );
                s_mapFqnToDerives[fqn] = search._bDerives;
                return search._bDerives;
            }

            /** @brief enumerator 이름·값을 수집합니다. */
            static CXChildVisitResult enumeratorCollectorVisitor( CXCursor cursor, CXCursor parent, CXClientData data )
            {
                if ( clang_getCursorKind( cursor ) != CXCursor_EnumConstantDecl )
                    return CXChildVisit_Continue;

                ParsedEnumeratorInfo enumerator;
                enumerator._name = getCursorSpelling( cursor );
                // 밑바탕이 부호 없는 enum 은 부호 없는 값으로 읽는다 — `clang_getEnumConstantDeclValue` 는 늘 부호 있게 넓혀 `uint8` 의 200 이
                // -56 이 된다(생성 코드는 컴파일러 식으로 내므로 맞지만, `--dump` 와 파서 안의 판단이 이 값을 본다).
                const CXType integerType = clang_getCanonicalType( clang_getEnumDeclIntegerType( parent ) );
                const bool   bUnsigned   = integerType.kind == CXType_Bool || integerType.kind == CXType_Char_U || integerType.kind == CXType_UChar ||
                                       integerType.kind == CXType_UShort || integerType.kind == CXType_UInt || integerType.kind == CXType_ULong ||
                                       integerType.kind == CXType_ULongLong;
                enumerator._value = bUnsigned ? static_cast<int64>( clang_getEnumConstantDeclUnsignedValue( cursor ) ) : clang_getEnumConstantDeclValue( cursor );
                static_cast<vector<ParsedEnumeratorInfo>*>( data )->push_back( std::move( enumerator ) );
                return CXChildVisit_Continue;
            }

            // ------------------------------------------------------------------------------
            // F) 선언 검증 — REFLECT 밖의 PROPERTY · FUNCTION · REFLECT_BODY
            // ------------------------------------------------------------------------------
            /**
             * @brief 멤버에 붙은 애노테이션이 REFLECT 타입 안에 있는지 봅니다. 아니면 알리고 false 입니다.
             * @param pMacroName 멤버 쪽 매크로 이름(오류 메시지용)
             */
            static bool isInsideReflectType( const CXCursor member, const utf8* pMacroName, const ParserConfig& config )
            {
                const CXCursor parent = clang_getCursorSemanticParent( member );
                if ( hasAnnotation( parent, annotation::kReflect, config ) )
                    return true;

                SW_LOG_ERROR( "ERROR: %#() is used in class/struct '%#', but it lacks REFLECT()!", pMacroName,
                              makeFullyQualifiedName( parent ).c_str() );
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AstVisitor::AstVisitor( CXTranslationUnit translationUnit, const ParserSession& session, const vector<string>& listTargetFile )
        : _translationUnit{ translationUnit }
        , _pSession{ &session }
        , _listTargetPath{ listTargetFile }
        , _listTargetFile{}
        , _listHeader{}
        , _pLastFile{ nullptr }
        , _lastTargetIndex{ kNoTarget }
        , _bHasError{ SW_FALSE }
        , _reserved{ 0 }
    {
        if ( _listTargetPath.empty() )
            _listTargetPath.push_back( AstVisitorInternal::takeString( clang_getTranslationUnitSpelling( translationUnit ) ) );

        _listTargetFile.reserve( _listTargetPath.size() );
        for ( const string& path : _listTargetPath )
        {
            _listTargetFile.push_back( clang_getFile( translationUnit, path.c_str() ) );
        }
        _listHeader.resize( _listTargetPath.size() );
    }

    // ------------------------------------------------------------------------------
    // G) AstVisitor — visit / onStructDeclaration / onEnumDeclaration
    // ------------------------------------------------------------------------------
    bool AstVisitor::visit()
    {
        // 대상 헤더를 번역 단위에서 못 찾으면 그 헤더의 산출물은 **조용히 비게** 된다. 그 자리에서 멈춘다.
        for ( uint32 index = 0; index < _listTargetFile.size(); ++index )
        {
            if ( _listTargetFile[index] == nullptr )
            {
                SW_LOG_ERROR( "ERROR: '%#' is not part of the parsed translation unit.", _listTargetPath[index] );
                _bHasError = SW_TRUE;
            }
        }
        if ( _bHasError == SW_TRUE )
            return false;

        clang_visitChildren( clang_getTranslationUnitCursor( _translationUnit ), visitCursor, this );
        return _bHasError == SW_FALSE;
    }

    /**
     * @details 오류는 헤더 단위로 남긴다. 순회 전체를 멈추면(`CXChildVisit_Break`) 헤더 여럿을 한 TU 로 묶을 때 한 헤더의 오타가
     *          나머지 헤더의 수집까지 막는다. 그 헤더만 실패로 두고 계속 돌아, 오류도 헤더마다 다 나온다.
     */
    void AstVisitor::markHeaderError( ParsedHeader& header )
    {
        header._bHasError = SW_TRUE;
        _bHasError        = SW_TRUE;
    }

    /**
     * @details 매크로가 만든 선언은 매크로를 **쓴** 자리(전개 위치)의 파일로 셉니다. `clang_Location_isFromMainFile` 로 물으면 매크로
     *          위치를 통째로 밖으로 쳐서 `REFLECT_BODY()` 가 만드는 마커 함수가 검사되지 않고, "REFLECT_BODY() 인데 REFLECT() 가 없다"
     *          검사가 죽습니다. 여러 헤더를 한 TU 로 묶을 때도 같은 질문이면 됩니다.
     */
    int32 AstVisitor::findTargetIndex( const CXCursor cursor ) const
    {
        CXFile file = nullptr;
        clang_getExpansionLocation( clang_getCursorLocation( cursor ), &file, nullptr, nullptr, nullptr );
        if ( file == nullptr )
            return kNoTarget;
        if ( file == _pLastFile )
            return _lastTargetIndex;

        int32 targetIndex = kNoTarget;
        for ( uint32 index = 0; index < _listTargetFile.size(); ++index )
        {
            if ( clang_File_isEqual( file, _listTargetFile[index] ) != 0 )
            {
                targetIndex = static_cast<int32>( index );
                break;
            }
        }
        _pLastFile       = file;
        _lastTargetIndex = targetIndex;
        return targetIndex;
    }

    CXChildVisitResult AstVisitor::visitCursor( CXCursor cursor, CXCursor, CXClientData clientData )
    {
        AstVisitor*        self = static_cast<AstVisitor*>( clientData );
        const CXCursorKind kind = clang_getCursorKind( cursor );

        if ( kind == CXCursor_Namespace )
            return CXChildVisit_Recurse;

        // include 된 외부 · 시스템 헤더의 선언은 바로 건너뛴다(AST 순회 비용을 크게 줄인다)
        const int32 targetIndex = self->findTargetIndex( cursor );
        if ( targetIndex == kNoTarget )
            return CXChildVisit_Continue;

        ParsedHeader&       header = self->_listHeader[static_cast<size_t>( targetIndex )];
        const ParserConfig& config = self->_pSession->_config;

        if ( kind == CXCursor_FieldDecl )
        {
            const bool bOrphanProperty = AstVisitorInternal::hasAnnotation( cursor, annotation::kProperty, config ) &&
                                         AstVisitorInternal::isInsideReflectType( cursor, annotation::kPropertyMacro, config ) == false;
            if ( bOrphanProperty )
                self->markHeaderError( header );
            return CXChildVisit_Continue;
        }

        if ( kind == CXCursor_CXXMethod )
        {
            const bool bHasFunction = AstVisitorInternal::hasAnnotation( cursor, annotation::kFunction, config );
            // 메서드의 PROPERTY 는 실제 애노테이션만 본다(값 참조 접근자). 소스 창 휴리스틱은 필드 쪽 것이다.
            const bool bHasProperty = AstVisitorInternal::findAnnotateAttr( cursor, annotation::kPropertyPrefix ).empty() == false;
            const bool bHasBody =
                AstVisitorInternal::findAnnotateAttr( cursor, annotation::kReflectBodyPrefix ).empty() == false ||
                AstVisitorInternal::isStringEqual( clang_getCursorSpelling( cursor ), annotation::kReflectBodyMarkerFn );
            const utf8* pMacroName   = bHasFunction ? annotation::kFunctionMacro
                                                    : ( bHasProperty ? annotation::kPropertyMacro : annotation::kReflectBodyPrefix );
            const bool  bOrphanMacro = ( bHasFunction || bHasProperty || bHasBody ) && AstVisitorInternal::isInsideReflectType( cursor, pMacroName, config ) == false;
            if ( bOrphanMacro )
                self->markHeaderError( header );
            return CXChildVisit_Continue;
        }

        // 클래스 템플릿은 FQN 에 인자가 없어 offsetof / TypeRegistrar<T> 가 성립하지 않는다.
        // 조용히 빠지면 원인을 찾기 어려우므로 경고만 남기고 건너뛴다.
        if ( kind == CXCursor_ClassTemplate || kind == CXCursor_ClassTemplatePartialSpecialization )
        {
            if ( AstVisitorInternal::hasAnnotation( cursor, annotation::kReflect, config ) )
            {
                SW_LOG_WARNING( "REFLECT on class template is not supported, skipping: %#",
                                AstVisitorInternal::makeFullyQualifiedName( cursor ) );
            }
            return CXChildVisit_Continue;
        }

        if ( kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl )
        {
            if ( AstVisitorInternal::hasAnnotation( cursor, annotation::kReflect, config ) )
                self->onStructDeclaration( cursor, header );
            return CXChildVisit_Recurse;
        }

        if ( kind == CXCursor_EnumDecl && AstVisitorInternal::hasAnnotation( cursor, annotation::kEnum, config ) )
            self->onEnumDeclaration( cursor, header );
        return CXChildVisit_Continue;
    }

    /**
     * @brief `REFLECT(...)` 매크로가 붙은 struct/class 선언을 파싱합니다.
     *
     * 수집 단계:
     * 1. 클래스 이름과 네임스페이스를 포함한 전체 경로(FQN)
     * 2. `REFLECT(...)` 매크로 인자(Abstract, Static, Category, Alias 등)
     * 3. 부모 클래스를 찾아 상속 관계 연결
     * 4. `REFLECT_BODY()` 매크로가 있는지 확인
     * 5. 멤버 변수(`PROPERTY`)와 멤버 함수(`FUNCTION`) 메타데이터 수집
     */
    void AstVisitor::onStructDeclaration( const CXCursor cursor, ParsedHeader& outHeader )
    {
        ParsedTypeInfo typeInfo;
        typeInfo._name               = AstVisitorInternal::getCursorSpelling( cursor );
        typeInfo._fullyQualifiedName = AstVisitorInternal::makeFullyQualifiedName( cursor );

        BLOCK( "Parse REFLECT Annotation" )
        {
            const string spelling = AstVisitorInternal::readAnnotation( cursor, annotation::kReflect, _pSession->_config );
            if ( spelling.empty() == false &&
                 AstVisitorInternal::applyAnnotation( spelling, typeInfo, *_pSession, typeInfo._fullyQualifiedName ) == false )
            {
                markHeaderError( outHeader );
                return;
            }
            // 순수 가상 함수가 있는 추상 클래스이면 UCLASS(Abstract) 처럼 Abstract 플래그를 켠다
            if ( clang_CXXRecord_isAbstract( cursor ) != 0 )
                typeInfo._bAbstract = SW_TRUE;
        }

        BLOCK( "Collect Bases / Markers / Fields / Methods" )
        {
            AstVisitorInternal::MemberCollector collector( typeInfo, *_pSession );
            clang_visitChildren( cursor, AstVisitorInternal::memberCollectVisitor, &collector );
            // 메서드는 프로퍼티 뒤에 선언될 수 있다 — 이름으로 가리키는 애노테이션은 멤버를 다 모은 뒤에 대조한다.
            AstVisitorInternal::validateMemberFunctions( collector );
            AstVisitorInternal::validatePropertyFlags( collector );
            AstVisitorInternal::validatePropertyRedirects( collector );
            AstVisitorInternal::validateBusBodyExport( collector );
            if ( collector._bHasError == SW_TRUE )
            {
                markHeaderError( outHeader );
                return;
            }
            AstVisitorInternal::synthesizeDefaultConstructor( collector );
            // 추상 타입(REFLECT(Abstract) · C++ 추상)은 팩토리를 내지 않는다 — 팩토리는 `addComponent<T>()` 로 T 를 만든다. 공통 기반
            // 컴포넌트(`LightComponent`)가 그 예다.
            const bool bRequiresFactory = AstVisitorInternal::isDerivedFromComponent( cursor, _pSession->_config );
            const bool bFactory         = bRequiresFactory && typeInfo._bAbstract == SW_FALSE;
            typeInfo._bReflectBody      = collector._bBodyFound == SW_TRUE ? SW_TRUE : SW_FALSE;
            typeInfo._bComponentFactory = bFactory ? SW_TRUE : SW_FALSE;
        }

        // REFLECT() 를 붙였으면 REFLECT_BODY() 도 있어야 한다. 없으면 그 타입만 StaticType() 이
        // 없어서, 다른 타입은 `T::StaticType()` 으로 되는 일을 그 타입만 레지스트리 이름 조회로
        // 우회해야 한다. 쓰는 쪽이 타입마다 접근 방법을 외워야 하는 상태가 된다. 경고로 두면
        // 그냥 지나치게 되므로 생성 자체를 실패시킨다.
        if ( typeInfo._bReflectBody == SW_FALSE )
        {
            SW_LOG_ERROR( "ERROR: REFLECT() is used in class/struct '%#', but it lacks REFLECT_BODY()! "
                          "Add REFLECT_BODY(); as the first line of the type body "
                          "(and include \"Engine/Reflection/ReflectionMacros.h\").",
                          typeInfo._fullyQualifiedName.c_str() );
            markHeaderError( outHeader );
            return;
        }

        SW_LOG_TRACE( "REFLECT class : %#  (props=%# methods=%# abstract=%# static=%# body=%# factory=%#)",
                      typeInfo._fullyQualifiedName, typeInfo._listProperty.size(), typeInfo._listMethod.size(),
                      typeInfo._bAbstract ? 1 : 0, typeInfo._bStatic ? 1 : 0, typeInfo._bReflectBody ? 1 : 0,
                      typeInfo._bComponentFactory ? 1 : 0 );
        outHeader._listType.push_back( std::move( typeInfo ) );
    }

    /**
     * @brief `ENUM(...)` 매크로가 붙은 열거형(Enum / Enum Class) 선언을 파싱합니다.
     *
     * 수집 단계:
     * 1. 열거형의 모든 원소(Enumerator) 이름과 정수 값
     * 2. `ENUM(...)` 어노테이션 속성(Alias, Count, Invalid 등)
     * 3. 비트플래그 여부는 `ENUM( Flags )` 선언으로만 정합니다(값 모양으로 추측하지 않습니다. 본문 주석 참고).
     */
    void AstVisitor::onEnumDeclaration( const CXCursor cursor, ParsedHeader& outHeader )
    {
        ParsedEnumInfo enumInfo;
        enumInfo._name               = AstVisitorInternal::getCursorSpelling( cursor );
        enumInfo._fullyQualifiedName = AstVisitorInternal::makeFullyQualifiedName( cursor );

        BLOCK( "Collect forward-declaration facts" )
        {
            // ENUM(Flags) 의 비트 연산자 트레이트는 이 열거형을 **전방 선언** 한 뒤 특수화한다
            // (`CodeGenerator::makeHeaderText`). 그래서 기반 정수 타입이 필요하다. 원본 철자로
            // 받아야 `uint8` 같은 별칭이 아니라 `unsigned char` 가 나와 재선언이 어긋나지 않는다.
            enumInfo._underlyingType = AstVisitorInternal::takeString(
                clang_getTypeSpelling( clang_getCanonicalType( clang_getEnumDeclIntegerType( cursor ) ) ) );

            // 클래스 안에 든 열거형은 밖에서 전방 선언할 수 없다. 코드젠이 그 사실을 알아야
            // 조용히 깨진 헤더를 내보내지 않고 그 자리에서 알린다.
            const CXCursorKind parentKind = clang_getCursorKind( clang_getCursorSemanticParent( cursor ) );
            const bool         bNested    = parentKind == CXCursor_ClassDecl || parentKind == CXCursor_StructDecl ||
                                 parentKind == CXCursor_ClassTemplate || parentKind == CXCursor_UnionDecl;
            enumInfo._bNestedInType = bNested ? SW_TRUE : SW_FALSE;
        }

        // 모든 열거자 항목(이름, 정수값) 수집
        clang_visitChildren( cursor, AstVisitorInternal::enumeratorCollectorVisitor, &enumInfo._listEnumerator );

        BLOCK( "Parse ENUM annotation (Alias / …)" )
        {
            // 소스의 ENUM(...) 을 우선해 Alias= 가 BitFlag annotate 에 가려지지 않게 한다.
            string spelling = AstVisitorInternal::extractSourceAnnotation( cursor, annotation::kEnum, _pSession->_config );
            if ( spelling.empty() )
                spelling = AstVisitorInternal::findAnnotateAttr( cursor, annotation::kEnumPrefix );
            if ( spelling.empty() == false &&
                 AstVisitorInternal::applyAnnotation( spelling, enumInfo, *_pSession, enumInfo._fullyQualifiedName ) == false )
            {
                markHeaderError( outHeader );
                return;
            }
            if ( enumInfo._countEnumerator.empty() == false && enumInfo._invalidEnumerator.empty() )
                enumInfo._invalidEnumerator = enumInfo._countEnumerator;
            if ( AstVisitorInternal::validateEnumeratorRedirects( enumInfo ) == false )
            {
                markHeaderError( outHeader );
                return;
            }
        }

        // **비트플래그인지는 선언이 정한다. 값의 모양이 아니다.** 비트플래그 열거형은 `ENUM( Flags )` 로 **명시한다.**
        //
        // 주의: "0 이 아닌 값이 모두 2의 거듭제곱이면 BitFlag" 같은 자동 감지를 두지 말 것 — `{ Game = 0, Editor = 1, Custom = 2 }`
        // 같은 **평범한 연속 열거형**에도 맞아 문자열 변환이 `toStringFlags` 로 가고 인스펙터가 콤보 대신 체크박스를 그리며,
        // C++ 트레이트(`IsBitFlagEnum<>`, 명시한 `ENUM( Flags )` 로만 나간다)와 레지스트리가 다른 답을 낸다. 명시하지 않아 놓친 쪽은
        // 비트 연산자가 없어 컴파일이 그 자리에서 막히지만, 명시하지 않았는데 켜지는 쪽은 조용히 틀린다.

        SW_LOG_INFO( "ENUM          : %#  (%# values, _bIsBitFlag=%# aliases=%#)",
                     enumInfo._fullyQualifiedName, enumInfo._listEnumerator.size(), enumInfo._bIsBitFlag ? "true" : "false",
                     enumInfo._listAlias.size() );
        outHeader._listEnum.push_back( std::move( enumInfo ) );
    }
} // namespace sw
