#include "pch.h"

#include "Programs/ReflectionParser/ContainerTreeBuilder.h"

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
    vector<string> ContainerTreeBuilder::extractTemplateArgs( const string_view typeStr )
    {
        const size_t start              = typeStr.find( '<' );
        const size_t end                = typeStr.rfind( '>' );
        const bool   bInvalidAngleRange = start == string_view::npos || end == string_view::npos || end <= start;
        if ( bInvalidAngleRange )
            return {};
        return ParserUtil::splitCommaRespectingAngles( typeStr.substr( start + 1, end - start - 1 ) );
    }

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

    bool ContainerTreeBuilder::findReflectContainer( const CXType type, const ParserConfig& config, ContainerKind& outKind, string& outWrapperStem )
    {
        const CXCursor declaration = findContainerDeclaration( type );
        if ( clang_Cursor_isNull( declaration ) )
            return false;

        const ReflectAnnotationDesc& desc     = annotation::kReflectContainer;
        const string                 spelling = AnnotationLookup::readAnnotation( declaration, desc, config );
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

    void ContainerTreeBuilder::markContainer( ParsedContainerNode& node, const ContainerKind kind, const string& wrapperStem )
    {
        node._bIsContainer  = SW_TRUE;
        node._containerKind = kind;
        node._containerType = wrapperStem;
    }

    shared_ptr<ParsedContainerNode> ContainerTreeBuilder::makeNestedContainer( const CXType type, const int32 numClangArgs, const int32 index, const string& spellingFallback, const ParserSession& session )
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

    bool ContainerTreeBuilder::tryResolveAliasSpelling( const CXType type, const ParserSession& session, string& outSpelling )
    {
        const CXType resolved = resolvePropertyAlias( type, session );
        const bool   bChanged = clang_equalTypes( type, resolved ) == 0;
        outSpelling           = CursorReader::takeString( clang_getTypeSpelling( resolved ) );
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

    string ContainerTreeBuilder::resolveTemplateArgName( const CXType type, const int32 numClangArgs, const int32 index, const string& spelling, const ParserSession& session )
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

    void ContainerTreeBuilder::fillContainerArgs( ParsedContainerNode& node, const string& typeSpelling, const CXType type, const ParserSession& session )
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

    shared_ptr<ParsedContainerNode> ContainerTreeBuilder::makeContainerFromSpelling( const string& typeSpelling, const ParserSession& session )
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

    shared_ptr<ParsedContainerNode> ContainerTreeBuilder::makeContainerFromType( const CXType type, const ParserSession& session )
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

    CXType ContainerTreeBuilder::resolvePropertyAlias( const CXType type, const ParserSession& session )
    {
        CXType resolved = type; // 반환은 이 하나다(-Wnrvo)
        CXType current  = type;
        for ( int32 depth = 0; depth < 8 && current.kind != CXType_Invalid; ++depth )
        {
            const string  spelling = CursorReader::takeString( clang_getTypeSpelling( current ) );
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

    void ContainerTreeBuilder::fillContainerDetails( ParsedPropertyInfo& prop, const CXType fieldType, const ParserSession& session )
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
} // namespace sw
