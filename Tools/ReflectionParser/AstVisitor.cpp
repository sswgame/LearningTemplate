#include "pch.h"

#include "ReflectionParser/AstVisitor.h"

#include "Core/Common/Types.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/Common.h"
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
            // B) 애노테이션 찾기 — 자식 속성(AnnotateAttr)이 정본이고, 소스 텍스트는 그것이 빠졌을 때의 폴백이다
            //
            // 예전에는 같은 질문을 다섯 벌로 물었다: 자식 속성 검색 두 종류(하나 · 여럿), 소스 폴백 두 종류(있나 ·
            // 읽기), 그리고 그 조합을 호출부마다 손으로. 접두사와 매크로 철자도 따로 들고 다녀서, 소스 폴백은 접두사로
            // 표를 다시 뒤져 매크로 철자를 찾았다. 이제 애노테이션 한 벌(`ReflectAnnotationDesc`)을 넘긴다.
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
                            if ( window[index] == '*' && index + 1 < pos && window[index + 1] == '/' )
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
             * @details 따옴표 안(애노테이션 문자열)은 경계가 아닙니다. 예전에는 따옴표 안도 셌다 — `Tooltip = "…; …"` 하나로 그 타입의
             *          `REFLECT()` 가 "앞 선언의 것" 이 되어, 멤버마다 "REFLECT() 가 없다" 는 엉뚱한 오류로 빌드가 섰다.
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
                    if ( character == '"' )
                        bInQuote = true;
                    else if ( character == ';' || character == '{' || character == '}' )
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
             * @details 예전에는 모르는 토큰을 **조용히 버렸습니다.** 조명 컴포넌트 셋의 `PROPERTY( …, Color, … )` 가 그렇게
             *          사라져 있었습니다 — 에디터 색 선택기 요청의 철자는 `Meta = "Color"` 이고, 멤버 이름에 color 가 들어 있어
             *          인스펙터의 이름 휴리스틱이 증상을 가리고 있었습니다.
             * @return 모르는 토큰이 없으면 true
             */
            template <typename TParsed>
            static bool applyAnnotation( const string_view spelling, TParsed& target, const ParserSession& session,
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
                const size_t start = typeStr.find( '<' );
                const size_t end   = typeStr.rfind( '>' );
                if ( start == string_view::npos || end == string_view::npos || end <= start )
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

                const ReflectAnnotationDesc& desc     = annotationConstants::kReflectContainer;
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

            static void fillContainerArgs( ParsedContainerNode& node, const string& typeSpelling, const CXType type,
                                           const ParserSession& session )
            {
                const vector<string> listArg      = extractTemplateArgs( typeSpelling );
                const int32          numClangArgs = ( type.kind == CXType_Invalid ) ? 0 : clang_Type_getNumTemplateArguments( type );

                if ( node._containerKind == ContainerKind::Map )
                {
                    if ( listArg.size() >= 2 )
                    {
                        node._keyTypeName     = session._typeNameMap.normalize( listArg[0] );
                        node._elementTypeName = session._typeNameMap.normalize( listArg[1] );
                        node._elementNested   = makeNestedContainer( type, numClangArgs, 1, listArg[1], session );
                    }
                }
                else if ( listArg.empty() == false )
                {
                    node._elementTypeName = session._typeNameMap.normalize( listArg[0] );
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
             * @details 예전에는 적힌 이름만 봐서 `using ScoreList = sw::vector<int32>;` · `using Health = int32;` 로 적은 프로퍼티가 모르는 타입 이름
             *          (`ScoreList` · `Health`)으로 남았다 — 컨테이너로도 스칼라로도 읽히지 않아 직렬화가 그 값을 쓰지 못했다(JSON 에는 `"null"`).
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
                    const CXType  named = current.kind == CXType_Elaborated ? clang_Type_getNamedType( current ) : current;
                    if ( session._containerTypeMap.match( spelling ) != nullptr || findReflectContainer( current, session._config, kind, wrapperStem ) ||
                         session._typeNameMap.isKnown( spelling ) || named.kind != CXType_Typedef )
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
            // D) REFLECT 타입의 멤버 — 베이스 · PROPERTY · FUNCTION · 생성자 · BODY/FACTORY 마커
            // ------------------------------------------------------------------------------
            /** @brief REFLECT 타입 하나의 멤버를 한 번의 자식 순회로 모으는 상태입니다. */
            struct MemberCollector
            {
                ParsedTypeInfo*        _pType;
                const ParserSession*   _pSession;
                uint32                 _baseCount;
                uint8                  _bSkipConstructors : 1; ///< Abstract / Static 타입은 생성할 수 없다(Unreal UCLASS(Abstract))
                uint8                  _bBodyFound        : 1;
                uint8                  _bFactoryFound     : 1;
                uint8                  _bHasError         : 1;
                [[maybe_unused]] uint8 _reserved          : 4;

                MemberCollector( ParsedTypeInfo& type, const ParserSession& session )
                    : _pType{ &type }
                    , _pSession{ &session }
                    , _baseCount{ 0 }
                    , _bSkipConstructors{ SW_FALSE }
                    , _bBodyFound{ SW_FALSE }
                    , _bFactoryFound{ SW_FALSE }
                    , _bHasError{ SW_FALSE }
                    , _reserved{ 0 }
                {
                    if ( type._bAbstract == SW_TRUE || type._bStatic == SW_TRUE )
                        _bSkipConstructors = SW_TRUE;
                }
            };

            /**
             * @brief 베이스 지정자(`: public X`)가 가리키는 **클래스** 선언입니다. 별칭(`using` · `typedef`)은 풀어 냅니다.
             * @details 적힌 타입의 선언을 바로 물으면 `using Base = Component; struct X : Base` 의 답은 별칭 선언이다. 예전에는 그래서 부모 FQN 이
             *          별칭 이름(`sw::Base`)이 되어 실행 중에 부모를 못 찾았고(상속 병합 · 캐스트가 사슬 중간에서 멈춘다), 컴포넌트 판별도 별칭에서
             *          멈춰 팩토리가 생기지 않았다. 정규 타입(canonical)은 별칭을 벗긴 레코드 타입이다.
             */
            static CXCursor getBaseClassDeclaration( const CXCursor baseSpecifier )
            {
                return clang_getTypeDeclaration( clang_getCanonicalType( clang_getCursorType( baseSpecifier ) ) );
            }

            /**
             * @brief 베이스 클래스 FQN을 부모로 기록합니다. ParsedTypeInfo 는 부모 하나만 담습니다 (단일 상속 체인).
             * @details PropertyInfo 의 오프셋 접근 · 캐스팅은 "리플렉션 부모는 항상 파생 객체의 byte offset 0 에 있다" 는
             *          전제로 동작합니다(비가상 첫 번째 베이스는 C++ ABI 가 offset 0 을 보장하지만, 두 번째 이후 베이스는 그렇지
             *          않습니다). 그래서 부모는 항상 선언 순서상 첫 번째 베이스로 고정합니다.
             *          두 번째 이후 베이스가 REFLECT() 없는 순수 인터페이스 · 믹스인(예: IFlagStore)이면 잃을 프로퍼티가 없으므로
             *          조용히 무시합니다. REFLECT() 가 붙은 베이스가 두 번째 이후에 있으면 프로퍼티가 유실되거나 조용히 오프셋이
             *          잘못될 수 있으므로, 경고가 아니라 빌드를 실패시키는 에러로 처리합니다. 그 베이스를 첫 번째로 옮기면
             *          해결됩니다.
             */
            static void collectBase( const CXCursor cursor, MemberCollector& collector )
            {
                const CXCursor baseDecl = getBaseClassDeclaration( cursor );
                const string   baseFQN  = ( clang_Cursor_isNull( baseDecl ) == 0 ) ? makeFullyQualifiedName( baseDecl ) : string{};

                ++collector._baseCount;
                if ( collector._baseCount == 1 )
                {
                    collector._pType->_parentFQN = baseFQN;
                    return;
                }

                // 두 번째 이후 베이스다. REFLECT() 가 없으면 잃을 프로퍼티가 없으므로 조용히 넘어간다.
                // clang_getTypeDeclaration 이 반환하는 커서는 AnnotateAttr 자식 순회에서 빠지는 경우가 있어
                // (다른 어노테이션 검사와 마찬가지로) 소스 텍스트 폴백까지 함께 검사한다.
                if ( clang_Cursor_isNull( baseDecl ) != 0 )
                    return;
                const CXCursor baseDefinition = clang_getCursorDefinition( baseDecl );
                const CXCursor searchCursor   = clang_Cursor_isNull( baseDefinition ) == 0 ? baseDefinition : baseDecl;
                if ( hasAnnotation( searchCursor, annotationConstants::kReflect, collector._pSession->_config ) == false )
                    return;

                SW_LOG_ERROR( "ERROR: %# has multiple base classes; '%#' has REFLECT() but is not the first base. "
                              "Its properties would not be inherited and its member offsets would be unsafe to access. "
                              "Declare '%#' as the first base instead (only the first base may be REFLECT()).",
                              collector._pType->_fullyQualifiedName, baseFQN, baseFQN );
                collector._bHasError = SW_TRUE;
            }

            /** @brief 필드 선언에서 PROPERTY 메타를 수집합니다. */
            static void collectField( const CXCursor cursor, MemberCollector& collector )
            {
                const string spelling = findAnnotateAttr( cursor, annotationConstants::kPropertyPrefix );
                if ( spelling.empty() && hasSourceAnnotation( cursor, annotationConstants::kProperty, collector._pSession->_config ) == false )
                    return;

                const ParserSession& session   = *collector._pSession;
                const CXType         fieldType = resolvePropertyAlias( clang_getCursorType( cursor ), session ); // 사용자 별칭은 벗긴다
                ParsedPropertyInfo   prop;
                prop._memberName = getCursorSpelling( cursor );
                prop._name       = prop._memberName;
                prop._typeName   = session._typeNameMap.normalize( takeString( clang_getTypeSpelling( fieldType ) ) );
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

                    // 자리는 재지 않는다 — 런타임이 그 구성의 레이아웃에서 찾는다(`PropertyInfo::resolveBitField`).
                    prop._bIsBitField = SW_TRUE;
                }

                const string owner = makeMemberOwnerName( collector._pType->_fullyQualifiedName, prop._memberName );
                if ( applyAnnotation( spelling, prop, session, owner ) == false )
                {
                    collector._bHasError = SW_TRUE;
                    return;
                }
                fillContainerDetails( prop, fieldType, session );
                collector._pType->_listProperty.push_back( std::move( prop ) );
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
                // libclang 에는 없다(2026-09-29 CI 가 잡았다).
                prop._typeName = session._typeNameMap.normalize( takeString( clang_getTypeSpelling( valueType ) ) );

                if ( applyAnnotation( spelling, prop, session, owner ) == false )
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
                collector._pType->_listProperty.push_back( std::move( prop ) );
            }

            /** @brief 메서드 · 생성자에 붙은 애노테이션 넷을 한 번의 자식 순회로 봅니다. */
            struct MethodAnnotation
            {
                string                 _functionSpelling; ///< FUNCTION(...) 철자. 없으면 비어 있다
                string                 _propertySpelling; ///< PROPERTY(...) 철자. 값 참조를 돌려주는 메서드면 접근자 프로퍼티다
                uint8                  _bBody    : 1;     ///< REFLECT_BODY
                uint8                  _bFactory : 1;     ///< COMPONENT_FACTORY
                [[maybe_unused]] uint8 _reserved : 6;

                MethodAnnotation()
                    : _functionSpelling{}
                    , _propertySpelling{}
                    , _bBody{ SW_FALSE }
                    , _bFactory{ SW_FALSE }
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
                    if ( spelling.find( annotationConstants::kReflectBodyPrefix ) != string_view::npos )
                        pAnnotation->_bBody = SW_TRUE;
                    if ( spelling.find( annotationConstants::kComponentFactoryPrefix ) != string_view::npos )
                        pAnnotation->_bFactory = SW_TRUE;
                    if ( pAnnotation->_functionSpelling.empty() && spelling.find( annotationConstants::kFunctionPrefix ) != string_view::npos )
                        pAnnotation->_functionSpelling = pText;
                    if ( pAnnotation->_propertySpelling.empty() && spelling.find( annotationConstants::kPropertyPrefix ) != string_view::npos )
                        pAnnotation->_propertySpelling = pText;
                }
                clang_disposeString( cxSpelling );
                return CXChildVisit_Continue;
            }

            /** @brief 인자 타입들을 정규 이름으로 채웁니다. */
            static void fillParameterTypes( const CXCursor cursor, ParsedFunctionInfo& method, const ParserSession& session )
            {
                const int32 numArgs = clang_Cursor_getNumArguments( cursor );
                for ( int32 argIndex = 0; argIndex < numArgs; ++argIndex )
                {
                    const CXCursor argCursor = clang_Cursor_getArgument( cursor, static_cast<uint32>( argIndex ) );
                    method._listParameterTypeName.push_back(
                        session._typeNameMap.normalize( takeString( clang_getTypeSpelling( clang_getCursorType( argCursor ) ) ) ) );
                }
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
                method._name           = annotationConstants::kCtorLookupName;
                method._returnTypeName = annotationConstants::kVoidTypeName;
                method._bConstructor   = SW_TRUE;
                method._category       = annotationConstants::kConstructorCategory;
                fillParameterTypes( cursor, method, *collector._pSession );

                const string owner = makeMemberOwnerName( collector._pType->_fullyQualifiedName, annotationConstants::kCtorLookupName );
                if ( functionSpelling.empty() == false && applyAnnotation( functionSpelling, method, *collector._pSession, owner ) == false )
                {
                    collector._bHasError = SW_TRUE;
                    return;
                }
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
                    if ( hasSourceAnnotation( cursor, annotationConstants::kFunction, collector._pSession->_config ) == false )
                        return;
                }

                const ParserSession& session = *collector._pSession;
                ParsedFunctionInfo   method;
                method._name           = getCursorSpelling( cursor );
                method._returnTypeName = session._typeNameMap.normalize( takeString( clang_getTypeSpelling( clang_getCursorResultType( cursor ) ) ) );
                method._bStatic        = clang_CXXMethod_isStatic( cursor ) != 0 ? SW_TRUE : SW_FALSE;
                method._bConst         = clang_CXXMethod_isConst( cursor ) != 0 ? SW_TRUE : SW_FALSE;
                fillParameterTypes( cursor, method, session );

                const string owner = makeMemberOwnerName( collector._pType->_fullyQualifiedName, method._name );
                if ( bHasAttr && applyAnnotation( functionSpelling, method, session, owner ) == false )
                {
                    collector._bHasError = SW_TRUE;
                    return;
                }
                collector._pType->_listMethod.push_back( std::move( method ) );
            }

            /** @brief 함수 꼴 멤버 하나 — 마커 · 생성자 · 메서드로 나눕니다. */
            static void collectFunctionMember( const CXCursor cursor, const CXCursorKind kind, MemberCollector& collector )
            {
                // REFLECT_BODY / COMPONENT_FACTORY 마커 함수다. 리플렉트 FUNCTION 이 아니다.
                const string spelling = getCursorSpelling( cursor );
                if ( spelling == annotationConstants::kReflectBodyMarkerFn )
                {
                    collector._bBodyFound = SW_TRUE;
                    return;
                }
                if ( spelling == annotationConstants::kComponentFactoryMarkerFn )
                {
                    collector._bFactoryFound = SW_TRUE;
                    return;
                }

                MethodAnnotation annotation;
                clang_visitChildren( cursor, methodAnnotationVisitor, &annotation );
                if ( annotation._bBody == SW_TRUE )
                    collector._bBodyFound = SW_TRUE;
                if ( annotation._bFactory == SW_TRUE )
                    collector._bFactoryFound = SW_TRUE;

                if ( kind == CXCursor_Constructor )
                {
                    collectConstructor( cursor, annotation._functionSpelling, collector );
                    return;
                }
                if ( kind == CXCursor_CXXMethod )
                {
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
                if ( kind == CXCursor_CXXBaseSpecifier )
                    collectBase( cursor, collector );
                else if ( kind == CXCursor_FieldDecl )
                    collectField( cursor, collector );
                else if ( bFunction )
                    collectFunctionMember( cursor, kind, collector );
                return CXChildVisit_Continue;
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
                // -56 이 됐다(생성 코드는 컴파일러 식으로 내므로 맞지만, `--dump` 와 파서 안의 판단이 이 값을 본다).
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
                if ( hasAnnotation( parent, annotationConstants::kReflect, config ) )
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
            _listTargetFile.push_back( clang_getFile( translationUnit, path.c_str() ) );
        _listHeader.resize( _listTargetPath.size() );
    }

    // ------------------------------------------------------------------------------
    // G) AstVisitor — visit / onStructDecl / onEnumDecl
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
     * @details 오류는 헤더 단위로 남긴다. 예전에는 검증 오류가 순회 전체를 멈췄는데(`CXChildVisit_Break`), 헤더 여럿을 한 TU 로
     *          묶으면 한 헤더의 오타가 나머지 헤더의 수집까지 막는다. 이제 그 헤더만 실패로 두고 계속 돌아, 오류도 헤더마다 다 나온다.
     */
    void AstVisitor::markHeaderError( ParsedHeader& header )
    {
        header._bHasError = SW_TRUE;
        _bHasError        = SW_TRUE;
    }

    /**
     * @details 매크로가 만든 선언은 매크로를 **쓴** 자리(전개 위치)의 파일로 셉니다. 예전에는 `clang_Location_isFromMainFile`
     *          로 물어 매크로 위치를 통째로 밖으로 쳤는데, 그러면 `REFLECT_BODY()` 가 만드는 마커 함수가 한 번도 검사되지 않아
     *          "REFLECT_BODY() 인데 REFLECT() 가 없다" 는 검사가 죽어 있었습니다. 여러 헤더를 한 TU 로 묶을 때도 같은 질문이면 됩니다.
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
            const bool bOrphanProperty = AstVisitorInternal::hasAnnotation( cursor, annotationConstants::kProperty, config ) &&
                                         AstVisitorInternal::isInsideReflectType( cursor, annotationConstants::kPropertyMacro, config ) == false;
            if ( bOrphanProperty )
                self->markHeaderError( header );
            return CXChildVisit_Continue;
        }

        if ( kind == CXCursor_CXXMethod )
        {
            const bool bHasFunction = AstVisitorInternal::hasAnnotation( cursor, annotationConstants::kFunction, config );
            // 메서드의 PROPERTY 는 실제 애노테이션만 본다(값 참조 접근자). 소스 창 휴리스틱은 필드 쪽 것이다.
            const bool bHasProperty = AstVisitorInternal::findAnnotateAttr( cursor, annotationConstants::kPropertyPrefix ).empty() == false;
            const bool bHasBody =
                AstVisitorInternal::findAnnotateAttr( cursor, annotationConstants::kReflectBodyPrefix ).empty() == false ||
                AstVisitorInternal::isStringEqual( clang_getCursorSpelling( cursor ), annotationConstants::kReflectBodyMarkerFn );
            const utf8* pMacroName   = bHasFunction ? annotationConstants::kFunctionMacro
                                                    : ( bHasProperty ? annotationConstants::kPropertyMacro : annotationConstants::kReflectBodyPrefix );
            const bool  bOrphanMacro = ( bHasFunction || bHasProperty || bHasBody ) && AstVisitorInternal::isInsideReflectType( cursor, pMacroName, config ) == false;
            if ( bOrphanMacro )
                self->markHeaderError( header );
            return CXChildVisit_Continue;
        }

        // 클래스 템플릿은 FQN 에 인자가 없어 offsetof / TypeRegistrar<T> 가 성립하지 않는다.
        // 조용히 빠지면 원인을 찾기 어려우므로 경고만 남기고 건너뛴다.
        if ( kind == CXCursor_ClassTemplate || kind == CXCursor_ClassTemplatePartialSpecialization )
        {
            if ( AstVisitorInternal::hasAnnotation( cursor, annotationConstants::kReflect, config ) )
            {
                SW_LOG_WARNING( "REFLECT on class template is not supported, skipping: %#",
                                AstVisitorInternal::makeFullyQualifiedName( cursor ) );
            }
            return CXChildVisit_Continue;
        }

        if ( kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl )
        {
            if ( AstVisitorInternal::hasAnnotation( cursor, annotationConstants::kReflect, config ) )
                self->onStructDeclaration( cursor, header );
            return CXChildVisit_Recurse;
        }

        if ( kind == CXCursor_EnumDecl && AstVisitorInternal::hasAnnotation( cursor, annotationConstants::kEnum, config ) )
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
     * 4. `REFLECT_BODY()` · `COMPONENT_FACTORY()` 매크로가 있는지 확인
     * 5. 멤버 변수(`PROPERTY`)와 멤버 함수(`FUNCTION`) 메타데이터 수집
     */
    void AstVisitor::onStructDeclaration( const CXCursor cursor, ParsedHeader& outHeader )
    {
        ParsedTypeInfo typeInfo;
        typeInfo._name               = AstVisitorInternal::getCursorSpelling( cursor );
        typeInfo._fullyQualifiedName = AstVisitorInternal::makeFullyQualifiedName( cursor );

        BLOCK( "Parse REFLECT Annotation" )
        {
            const string spelling = AstVisitorInternal::readAnnotation( cursor, annotationConstants::kReflect, _pSession->_config );
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
            if ( collector._bHasError == SW_TRUE )
            {
                markHeaderError( outHeader );
                return;
            }
            // 추상 타입(REFLECT(Abstract) · C++ 추상)은 팩토리를 내지 않는다 — 팩토리는 `addComponent<T>()` 로 T 를 만든다. 공통 기반
            // 컴포넌트(`LightComponent`)가 첫 예다. 예전에는 컴포넌트에서 파생했으면 무조건 내 그런 기반을 둘 수 없었다.
            const bool bRequiresFactory = collector._bFactoryFound == SW_TRUE || AstVisitorInternal::isDerivedFromComponent( cursor, _pSession->_config );
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
            // (`CodeGenerator::emitGeneratedHeader`). 그래서 기반 정수 타입이 필요하다. 정본 철자로
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
            string spelling = AstVisitorInternal::extractSourceAnnotation( cursor, annotationConstants::kEnum, _pSession->_config );
            if ( spelling.empty() )
                spelling = AstVisitorInternal::findAnnotateAttr( cursor, annotationConstants::kEnumPrefix );
            if ( spelling.empty() == false &&
                 AstVisitorInternal::applyAnnotation( spelling, enumInfo, *_pSession, enumInfo._fullyQualifiedName ) == false )
            {
                markHeaderError( outHeader );
                return;
            }
            if ( enumInfo._countEnumerator.empty() == false && enumInfo._invalidEnumerator.empty() )
                enumInfo._invalidEnumerator = enumInfo._countEnumerator;
        }

        // **비트플래그인지는 선언이 정한다. 값의 모양이 아니다.**
        //
        // 처음부터(초기 커밋) "0 이 아닌 값이 모두 2의 거듭제곱이면 BitFlag" 라는 자동 감지가
        // 있었는데, 그 조건은 `{ Game = 0, Editor = 1, Custom = 2 }` 같은 **평범한 연속 열거형**
        // 에도 그대로 맞는다. 실제로 `CameraRole` · `PackEncryptionType` · `SampleStatus` 셋이
        // 비트플래그로 등록돼 있었고, 그러면 문자열 변환이 `toStringFlags` 로 가고 인스펙터가
        // 콤보 대신 체크박스를 그린다.
        //
        // 더 얄궂은 것은 **같은 파싱이 같은 질문에 두 답을 냈다는 점**이다. C++ 트레이트
        // (`IsBitFlagEnum<>`)는 명시한 `ENUM( Flags )` 로만 나가므로 그 셋에는 없었다.
        // 등록부는 "플래그다", 트레이트는 "아니다" 였다.
        //
        // 그래서 자동 감지를 없앤다. 비트플래그 열거형은 `ENUM( Flags )` 로 **명시한다.**
        // 명시하지 않아 놓친 쪽은 비트 연산자가 없어 컴파일이 그 자리에서 막히지만, 명시하지
        // 않았는데 켜지는 쪽은 조용히 틀린다.

        SW_LOG_INFO( "ENUM          : %#  (%# values, _bIsBitFlag=%# aliases=%#)",
                     enumInfo._fullyQualifiedName, enumInfo._listEnumerator.size(), enumInfo._bIsBitFlag ? "true" : "false",
                     enumInfo._listAlias.size() );
        outHeader._listEnum.push_back( std::move( enumInfo ) );
    }
} // namespace sw
