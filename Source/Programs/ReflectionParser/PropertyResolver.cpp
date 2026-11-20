#include "pch.h"

#include "Programs/ReflectionParser/PropertyResolver.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Programs/ReflectionParser/CursorReader.h"
#include "Programs/ReflectionParser/ParserDefines.h"
#include "Programs/ReflectionParser/ParserSession.h"

#include <charconv>
#include <limits>

namespace sw
{
    string PropertyResolver::readTypedTarget( const CXType type, const string_view identity, const ParserSession& session )
    {
        if ( CursorReader::getTypeIdentity( type ) != identity )
            return {};
        return session._typeNameMap.normalize( CursorReader::getTemplateArgumentSpelling( type, 0 ) );
    }

    CXType PropertyResolver::getSequenceElementType( const CXType valueType )
    {
        const CXType canonical = clang_getCanonicalType( valueType );
        if ( canonical.kind == CXType_ConstantArray )
            return clang_getArrayElementType( valueType );
        return clang_Type_getTemplateArgumentAsType( valueType, 0 );
    }

    bool PropertyResolver::isFlatOrderedSequence( const ParsedPropertyInfo& prop )
    {
        // 집합(`set` · `unordered_set`)은 원소의 순서 · 해시가 있어야 컴파일되는데 핸들 · 참조 · 키에는 둘 다 없다 — 생성 단계에서 막아 C++ 오류 대신 이 오류가 뜨게 한다.
        // 집합은 표에서 State 역할이 없는 시퀀스다(래퍼 형식과 컴파일 때 대조된다) — 철자("Set")로 가르지 않는다.
        const ParsedContainerNode* pTree = prop._containerTree.get();
        const bool                 bFlat = pTree != nullptr && pTree->_elementNested == nullptr;
        return prop._bIsContainer == SW_TRUE && prop._containerKind == ContainerKind::Sequence && prop._keyTypeName.empty() && bFlat && pTree->_bInPlaceElementWrite == SW_TRUE;
    }

    void PropertyResolver::normalizeTypedValue( ParsedPropertyInfo& prop, const bool bSingleValue, const string& canonical )
    {
        if ( bSingleValue )
        {
            prop._typeName = canonical;
            return;
        }
        const string typedSpelling = prop._elementTypeName;
        replaceAllText( prop._typeName, typedSpelling, canonical );
        prop._elementTypeName = canonical;
        if ( prop._containerTree != nullptr )
            prop._containerTree->_elementTypeName = canonical;
    }

    void PropertyResolver::replaceAllText( string& text, const string& from, const string& to )
    {
        for ( size_t pos = text.find( from ); pos != string::npos; pos = text.find( from, pos + to.size() ) )
        {
            text.replace( pos, from.size(), to );
        }
    }

    bool PropertyResolver::resolveComponentHandleProperty( ParsedPropertyInfo& prop, const CXType valueType, const ParserSession& session, const string& owner )
    {
        const bool bHolds = CursorReader::containsTypeIdentity( valueType, typeidentity::kTypedComponentHandle ) ||
                            CursorReader::containsTypeIdentity( valueType, typeidentity::kComponentHandle );
        if ( bHolds == false )
            return true;

        const bool   bSingleValue = prop._bIsContainer == SW_FALSE;
        const string target       = bSingleValue ? readTypedTarget( valueType, typeidentity::kTypedComponentHandle, session )
                                                 : ( isFlatOrderedSequence( prop ) ? readTypedTarget( getSequenceElementType( valueType ), typeidentity::kTypedComponentHandle, session ) : string{} );
        if ( target.empty() )
        {
            SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYHoldsComponent", "ERROR: PROPERTY() '%#' holds a component handle (%#) in a shape that cannot be saved. Use one TComponentHandle<T>, or a vector of "
                                                                       "TComponentHandle<T>. An untyped ComponentHandle, a map, a set and nested containers have no place to check the target type.",
                             owner, prop._typeName );
            return false;
        }
        normalizeTypedValue( prop, bSingleValue, "ComponentHandle" );
        prop._handleTargetTypeName = target;
        return true;
    }

