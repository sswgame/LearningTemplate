#include "pch.h"

#include "Programs/ReflectionParser/ContainerTreeBuilder.h"

#include "Core/String/StringBuilder.h"

#include "Programs/ReflectionParser/AnnotationApply.h"
#include "Programs/ReflectionParser/AnnotationLookup.h"
#include "Programs/ReflectionParser/ContainerTypeMap.h"
#include "Programs/ReflectionParser/CursorReader.h"
#include "Programs/ReflectionParser/ParserConfig.h"
#include "Programs/ReflectionParser/ParserDefines.h"
#include "Programs/ReflectionParser/ParserUtil.h"
#include "Programs/ReflectionParser/TypeNameMap.h"

namespace sw
{
    namespace
    {
        struct ContainerTreeBuilderInternal
        {
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
            [[nodiscard]] static bool findReflectContainer( const CXType type, ContainerKind& outKind, string& outWrapperStem )
            {
                const CXCursor declaration = findContainerDeclaration( type );
                if ( clang_Cursor_isNull( declaration ) )
                    return false;

                const ReflectAnnotationDesc& desc     = annotation::kReflectContainer;
                const string                 spelling = AnnotationLookup::findPreviousSiblingMarker( declaration, desc );
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
            static void markContainer( ParsedContainerNode& node, const ContainerKind kind, const string& wrapperStem, const ParserSession& session )
            {
                node._bIsContainer  = SW_TRUE;
                node._containerKind = kind;
                node._containerType = wrapperStem;
                // 제자리 쓰기 시퀀스인지는 래퍼 줄기의 표 역할(State)이다 — 같은 줄기의 줄은 역할이 같고, 생성 코드가 래퍼 형식과 컴파일 때 대조한다(`matchesTable`).
                const ContainerTypeRule* pRule = session._containerTypeMap.findByWrapperStem( wrapperStem );
                node._bInPlaceElementWrite     = pRule != nullptr && pRule->_bState == SW_TRUE ? SW_TRUE : SW_FALSE;
            }

            /** @brief 원소 · 값 인자 형식이 컨테이너면 그 중첩 노드, 아니면 nullptr 입니다(인자를 읽지 못한 노드는 그대로 돌려 오류가 트리에 남는다). */
            static shared_ptr<ParsedContainerNode> makeNestedContainer( const CXType argType, const ParserSession& session )
            {
                // 원소 · 값 형식도 사용자 별칭을 벗긴 뒤 본다 — `vector<ScoreList>` 의 원소 이름은 벗겨 쓰면서 중첩은 놓치면 이름과 트리가 어긋난다.
                shared_ptr<ParsedContainerNode> nested = makeContainerFromType( ContainerTreeBuilder::resolvePropertyAlias( argType, session ), session );
                if ( nested->_bIsContainer == SW_FALSE && nested->_bInvalid == SW_FALSE )
                    nested.reset();
                return nested;
            }

            /**
             * @brief 템플릿 인자 하나(원소 · 키)의 타입 이름입니다. 인자 안의 사용자 별칭을 벗깁니다(`tryResolveAliasSpelling`).
             * @details 적힌 이름만 쓰면 `vector<AccountID>` 의 원소가 모르는 이름 `AccountID` 로 등록되고, 자기 기술형 바이너리가 그 원소를 읽지 못합니다.
             *          별칭이 없는 인자는 clang 의 철자 그대로입니다.
             */
            static string resolveTemplateArgName( const CXType argType, const ParserSession& session )
            {
                string spelling;
                (void)ContainerTreeBuilder::tryResolveAliasSpelling( argType, session, spelling ); // 벗겼든 아니든 clang 의 철자를 쓴다
                return session._typeNameMap.normalize( spelling );
            }

            /** @brief 맵 · 시퀀스의 키 · 원소 · 중첩 노드를 clang 형식 인자로 채웁니다. 인자를 읽지 못하면 알리고 노드를 무효로 둡니다. */
            static void fillContainerArgs( ParsedContainerNode& node, const CXType type, const ParserSession& session )
            {
                // 인자는 clang 형식에서만 읽는다 — 형식 철자를 쪼개 읽던 폴백은 인자를 못 얻은 자리를 조용히 다른 길로 넘겼다.
                const uint32 requiredCount = node._containerKind == ContainerKind::Map ? 2u : 1u;
                const int32  numClangArgs  = clang_Type_getNumTemplateArguments( type );
                CXType       arrArgType[2] = {};
                bool         bArgsValid    = numClangArgs >= static_cast<int32>( requiredCount );
                for ( uint32 index = 0; bArgsValid && index < requiredCount; ++index )
                {
                    arrArgType[index] = clang_Type_getTemplateArgumentAsType( type, index );
                    bArgsValid        = arrArgType[index].kind != CXType_Invalid;
                }
                if ( bArgsValid == false )
                {
                    SW_LOG_ERROR_ID( "ContainerTreeBuilder.ERRORContainerArguments", "ERROR: container type '%#' (%#) needs %# type argument(s) the parser can read - %# has %#.",
                                     CursorReader::takeString( clang_getTypeSpelling( type ) ), toString( node._containerKind ), requiredCount, node._containerType, numClangArgs );
                    node._bInvalid = SW_TRUE;
                    return;
                }

                const CXType elementType = arrArgType[requiredCount - 1];
                if ( node._containerKind == ContainerKind::Map )
                    node._keyTypeName = resolveTemplateArgName( arrArgType[0], session );
                node._elementTypeName = resolveTemplateArgName( elementType, session );
                node._elementNested   = makeNestedContainer( elementType, session );
            }

            /** @brief 트리 어디에도 무효 노드가 없으면 true 입니다. */
            static bool isTreeValid( const ParsedContainerNode& node )
            {
                if ( node._bInvalid == SW_TRUE )
                    return false;
                return node._elementNested == nullptr || isTreeValid( *node._elementNested );
            }

            /**
             * @brief clang 타입으로 컨테이너 트리를 만듭니다.
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
                    // 표의 `array` 줄이 래퍼를 정한다 — 로더가 그 줄이 고정 시퀀스로 있는지 시작할 때 봤다.
                    const ContainerTypeRule& fixedArray = *session._containerTypeMap.findByMatch( annotation::kFixedArrayTypeName );
                    markContainer( *node, fixedArray._kind, fixedArray._type, session );
                    node->_typeName        = fixedArray._match;
                    node->_elementTypeName = session._typeNameMap.normalize( CursorReader::takeString( clang_getTypeSpelling( elementType ) ) );
                    node->_elementNested   = makeContainerFromType( elementType, session );
                    if ( node->_elementNested != nullptr && node->_elementNested->_bIsContainer == SW_FALSE )
                        node->_elementNested.reset();
                    return node;
                }

                const string             spelling = CursorReader::takeString( clang_getTypeSpelling( type ) );
                const ContainerTypeRule* rule     = session._containerTypeMap.match( spelling );

                ContainerKind kind = ContainerKind::None;
                string        wrapperStem;
                if ( findReflectContainer( type, kind, wrapperStem ) )
                    markContainer( *node, kind, wrapperStem, session );
                else if ( rule != nullptr )
                    markContainer( *node, rule->_kind, rule->_type, session );
                else
                    return node;

                if ( rule != nullptr )
                    node->_typeName = rule->_match;
                fillContainerArgs( *node, type, session );
                return node;
            }
        };
    } // namespace

    bool ContainerTreeBuilder::tryResolveAliasSpelling( const CXType type, const ParserSession& session, string& outSpelling )
    {
        const CXType resolved = resolvePropertyAlias( type, session );
        const bool   bChanged = clang_equalTypes( type, resolved ) == 0;
        outSpelling           = CursorReader::takeString( clang_getTypeSpelling( resolved ) );
        // 컨테이너의 인자만 다시 짠다. 알려진 이름(`string` 은 `basic_string<utf8>`)의 인자를 벗기면 `string<char>` 같은 모르는 이름이 된다.
        ContainerKind kind = ContainerKind::None;
        string        wrapperStem;
        const bool    bContainer = session._containerTypeMap.match( outSpelling ) != nullptr || ContainerTreeBuilderInternal::findReflectContainer( resolved, kind, wrapperStem );
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

        StringBuilder<constant::buffer::kMax256> rebuilt;
        rebuilt.append( string_view( outSpelling ).substr( 0, outSpelling.find( '<' ) ) );
        rebuilt.append( '<' );
        for ( size_t argIndex = 0; argIndex < listArgSpelling.size(); ++argIndex )
        {
            if ( argIndex > 0 )
                rebuilt.append( ", " );
            rebuilt.append( listArgSpelling[argIndex] );
        }
        rebuilt.append( '>' );
        outSpelling = string( rebuilt.view() );
        return true;
    }

    CXType ContainerTreeBuilder::resolvePropertyAlias( const CXType type, const ParserSession& session )
    {
        // 별칭 사슬은 순환할 수 없어(typedef 는 앞선 형식만 가리킨다) 끝이 있다 — 깊이 상한을 두면 그 너머에서 조용히 멈췄다.
        CXType resolved = type; // 반환은 이 하나다(-Wnrvo)
        CXType current  = type;
        while ( current.kind != CXType_Invalid )
        {
            const string  spelling = CursorReader::takeString( clang_getTypeSpelling( current ) );
            ContainerKind kind     = ContainerKind::None;
            string        wrapperStem;
            const CXType  named         = current.kind == CXType_Elaborated ? clang_Type_getNamedType( current ) : current;
            const bool    bResolvedType = session._containerTypeMap.match( spelling ) != nullptr || ContainerTreeBuilderInternal::findReflectContainer( current, kind, wrapperStem ) ||
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

    bool ContainerTreeBuilder::fillContainerDetails( ParsedPropertyInfo& prop, const CXType fieldType, const ParserSession& session )
    {
        prop._containerTree = ContainerTreeBuilderInternal::makeContainerFromType( fieldType, session );
        if ( ContainerTreeBuilderInternal::isTreeValid( *prop._containerTree ) == false )
            return false;
        if ( prop._containerTree->_bIsContainer == SW_FALSE )
            return true;

        prop._bIsContainer    = SW_TRUE;
        prop._containerKind   = prop._containerTree->_containerKind;
        prop._containerType   = prop._containerTree->_containerType;
        prop._elementTypeName = prop._containerTree->_elementTypeName;
        prop._keyTypeName     = prop._containerTree->_keyTypeName;
        return true;
    }
} // namespace sw
