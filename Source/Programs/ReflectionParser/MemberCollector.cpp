#include "pch.h"

#include "Programs/ReflectionParser/MemberCollector.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Programs/ReflectionParser/AnnotationApply.h"
#include "Programs/ReflectionParser/AnnotationFields.h"
#include "Programs/ReflectionParser/AnnotationLookup.h"
#include "Programs/ReflectionParser/ContainerTreeBuilder.h"
#include "Programs/ReflectionParser/CursorReader.h"
#include "Programs/ReflectionParser/DeclarationValidator.h"
#include "Programs/ReflectionParser/FixedTypeAnalyzer.h"
#include "Programs/ReflectionParser/ParserConfig.h"
#include "Programs/ReflectionParser/ParserDefines.h"
#include "Programs/ReflectionParser/PropertyResolver.h"
#include "Programs/ReflectionParser/SourceTokens.h"
#include "Programs/ReflectionParser/TypeNameMap.h"

namespace sw
{
    MemberCollector::MemberCollector( ParsedTypeInfo& type, const ParserSession& session )
        : _pType{ &type }
        , _pSession{ &session }
        , _listMethodSignature{}
        , _listField{}
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

    void MemberCollector::collect( const CXCursor recordCursor )
    {
        clang_visitChildren( recordCursor, memberCollectVisitor, this );
    }

    void MemberCollector::validate()
    {
        // 넷 다 돈다 — 한 번의 실행이 그 타입의 오류를 모두 알린다.
        if ( DeclarationValidator::validateMemberFunctions( *_pType, _listMethodSignature ) == false )
            _bHasError = SW_TRUE;
        if ( DeclarationValidator::validatePropertyFlags( *_pType ) == false )
            _bHasError = SW_TRUE;
        if ( DeclarationValidator::validatePropertyRedirects( *_pType ) == false )
            _bHasError = SW_TRUE;
        if ( DeclarationValidator::validateBusBodyExport( *_pType, _bBusBody == SW_TRUE, _bExported == SW_TRUE, _pSession->_bModuleLocal ) == false )
            _bHasError = SW_TRUE;
        if ( DeclarationValidator::validateTickMode( *_pType ) == false )
            _bHasError = SW_TRUE;
        if ( validateInterfaces() == false )
            _bHasError = SW_TRUE;
    }