    bool PropertyResolver::resolveAssetRefProperty( ParsedPropertyInfo& prop, const CXType valueType, const ParserSession& session, const string& owner )
    {
        if ( CursorReader::containsTypeIdentity( valueType, typeidentity::kAssetRef ) == false )
            return true;
        const bool   bSingleValue = prop._bIsContainer == SW_FALSE;
        const string target       = bSingleValue ? readTypedTarget( valueType, typeidentity::kAssetRef, session )
                                                 : ( isFlatOrderedSequence( prop ) ? readTypedTarget( getSequenceElementType( valueType ), typeidentity::kAssetRef, session ) : string{} );
        if ( target.empty() )
        {
            SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYHoldsAssetRef", "ERROR: PROPERTY() '%#' holds an asset reference (%#) in a shape tools cannot rewrite. Use one AssetRef<T>, or a vector of AssetRef<T>.", owner,
                             prop._typeName );
            return false;
        }
        normalizeTypedValue( prop, bSingleValue, "AssetRef" );
        prop._assetType  = target;
        prop._bAssetPath = SW_TRUE;
        return true;
    }

    bool PropertyResolver::resolveCatalogKeyProperty( ParsedPropertyInfo& prop, const CXType valueType, const ParserSession& session, const string& owner )
    {
        if ( CursorReader::containsTypeIdentity( valueType, typeidentity::kCatalogKey ) == false )
            return true;
        const bool   bSingleValue = prop._bIsContainer == SW_FALSE;
        const CXType keyType      = bSingleValue ? valueType : ( isFlatOrderedSequence( prop ) ? getSequenceElementType( valueType ) : CXType{} );
        const string target       = keyType.kind == CXType_Invalid ? string{} : readTypedTarget( keyType, typeidentity::kCatalogKey, session );
        if ( target.empty() )
        {
            SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYHoldsCatalogKey", "ERROR: PROPERTY() '%#' holds a catalog key (%#) in a shape the cook cannot check. Use one CatalogKey<T>, or a vector of CatalogKey<T>.",
                             owner, prop._typeName );
            return false;
        }
        normalizeTypedValue( prop, bSingleValue, "CatalogKey" );
        // 표의 `<CatalogKey tag>` 는 이름공간 없는 형식 이름이다 — 항목 종류 선언의 이름(중첩이면 바깥 형식부터)을 선언에서 읽는다.
        CXCursor tagDeclaration = clang_getTypeDeclaration( clang_getCanonicalType( clang_Type_getTemplateArgumentAsType( keyType, 0 ) ) );
        string   tag            = CursorReader::getCursorSpelling( tagDeclaration );
        for ( CXCursor parent = clang_getCursorSemanticParent( tagDeclaration ); clang_getCursorKind( parent ) != CXCursor_Namespace && clang_getCursorKind( parent ) != CXCursor_TranslationUnit;
              parent          = clang_getCursorSemanticParent( parent ) )
        {
            StringBuilder<constant::buffer::kMax256> nested;
            nested.appendFormat( "%#::%#", CursorReader::getCursorSpelling( parent ), tag );
            tag = string( nested.view() );
        }
        prop._catalogKeyTag = std::move( tag );
        return true;
    }

