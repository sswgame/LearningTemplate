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
    CXCursor ContainerTreeBuilder::findContainerDeclaration( const CXType type )
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

    bool ContainerTreeBuilder::findReflectContainer( const CXType type, ContainerKind& outKind, string& outWrapperStem )
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

    void ContainerTreeBuilder::markContainer( ParsedContainerNode& node, const ContainerKind kind, const string& wrapperStem, const ParserSession& session )
    {
        node._bIsContainer  = SW_TRUE;
        node._containerKind = kind;
        node._containerType = wrapperStem;
        // 제자리 쓰기 시퀀스인지는 래퍼 줄기의 표 역할(State)이다 — 같은 줄기의 줄은 역할이 같고, 생성 코드가 래퍼 형식과 컴파일 때 대조한다(`matchesTable`).
        const ContainerTypeRule* pRule = session._containerTypeMap.findByWrapperStem( wrapperStem );
        node._bInPlaceElementWrite     = pRule != nullptr && pRule->_bState == SW_TRUE ? SW_TRUE : SW_FALSE;
    }

    shared_ptr<ParsedContainerNode> ContainerTreeBuilder::makeNestedContainer( const CXType argType, const ParserSession& session )
    {
        // 원소 · 값 형식도 사용자 별칭을 벗긴 뒤 본다 — `vector<ScoreList>` 의 원소 이름은 벗겨 쓰면서 중첩은 놓치면 이름과 트리가 어긋난다.
        shared_ptr<ParsedContainerNode> nested = makeContainerFromType( resolvePropertyAlias( argType, session ), session );
        if ( nested->_bIsContainer == SW_FALSE && nested->_bInvalid == SW_FALSE )
            nested.reset();
        return nested;
    }

    bool ContainerTreeBuilder::tryResolveAliasSpelling( const CXType type, const ParserSession& session, string& outSpelling )
    {
        const CXType resolved = resolvePropertyAlias( type, session );
        const bool   bChanged = clang_equalTypes( type, resolved ) == 0;
        outSpelling           = CursorReader::takeString( clang_getTypeSpelling( resolved ) );
        // 컨테이너의 인자만 다시 짠다. 알려진 이름(`string` 은 `basic_string<utf8>`)의 인자를 벗기면 `string<char>` 같은 모르는 이름이 된다.
        ContainerKind kind = ContainerKind::None;
        string        wrapperStem;
        const bool    bContainer = session._containerTypeMap.match( outSpelling ) != nullptr || findReflectContainer( resolved, kind, wrapperStem );
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

    string ContainerTreeBuilder::resolveTemplateArgName( const CXType argType, const ParserSession& session )
    {
        string spelling;
        (void)tryResolveAliasSpelling( argType, session, spelling ); // 벗겼든 아니든 clang 의 철자를 쓴다
        return session._typeNameMap.normalize( spelling );
    }

    void ContainerTreeBuilder::fillContainerArgs( ParsedContainerNode& node, const CXType type, const ParserSession& session )
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

    shared_ptr<ParsedContainerNode> ContainerTreeBuilder::makeContainerFromType( const CXType type, const ParserSession& session )
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
            const bool    bResolvedType = session._containerTypeMap.match( spelling ) != nullptr || findReflectContainer( current, kind, wrapperStem ) ||
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

    bool ContainerTreeBuilder::isTreeValid( const ParsedContainerNode& node )
    {
        if ( node._bInvalid == SW_TRUE )
            return false;
        return node._elementNested == nullptr || isTreeValid( *node._elementNested );
    }

    bool ContainerTreeBuilder::fillContainerDetails( ParsedPropertyInfo& prop, const CXType fieldType, const ParserSession& session )
    {
        prop._containerTree = makeContainerFromType( fieldType, session );
        if ( isTreeValid( *prop._containerTree ) == false )
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