    bool MemberCollector::validateInterfaces()
    {
        bool bValid = true;
        if ( _pType->_bInterface == SW_TRUE )
        {
            // 상태가 붙으면 인터페이스가 아니다(`I` 붙은 상태 베이스가 다시 생기지 않게) — 생성 코드의 `static_assert` 보다 먼저 고칠 자리를 말한다.
            if ( _listField.empty() == false )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORInterfaceHasState",
                                 "ERROR: REFLECT( Interface ) '%#' has a data member '%#' - an interface is pure virtual functions only; move the state to the implementing component.",
                                 _pType->_fullyQualifiedName, _listField.front().first );
                bValid = false;
            }
            if ( _baseCount != 0 )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORInterfaceHasBase",
                                 "ERROR: REFLECT( Interface ) '%#' has a base '%#' - an interface stands alone; export each interface with its own REFLECT_INTERFACE line.",
                                 _pType->_fullyQualifiedName, _pType->_parentFQN );
                bValid = false;
            }
            if ( _pType->_listInterfaceExport.empty() == false )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORInterfaceExports", "ERROR: REFLECT( Interface ) '%#' has REFLECT_INTERFACE - only components export interfaces through port members.",
                                 _pType->_fullyQualifiedName );
                bValid = false;
            }
            return bValid;
        }
        for ( size_t index = 0; index < _pType->_listInterfaceExport.size(); ++index )
        {
            ParsedInterfaceExport& entry = _pType->_listInterfaceExport[index];
            for ( size_t earlier = 0; earlier < index; ++earlier )
            {
                if ( _pType->_listInterfaceExport[earlier]._interfaceSpelling == entry._interfaceSpelling )
                {
                    SW_LOG_ERROR_ID( "AstVisitor.ERRORInterfaceExportedTwice", "ERROR: '%#' exports interface '%#' twice - keep one REFLECT_INTERFACE line per interface.",
                                     _pType->_fullyQualifiedName, entry._interfaceSpelling );
                    bValid = false;
                }
            }
            const pair<string, CXType>* pField = nullptr;
            for ( const pair<string, CXType>& field : _listField )
            {
                if ( field.first == entry._portMember )
                    pField = &field;
            }
            if ( pField == nullptr )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORInterfacePortMissing", "ERROR: REFLECT_INTERFACE( %#, %# ) in '%#' names no data member - declare the port member in the same type.",
                                 entry._interfaceSpelling, entry._portMember, _pType->_fullyQualifiedName );
                bValid = false;
                continue;
            }
            entry._interfaceFQN = findPortInterface( pField->second, entry._interfaceSpelling );
            if ( entry._interfaceFQN.empty() )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORInterfacePortBase", "ERROR: port '%#' of '%#' does not derive from interface '%#' - make the port type a nested 'struct ...Port final : %#'.",
                                 entry._portMember, _pType->_fullyQualifiedName, entry._interfaceSpelling, entry._interfaceSpelling );
                bValid = false;
            }
        }
        return bValid;
    }

    namespace
    {
        /** @brief `findPortInterface` 의 기반 순회 상태입니다. */
        struct PortBaseSearch
        {
            const string* _pSpelling;
            string        _found;
        };

        CXChildVisitResult portBaseVisitor( CXCursor cursor, CXCursor, CXClientData data )
        {
            if ( clang_getCursorKind( cursor ) != CXCursor_CXXBaseSpecifier )
                return CXChildVisit_Continue;
            PortBaseSearch& search   = *static_cast<PortBaseSearch*>( data );
            const CXCursor  baseDecl = CursorReader::getBaseClassDeclaration( cursor );
            if ( clang_Cursor_isNull( baseDecl ) != 0 )
                return CXChildVisit_Continue;
            const string      fqn      = CursorReader::makeFullyQualifiedName( baseDecl );
            const string_view spelling = *search._pSpelling;
            const string_view full     = fqn;
            // 짧은 이름(`IHealthSource`)이나 한정 이름(`sw::IHealthSource`) 둘 다 받는다 — 한정은 `::` 경계에서만 맞춘다.
            const bool bQualifiedTail = full.size() > spelling.size() + 2 && full.substr( full.size() - spelling.size() ) == spelling &&
                                        full.substr( full.size() - spelling.size() - 2, 2 ) == "::";
            if ( full == spelling || bQualifiedTail )
            {
                search._found = fqn;
                return CXChildVisit_Break;
            }
            return CXChildVisit_Continue;
        }
    } // namespace

    string MemberCollector::findPortInterface( const CXType portType, const string& interfaceSpelling )
    {
        // 생성 코드는 이름공간과 무관하게 이 정규 이름을 쓴다(적은 철자는 생성 단위의 이름공간에서 다르게 풀릴 수 있다).
        const CXCursor portDecl = clang_getTypeDeclaration( clang_getCanonicalType( portType ) );
        if ( clang_Cursor_isNull( portDecl ) != 0 )
            return {};
        PortBaseSearch search{ &interfaceSpelling, {} };
        clang_visitChildren( portDecl, portBaseVisitor, &search );
        return search._found;
    }

    CXChildVisitResult MemberCollector::memberCollectVisitor( CXCursor cursor, CXCursor, CXClientData data )
    {
        MemberCollector&   collector = *static_cast<MemberCollector*>( data );
        const CXCursorKind kind      = clang_getCursorKind( cursor );
        const bool         bFunction = kind == CXCursor_CXXMethod || kind == CXCursor_Constructor || kind == CXCursor_FunctionTemplate ||
                               kind == CXCursor_FunctionDecl || kind == CXCursor_Destructor;
        const bool bExportAttr = kind == CXCursor_DLLExport || kind == CXCursor_DLLImport || kind == CXCursor_VisibilityAttr;
        const bool bBusVersion = kind == CXCursor_VarDecl && CursorReader::isStringEqual( clang_getCursorSpelling( cursor ), annotation::kBusVersionMember );
        if ( kind == CXCursor_CXXBaseSpecifier )
            collector.collectBase( cursor );
        else if ( kind == CXCursor_FieldDecl )
        {
            collector._listField.emplace_back( CursorReader::getCursorSpelling( cursor ), clang_getCursorType( cursor ) );
            collector.collectField( cursor );
        }
        else if ( bFunction )
            collector.collectFunctionMember( cursor, kind );
        else if ( bBusVersion )
            collector._bBusBody = SW_TRUE;
        else if ( bExportAttr )
            collector._bExported = SW_TRUE;
        return CXChildVisit_Continue;
    }

    void MemberCollector::collectBase( const CXCursor cursor )
    {
        const CXCursor baseDecl = CursorReader::getBaseClassDeclaration( cursor );
        const string   baseFQN  = ( clang_Cursor_isNull( baseDecl ) == 0 ) ? CursorReader::makeFullyQualifiedName( baseDecl ) : string{};

        ++_baseCount;
        _pType->_baseCount      = _baseCount;
        const bool bVirtualBase = clang_isVirtualBase( cursor ) != 0;
        if ( _baseCount == 1 && bVirtualBase == false )
        {
            _pType->_parentFQN        = baseFQN;
            _pType->_bParentReflected = AnnotationLookup::isReflectedRecord( baseDecl ) ? SW_TRUE : SW_FALSE;
            return;
        }

        CXFile file = nullptr;
        uint32 line = 0;
        clang_getExpansionLocation( clang_getCursorLocation( cursor ), &file, &line, nullptr, nullptr );
        const string path = file != nullptr ? CursorReader::takeString( clang_getFileName( file ) ) : string{};
        SW_LOG_ERROR_ID( "AstVisitor.ERRORREFLECTTypeBase", "ERROR: %#(%#): REFLECT type '%#' has %# base '%#'. A REFLECT type takes at most one non-virtual base: castTo "
                                                            "reinterpret_casts along the reflected parent chain, so any other base gets the wrong address. "
                                                            "Compose the extra behaviour as a component or member instead.",
                         path, line, _pType->_fullyQualifiedName, bVirtualBase ? "a virtual" : "a second", baseFQN );
        _bHasError = SW_TRUE;
    }

    void MemberCollector::fillParameters( const CXCursor cursor, ParsedFunctionInfo& method, const ParserSession& session )
    {
        const int32 numArgs = clang_Cursor_getNumArguments( cursor );
        for ( int32 argIndex = 0; argIndex < numArgs; ++argIndex )
        {
            const CXCursor      argCursor = clang_Cursor_getArgument( cursor, static_cast<uint32>( argIndex ) );
            ParsedParameterInfo parameter;
            parameter._name         = CursorReader::getCursorSpelling( argCursor );
            parameter._typeName     = session._typeNameMap.normalize( CursorReader::takeString( clang_getTypeSpelling( clang_getCursorType( argCursor ) ) ) );
            parameter._defaultValue = SourceTokens::readDefaultArgument( argCursor );
            // 타입 핸들 인자는 썽크가 지역 변수로 만들 정규 철자를 함께 적는다(`resolveComponentHandleParameters`). const · 참조는 뗀다.
            string canonical = CursorReader::takeString( clang_getTypeSpelling( clang_getCanonicalType( clang_getNonReferenceType( clang_getCursorType( argCursor ) ) ) ) );
            if ( StringUtil::startsWith( canonical, "const " ) )
                canonical.erase( 0, 6 );
            parameter._canonicalTypeName = std::move( canonical );
            // 타입 핸들은 형식의 정체로 가른다 — 한 값이면 대상 `T` 를 읽고, 컨테이너 안에 들었으면 `_bHoldsTypedHandle` 만 켜 두어 오류가 된다.
            const CXType valueType       = clang_getNonReferenceType( clang_getCursorType( argCursor ) );
            parameter._bHoldsTypedHandle = CursorReader::containsTypeIdentity( valueType, typeidentity::kTypedComponentHandle );
            if ( CursorReader::getTypeIdentity( valueType ) == typeidentity::kTypedComponentHandle )
                parameter._handleTargetTypeName = session._typeNameMap.normalize( CursorReader::getTemplateArgumentSpelling( valueType, 0 ) );
            method._listParameter.push_back( std::move( parameter ) );
        }
    }

    bool MemberCollector::isEventType( const CXType fieldType )
    {
        return CursorReader::getTypeIdentity( fieldType ) == typeidentity::kMulticastDelegate;
    }

    CXType MemberCollector::findEventFunctionType( const CXType fieldType )
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

    void MemberCollector::collectEvent( const CXCursor cursor, const string& spelling, const CXType fieldType )
    {
        const ParserSession& session = *_pSession;
        ParsedEventInfo      event;
        event._memberName             = CursorReader::getCursorSpelling( cursor );
        event._annotation._memberName = event._memberName;
        event._annotation._name       = event._memberName;
        event._annotation._typeName   = session._typeNameMap.normalize( CursorReader::takeString( clang_getTypeSpelling( fieldType ) ) );
        const string owner            = CursorReader::makeMemberOwnerName( _pType->_fullyQualifiedName, event._memberName );
        if ( AnnotationLookup::applyAnnotation( spelling, event._annotation, session, owner ) == false )
        {
            _bHasError = SW_TRUE;
            return;
        }
        for ( const string& token : AnnotationApply::splitAnnotationArgs( AnnotationApply::annotationArgumentText( spelling, annotation::kPropertyPrefix ) ) )
        {
            // 키 철자가 곧 필드 표의 줄 이름이다. 모르는 키는 위의 적용이 이미 알렸다.
            const size_t      eqPos = token.find( '=' );
            const string_view key   = eqPos == string::npos ? string_view( token ) : StringUtil::trim( string_view( token ).substr( 0, eqPos ) );
            if ( getAnnotationScope<ParsedPropertyInfo>().findField( key ) != nullptr && AnnotationFields::canAcceptOnEvent( key ) == false )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYEventEvent", "ERROR: PROPERTY() on event '%#' has '%#'. An event (multicast delegate) takes display metadata only (%#).", owner, token,
                                 AnnotationFields::makeEventFieldList() );
                _bHasError = SW_TRUE;
                return;
            }
        }

        // 함수 타입은 MulticastDelegate 의 첫 템플릿 인자다. 반환은 void 만 받는다 — broadcast 는 반환값을 모으지 않는다.
        const CXType functionType = findEventFunctionType( fieldType );
        const CXType resultType   = clang_getResultType( functionType );
        if ( functionType.kind != CXType_FunctionProto || resultType.kind != CXType_Void )
        {
            SW_LOG_ERROR_ID( "AstVisitor.ERROREventMustMulticastDelegate", "ERROR: event '%#' must be MulticastDelegate<void( ... )> - broadcast does not collect return values.", owner );
            _bHasError = SW_TRUE;
            return;
        }
        const vector<string> listName = SourceTokens::readEventParameterNames( cursor );
        const int32          argCount = clang_getNumArgTypes( functionType );
        for ( int32 argIndex = 0; argIndex < argCount; ++argIndex )
        {
            ParsedParameterInfo parameter;
            parameter._typeName = session._typeNameMap.normalize( CursorReader::takeString( clang_getTypeSpelling( clang_getArgType( functionType, static_cast<uint32>( argIndex ) ) ) ) );
            if ( static_cast<size_t>( argIndex ) < listName.size() )
                parameter._name = listName[static_cast<size_t>( argIndex )];
            event._listParameter.push_back( std::move( parameter ) );
        }
        _pType->_listEvent.push_back( std::move( event ) );
    }

    void MemberCollector::collectField( const CXCursor cursor )
    {
        // 멤버 앞의 속성은 선언에 붙어 AST 에 남는다(형식 앞 매크로만 표식 선언이다) — 자식 속성만 본다.
        const string spelling = AnnotationLookup::findAnnotateAttr( cursor, annotation::kPropertyPrefix );
        if ( spelling.empty() )
            return;

        if ( isEventType( clang_getCursorType( cursor ) ) )
        {
            collectEvent( cursor, spelling, clang_getCursorType( cursor ) );
            return;
        }

        const ParserSession& session   = *_pSession;
        const CXType         fieldType = ContainerTreeBuilder::resolvePropertyAlias( clang_getCursorType( cursor ), session ); // 사용자 별칭은 벗긴다
        ParsedPropertyInfo   prop;
        prop._memberName = CursorReader::getCursorSpelling( cursor );
        prop._name       = prop._memberName;
        // 칸 이름이 매크로로 바뀌면(Windows CRT 의 `_stat` → `_stat64i32`) 리플렉션 이름 · 데이터 속성 이름이 소스와 조용히 어긋난다.
        // 선언의 소스 토큰에 그 이름이 없으면 매크로가 만든 이름이다 — 칸 이름을 바꾸게 멈춘다.
        if ( SourceTokens::isSpelledInSource( cursor, prop._memberName ) == false )
        {
            SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYRenamedMacro", "ERROR: PROPERTY() '%#' in '%#' is renamed by a macro (the source spells another name) - rename the field",
                             prop._memberName.c_str(), CursorReader::makeFullyQualifiedName( clang_getCursorSemanticParent( cursor ) ).c_str() );
            _bHasError = SW_TRUE;
            return;
        }
        // 템플릿 인자 안의 별칭까지 벗긴 이름이다. 별칭이 없으면 `fieldType` 의 표기 그대로다.
        string resolvedSpelling;
        (void)ContainerTreeBuilder::tryResolveAliasSpelling( clang_getCursorType( cursor ), session, resolvedSpelling ); // 벗겼든 아니든 표기를 쓴다
        prop._typeName = session._typeNameMap.normalize( resolvedSpelling );
        if ( clang_Cursor_isBitField( cursor ) != 0 )
        {
            const int32 bitWidth = clang_getFieldDeclBitWidth( cursor );
            if ( bitWidth != 1 )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYBitfieldBit", "ERROR: PROPERTY() bitfield '%#' in '%#' has bit width %#. Only 1-bit bitfield boolean flags (e.g. uint8 _flag : 1;) are supported in reflection!",
                                 prop._name.c_str(), CursorReader::makeFullyQualifiedName( clang_getCursorSemanticParent( cursor ) ).c_str(), bitWidth );
                _bHasError = SW_TRUE;
                return;
            }

            // 자리는 측정하지 않는다 — 런타임이 그 구성의 레이아웃에서 찾는다(`PropertyInfo::resolveBitField`).
            prop._bIsBitField = SW_TRUE;
        }

        const string owner = CursorReader::makeMemberOwnerName( _pType->_fullyQualifiedName, prop._memberName );
        if ( AnnotationLookup::applyAnnotation( spelling, prop, session, owner ) == false || PropertyResolver::resolvePropertyConstants( cursor, prop, owner ) == false )
        {
            _bHasError = SW_TRUE;
            return;
        }
        // 이름 · 경로 칸의 C++ 문자열 기본값 — 데이터가 그 칸을 안 적으면 맞지 않는 이름이 조용히 쓰인다(판정은 `validatePropertyFlags`). 테스트 형식은 기본값 자체를 시험하므로 보지 않는다.
        CXFile declarationFile = nullptr;
        clang_getExpansionLocation( clang_getCursorLocation( cursor ), &declarationFile, nullptr, nullptr, nullptr );
        const bool bNameCheckSkipped = declarationFile != nullptr && session._config.isNameFieldCheckSkipped( CursorReader::takeString( clang_getFileName( declarationFile ) ) );
        if ( bNameCheckSkipped == false && SourceTokens::hasStringLiteralDefault( cursor, prop._memberName ) &&
             CursorReader::isNameFieldType( clang_getCursorType( cursor ) ) )
            prop._bHasNameLiteralDefault = SW_TRUE;
        if ( ContainerTreeBuilder::fillContainerDetails( prop, fieldType, session ) == false )
        {
            SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYContainerType", "ERROR: PROPERTY() '%#' - its container type could not be read (see above).", owner );
            _bHasError = SW_TRUE;
            return;
        }
        if ( PropertyResolver::resolveComponentHandleProperty( prop, fieldType, session, owner ) == false ||
             PropertyResolver::resolveAssetRefProperty( prop, fieldType, session, owner ) == false || PropertyResolver::resolveCatalogKeyProperty( prop, fieldType, session, owner ) == false )
        {
            _bHasError = SW_TRUE;
            return;
        }
        if ( DeclarationValidator::isInterpAllowed( prop, session._typeNameMap, owner ) == false ||
             DeclarationValidator::isRuntimeStateAllowed( prop, session._typeNameMap, session._containerTypeMap, owner ) == false || DeclarationValidator::applyDisplayMeta( prop, fieldType, owner ) == false )
        {
            _bHasError = SW_TRUE;
            return;
        }
        FixedTypeAnalyzer::fillFixedLayoutFacts( cursor, fieldType, prop, session );
        _pType->_listProperty.push_back( std::move( prop ) );
    }

    void MemberCollector::collectAccessorProperty( const CXCursor cursor, const string& spelling )
    {
        const ParserSession& session = *_pSession;
        ParsedPropertyInfo   prop;
        prop._memberName   = CursorReader::getCursorSpelling( cursor );
        prop._name         = prop._memberName;
        prop._bIsAccessor  = SW_TRUE;
        const string owner = CursorReader::makeMemberOwnerName( _pType->_fullyQualifiedName, prop._memberName );

        const CXType resultType = clang_getCursorResultType( cursor );
        const CXType valueType  = clang_getPointeeType( resultType );
        const bool   bShapeOk   = clang_CXXMethod_isStatic( cursor ) == 0 && clang_Cursor_getNumArguments( cursor ) == 0 &&
                              resultType.kind == CXType_LValueReference && clang_isConstQualifiedType( valueType ) == 0;
        if ( bShapeOk == false )
        {
            SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYMethodNeeds", "ERROR: PROPERTY() on method '%#' needs a non-static method with no parameters that returns a non-const "
                                                                    "lvalue reference (T&) to the value. Put PROPERTY() on a field instead if the value lives in the object.",
                             owner );
            _bHasError = SW_TRUE;
            return;
        }
        // 위에서 const 가 아님을 확인했으므로 철자에 한정자가 붙지 않는다. `clang_getUnqualifiedType` 은 libclang 16 부터라 리눅스 CI 의
        // libclang 에는 없다 — 쓰지 말 것.
        prop._typeName = session._typeNameMap.normalize( CursorReader::takeString( clang_getTypeSpelling( valueType ) ) );

        if ( AnnotationLookup::applyAnnotation( spelling, prop, session, owner ) == false || PropertyResolver::resolvePropertyConstants( cursor, prop, owner ) == false )
        {
            _bHasError = SW_TRUE;
            return;
        }
        if ( ContainerTreeBuilder::fillContainerDetails( prop, valueType, session ) == false || prop._bIsContainer == SW_TRUE )
        {
            SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYMethodReturns", "ERROR: PROPERTY() on method '%#' returns a container. Accessor properties support single values only.", owner );
            _bHasError = SW_TRUE;
            return;
        }
        if ( PropertyResolver::resolveComponentHandleProperty( prop, valueType, session, owner ) == false ||
             PropertyResolver::resolveAssetRefProperty( prop, valueType, session, owner ) == false || PropertyResolver::resolveCatalogKeyProperty( prop, valueType, session, owner ) == false )
        {
            _bHasError = SW_TRUE;
            return;
        }
        if ( DeclarationValidator::isInterpAllowed( prop, session._typeNameMap, owner ) == false ||
             DeclarationValidator::isRuntimeStateAllowed( prop, session._typeNameMap, session._containerTypeMap, owner ) == false || DeclarationValidator::applyDisplayMeta( prop, valueType, owner ) == false )
        {
            _bHasError = SW_TRUE;
            return;
        }
        // 값이 객체 밖에 있어 고정 배치형 코덱이 필드처럼 읽고 쓸 수 없습니다.
        prop._fixedType = FixedTypeAnalyzer::makeIneligibleFixedType( make_shared<ParsedFixedType>(), "accessor property (the value lives outside the object)" );
        _pType->_listProperty.push_back( std::move( prop ) );
    }

    struct MemberCollector::MethodAnnotation
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

    CXChildVisitResult MemberCollector::methodAnnotationVisitor( CXCursor cursor, CXCursor, CXClientData data )
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

    void MemberCollector::collectConstructor( const CXCursor cursor, const string& functionSpelling )
    {
        if ( _bSkipConstructors == SW_TRUE )
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
        fillParameters( cursor, method, *_pSession );

        const string owner = CursorReader::makeMemberOwnerName( _pType->_fullyQualifiedName, annotation::kCtorLookupName );
        if ( functionSpelling.empty() == false && AnnotationLookup::applyAnnotation( functionSpelling, method, *_pSession, owner ) == false )
        {
            _bHasError = SW_TRUE;
            return;
        }
        if ( PropertyResolver::resolveComponentHandleParameters( method, false, owner ) == false ) // 생성자는 매니저가 없다 — 타입 핸들 인자는 오류다
        {
            _bHasError = SW_TRUE;
            return;
        }
        _pType->_listMethod.push_back( std::move( method ) );
    }

    void MemberCollector::synthesizeDefaultConstructor()
    {
        if ( _bSkipConstructors == SW_TRUE )
            return;
        for ( const ParsedFunctionInfo& method : _pType->_listMethod )
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
        _pType->_listMethod.push_back( std::move( method ) );
    }

    void MemberCollector::collectMethod( const CXCursor cursor, const string& functionSpelling )
    {
        // FUNCTION() 은 선언에 붙는 속성이다 — 없으면 리플렉션 함수가 아니다.
        if ( functionSpelling.empty() )
            return;

        const ParserSession& session = *_pSession;
        ParsedFunctionInfo   method;
        method._name           = CursorReader::getCursorSpelling( cursor );
        method._returnTypeName = session._typeNameMap.normalize( CursorReader::takeString( clang_getTypeSpelling( clang_getCursorResultType( cursor ) ) ) );
        method._bStatic        = clang_CXXMethod_isStatic( cursor ) != 0 ? SW_TRUE : SW_FALSE;
        method._bConst         = clang_CXXMethod_isConst( cursor ) != 0 ? SW_TRUE : SW_FALSE;
        fillParameters( cursor, method, session );

        const string owner = CursorReader::makeMemberOwnerName( _pType->_fullyQualifiedName, method._name );
        if ( AnnotationLookup::applyAnnotation( functionSpelling, method, session, owner ) == false )
        {
            _bHasError = SW_TRUE;
            return;
        }
        const bool bComponentOwner = isDerivedFromComponent( clang_getCursorSemanticParent( cursor ), session._config );
        if ( PropertyResolver::resolveComponentHandleParameters( method, bComponentOwner, owner ) == false )
        {
            _bHasError = SW_TRUE;
            return;
        }
        _pType->_listMethod.push_back( std::move( method ) );
    }

    void MemberCollector::recordMethodSignature( const CXCursor cursor, const string& name )
    {
        ParsedMethodSignature signature;
        signature._name     = name;
        const int32 numArgs = clang_Cursor_getNumArguments( cursor );
        for ( int32 argIndex = 0; argIndex < numArgs; ++argIndex )
        {
            const CXCursor argCursor = clang_Cursor_getArgument( cursor, static_cast<uint32>( argIndex ) );
            signature._listParameterTypeName.push_back(
                _pSession->_typeNameMap.normalize( CursorReader::takeString( clang_getTypeSpelling( clang_getCursorType( argCursor ) ) ) ) );
        }
        _listMethodSignature.push_back( std::move( signature ) );
    }

    void MemberCollector::collectInterfaceExport( const CXCursor cursor )
    {
        const string      text  = AnnotationLookup::findAnnotateAttr( cursor, annotation::kReflectInterfacePrefix );
        const string_view view  = text;
        const size_t      head  = view.find( ';' );
        const size_t      comma = view.find( ',' );
        if ( head == string_view::npos || comma == string_view::npos || comma < head )
        {
            SW_LOG_ERROR_ID( "AstVisitor.ERRORInterfaceExportShape", "ERROR: REFLECT_INTERFACE in '%#' needs two arguments - REFLECT_INTERFACE( IInterface, _portMember ).",
                             _pType->_fullyQualifiedName );
            _bHasError = SW_TRUE;
            return;
        }
        // 매크로가 `#InterfaceType "," #PortMember` 로 붙여 공백이 없다.
        ParsedInterfaceExport entry;
        entry._interfaceSpelling = string( view.substr( head + 1, comma - head - 1 ) );
        entry._portMember        = string( view.substr( comma + 1 ) );
        _pType->_listInterfaceExport.push_back( std::move( entry ) );
    }

    void MemberCollector::collectFunctionMember( const CXCursor cursor, const CXCursorKind kind )
    {
        // REFLECT_BODY 마커 함수다. 리플렉트 FUNCTION 이 아니다.
        const string spelling = CursorReader::getCursorSpelling( cursor );
        if ( spelling == annotation::kReflectBodyMarkerFn )
        {
            _bBodyFound = SW_TRUE;
            return;
        }
        if ( string_view( spelling ).substr( 0, string_view( annotation::kReflectInterfaceMarkerFn ).size() ) == annotation::kReflectInterfaceMarkerFn )
        {
            collectInterfaceExport( cursor );
            return;
        }

        MethodAnnotation annotation;
        clang_visitChildren( cursor, methodAnnotationVisitor, &annotation );
        if ( annotation._bBody == SW_TRUE )
            _bBodyFound = SW_TRUE;

        if ( kind == CXCursor_Constructor )
        {
            collectConstructor( cursor, annotation._functionSpelling );
            return;
        }
        if ( kind == CXCursor_CXXMethod )
        {
            recordMethodSignature( cursor, spelling );
            if ( annotation._propertySpelling.empty() == false )
            {
                if ( annotation._functionSpelling.empty() == false )
                {
                    SW_LOG_ERROR_ID( "AstVisitor.ERRORBothPROPERTYFUNCTION", "ERROR: '%#' has both PROPERTY() and FUNCTION(). A method is either a value accessor (PROPERTY) or a callable (FUNCTION).",
                                     CursorReader::makeMemberOwnerName( _pType->_fullyQualifiedName, spelling ) );
                    _bHasError = SW_TRUE;
                    return;
                }
                collectAccessorProperty( cursor, annotation._propertySpelling );
                return;
            }
            collectMethod( cursor, annotation._functionSpelling );
            return;
        }

        // 멤버 함수 템플릿은 인자 타입이 정해지지 않아 등록할 수 없다. 클래스 템플릿과 같이 생성 오류다(건너뛰면 적은 FUNCTION 이 조용히 사라진다).
        if ( kind == CXCursor_FunctionTemplate && annotation._functionSpelling.empty() == false )
        {
            SW_LOG_ERROR_ID( "AstVisitor.FUNCTIONMemberFunctionTemplate", "ERROR: FUNCTION on a member function template is not supported: %#",
                             CursorReader::makeMemberOwnerName( _pType->_fullyQualifiedName, spelling ) );
            _bHasError = SW_TRUE;
        }
    }

    struct MemberCollector::ComponentSearch
    {
        const vector<string>* _pListBaseType;
        bool                  _bDerives; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };

    CXChildVisitResult MemberCollector::componentBaseVisitor( CXCursor cursor, CXCursor, CXClientData clientData )
    {
        if ( clang_getCursorKind( cursor ) != CXCursor_CXXBaseSpecifier )
            return CXChildVisit_Continue;

        ComponentSearch* pSearch  = static_cast<ComponentSearch*>( clientData );
        const CXCursor   baseDecl = CursorReader::getBaseClassDeclaration( cursor );
        if ( clang_Cursor_isNull( baseDecl ) != 0 || isDerivedFromAny( baseDecl, *pSearch->_pListBaseType ) == false )
            return CXChildVisit_Continue;

        pSearch->_bDerives = true;
        return CXChildVisit_Break;
    }

    bool MemberCollector::isDerivedFromComponent( const CXCursor cursor, const ParserConfig& config )
    {
        return isDerivedFromAny( cursor, config._listComponentBaseType );
    }

    bool MemberCollector::isDerivedFromAny( const CXCursor cursor, const vector<string>& listBaseType )
    {
        // 같은 중간 베이스 클래스를 거듭 재귀 탐색하지 않게 한다(스레드마다 한 벌). 열쇠는 (기반 목록, FQN) — 목록마다 답이 다르다.
        thread_local unordered_map<string, bool> s_mapFqnToDerives;

        const string fqn = CursorReader::makeFullyQualifiedName( cursor );
        for ( const string& baseType : listBaseType )
        {
            if ( fqn == baseType )
                return true;
        }

        StringBuilder<constant::buffer::kMax512> cacheKeyText;
        cacheKeyText.appendFormat( "%#|%#", listBaseType.empty() ? string_view() : string_view( listBaseType.front() ), fqn );
        const string cacheKey( cacheKeyText.view() );
        const auto   cacheIt = s_mapFqnToDerives.find( cacheKey );
        if ( cacheIt != s_mapFqnToDerives.end() )
            return cacheIt->second;

        // 캐시 미스. 순환 참조를 막으려고 먼저 false 로 넣는다
        s_mapFqnToDerives[cacheKey] = false;

        ComponentSearch search{ &listBaseType, false };
        clang_visitChildren( cursor, componentBaseVisitor, &search );
        s_mapFqnToDerives[cacheKey] = search._bDerives;
        return search._bDerives;
    }
} // namespace sw