    bool PropertyResolver::resolveComponentHandleParameters( ParsedFunctionInfo& method, bool bComponentOwner, const string& owner )
    {
        for ( ParsedParameterInfo& parameter : method._listParameter )
        {
            if ( parameter._bHoldsTypedHandle == false )
                continue;
            const string& target = parameter._handleTargetTypeName;
            if ( target.empty() )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORFUNCTIONArgumentHolds", "ERROR: FUNCTION '%#' argument '%#' holds component handles in a container (%#). Pass one TComponentHandle<T> per argument.", owner,
                                 parameter._name, parameter._typeName );
                return false;
            }
            const bool bMemberOfComponent = bComponentOwner && method._bStatic == SW_FALSE && method._bConstructor == SW_FALSE;
            if ( bMemberOfComponent == false )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORFUNCTIONArgumentTComponentHandle", "ERROR: FUNCTION '%#' argument '%#' is a TComponentHandle<%#>. Typed handle arguments are checked against the scene of the "
                                                                                     "called component, so only non-static member functions of a Component may take them.",
                                 owner, parameter._name, target );
                return false;
            }
            parameter._typeName = "ComponentHandle";
        }
        return true;
    }

    struct PropertyResolver::ConstantSearch
    {
        const vector<string>* _pListPart;
        size_t                _partIndex;
        CXCursor              _found;
        bool                  _bFound; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };

    CXChildVisitResult PropertyResolver::constantSearchVisitor( CXCursor cursor, CXCursor, CXClientData data )
    {
        ConstantSearch*       pSearch  = static_cast<ConstantSearch*>( data );
        const vector<string>& listPart = *pSearch->_pListPart;
        if ( CursorReader::isStringEqual( clang_getCursorSpelling( cursor ), listPart[pSearch->_partIndex] ) == false )
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

    CXCursor PropertyResolver::findConstantIn( const CXCursor scope, const vector<string>& listPart )
    {
        ConstantSearch search{ &listPart, 0, clang_getNullCursor(), false };
        clang_visitChildren( scope, constantSearchVisitor, &search );
        return search._found;
    }

    CXCursor PropertyResolver::findNamedConstant( const CXCursor memberCursor, const string_view name )
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
                listNamespace.insert( listNamespace.begin(), CursorReader::getCursorSpelling( scope ) );
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

    bool PropertyResolver::resolveMaxCountName( const CXCursor memberCursor, ParsedPropertyInfo& prop, const string_view owner )
    {
        if ( prop._maxCountName.empty() )
            return true;
        const CXCursor found = findNamedConstant( memberCursor, prop._maxCountName );
        if ( clang_Cursor_isNull( found ) != 0 )
        {
            SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYMaxCountConstant", "ERROR: PROPERTY() '%#': MaxCount constant '%#' is not declared in this type or an enclosing scope", owner,
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
            SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYMaxCountConstant2", "ERROR: PROPERTY() '%#': MaxCount constant '%#' must be an integer constant between 1 and 4294967295", owner,
                             prop._maxCountName.c_str() );
            return false;
        }
        prop._maxCount     = static_cast<uint32>( value );
        prop._bHasMaxCount = SW_TRUE;
        return true;
    }

    bool PropertyResolver::resolveRangeConstants( const CXCursor memberCursor, ParsedPropertyInfo& prop, const string_view owner )
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
            StringBuilder<constant::buffer::kMax256> resolved;
            bool                                     bChanged = false;
            string_view                              rest     = side._pExpr->_text;
            for ( size_t comma = 0; comma != string_view::npos; )
            {
                comma                       = rest.find( ',' );
                const string_view term      = StringUtil::trim( rest.substr( 0, comma ) );
                rest                        = comma == string_view::npos ? string_view() : rest.substr( comma + 1 );
                const bool        bNegative = term.empty() == false && term.front() == '-';
                const string_view name      = bNegative ? StringUtil::trim( term.substr( 1 ) ) : term;
                float32           number{ 0.0f };
                const bool        bConstant = name.empty() == false && name.front() != '_' && StringUtil::parseFloat( term, number ) == false;
                if ( resolved.size() > 0 )
                    resolved.append( ", " );
                if ( bConstant == false )
                {
                    resolved.append( term );
                    continue;
                }
                const CXCursor found = findNamedConstant( memberCursor, name );
                if ( clang_Cursor_isNull( found ) != 0 )
                {
                    SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYConstantNot", "ERROR: PROPERTY() '%#': %# constant '%#' is not declared in this type or an enclosing scope", owner, side._label, name );
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
                    SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYConstantMust", "ERROR: PROPERTY() '%#': %# constant '%#' must be an integer or floating-point constant", owner, side._label, name );
                    return false;
                }
                utf8       buffer[40]{};
                const auto converted = std::to_chars( buffer, buffer + sizeof( buffer ), bNegative ? -value : value );
                resolved.append( buffer, static_cast<uint32>( converted.ptr - buffer ) );
                bChanged = true;
            }
            if ( bChanged == false )
                continue;
            side._pExpr->_text = string( resolved.view() );
            float32 parsed{ 0.0f };
            if ( StringUtil::parseFloat( resolved.view(), parsed ) == false )
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

    bool PropertyResolver::resolvePropertyConstants( const CXCursor memberCursor, ParsedPropertyInfo& prop, const string_view owner )
    {
        return resolveMaxCountName( memberCursor, prop, owner ) && resolveRangeConstants( memberCursor, prop, owner );
    }
} // namespace sw
