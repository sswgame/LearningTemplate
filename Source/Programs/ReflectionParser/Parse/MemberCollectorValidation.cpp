#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Programs/ReflectionParser/Config/ParserConfig.h"
#include "Programs/ReflectionParser/Parse/AnnotationApply.h"
#include "Programs/ReflectionParser/Parse/AnnotationFields.h"
#include "Programs/ReflectionParser/Parse/AnnotationLookup.h"
#include "Programs/ReflectionParser/Parse/ContainerTreeBuilder.h"
#include "Programs/ReflectionParser/Parse/CursorReader.h"
#include "Programs/ReflectionParser/Parse/DeclarationValidator.h"
#include "Programs/ReflectionParser/Parse/MemberCollector.h"
#include "Programs/ReflectionParser/Parse/ParsedReflection.h"
#include "Programs/ReflectionParser/Parse/PropertyResolver.h"
#include "Programs/ReflectionParser/Parse/ReflectAnnotation.h"
#include "Programs/ReflectionParser/Parse/SourceTokens.h"
#include "Programs/ReflectionParser/Run/ParserSession.h"
#include "Programs/ReflectionParser/TypeMap/TypeNameMap.h"
#include "Programs/ReflectionParser/TypeMap/TypeSpelling.h"

#include <clang-c/Index.h>

namespace sw
{
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

        struct MemberCollectorValidationInternal
        {
            /** @brief @p type 이 싣고 되살릴 값을 드는 형식인지 봅니다. 포인터 · 참조 · const 값 · `typeidentity::kArrNonStateType` 은 아니다. 배열은 원소로 본다. */
            static bool hasStateValue( const CXType type )
            {
                const CXType canonical = clang_getCanonicalType( type );
                if ( canonical.kind == CXType_ConstantArray )
                    return hasStateValue( clang_getArrayElementType( canonical ) );
                const bool bReference = canonical.kind == CXType_Pointer || canonical.kind == CXType_LValueReference || canonical.kind == CXType_RValueReference || canonical.kind == CXType_MemberPointer;
                if ( bReference || clang_isConstQualifiedType( canonical ) != 0 )
                    return false;
                const string identity = CursorReader::getTypeIdentity( canonical );
                if ( CursorReader::isAnyIdentity( identity, typeidentity::kArrNonStateType ) )
                    return false;
                const bool bSharedConst = CursorReader::isAnyIdentity( identity, typeidentity::kArrSharedPointerTemplate ) &&
                                          clang_isConstQualifiedType( clang_Type_getTemplateArgumentAsType( canonical, 0 ) ) != 0;
                return bSharedConst == false;
            }

            struct StateCodecSearch
            {
                bool _bFound;
            };

            /** @brief @p recordCursor 나 그 기반이 `serializeState` · `serializeGameState` 를 선언했는지 봅니다. */
            static bool hasStateCodec( const CXCursor recordCursor )
            {
                const CXCursor definition = clang_getCursorDefinition( recordCursor );
                if ( clang_Cursor_isNull( definition ) != 0 )
                    return false;
                StateCodecSearch search{ false };
                clang_visitChildren( definition, stateCodecVisitor, &search );
                return search._bFound;
            }

            static CXChildVisitResult stateCodecVisitor( CXCursor cursor, CXCursor, CXClientData data )
            {
                StateCodecSearch&  search = *static_cast<StateCodecSearch*>( data );
                const CXCursorKind kind   = clang_getCursorKind( cursor );
                if ( kind == CXCursor_CXXMethod )
                {
                    const string name = CursorReader::getCursorSpelling( cursor );
                    search._bFound    = name == annotation::kSerializeStateMethod || name == annotation::kSerializeGameStateMethod;
                }
                else if ( kind == CXCursor_CXXBaseSpecifier )
                {
                    const CXCursor baseDecl = CursorReader::getBaseClassDeclaration( cursor );
                    search._bFound          = clang_Cursor_isNull( baseDecl ) == 0 && hasStateCodec( baseDecl );
                }
                return search._bFound ? CXChildVisit_Break : CXChildVisit_Continue;
            }

            /** @brief 포트 형식 @p portType 의 기반 가운데 @p interfaceSpelling 으로 끝나는 것의 정규 이름입니다. 없으면 빈 글입니다. */
            static string findPortInterface( const CXType portType, const string& interfaceSpelling )
            {
                // 생성 코드는 이름공간과 무관하게 이 정규 이름을 쓴다(적은 철자는 생성 단위의 이름공간에서 다르게 풀릴 수 있다).
                const CXCursor portDecl = clang_getTypeDeclaration( clang_getCanonicalType( portType ) );
                if ( clang_Cursor_isNull( portDecl ) != 0 )
                    return {};
                PortBaseSearch search{ &interfaceSpelling, {} };
                clang_visitChildren( portDecl, portBaseVisitor, &search );
                return search._found;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void MemberCollector::validate( const CXCursor recordCursor )
    {
        // 모두 돈다 — 한 번의 실행이 그 타입의 오류를 모두 알린다.
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
        if ( validateStateDeclarations( recordCursor ) == false )
            _bHasError = SW_TRUE;
    }

    bool MemberCollector::validateStateDeclarations( const CXCursor recordCursor )
    {
        CXFile declarationFile = nullptr;
        clang_getExpansionLocation( clang_getCursorLocation( recordCursor ), &declarationFile, nullptr, nullptr, nullptr );
        const string headerPath = declarationFile != nullptr ? CursorReader::takeString( clang_getFileName( declarationFile ) ) : string{};
        bool         bStateType = isDerivedFromComponent( recordCursor, _pSession->_config );
        for ( const ParsedPropertyInfo& prop : _pType->_listProperty )
        {
            bStateType = bStateType || prop._bRuntimeState == SW_TRUE || prop._bSaveGame == SW_TRUE;
        }
        const bool bUndeclaredAllowed = bStateType == false || _pSession->_config.isStateDeclarationSkipped( headerPath );
        const bool bCodec             = bStateType && MemberCollectorValidationInternal::hasStateCodec( recordCursor );
        bool       bValid             = true;
        for ( const CollectedField& field : _listField )
        {
            const string owner = CursorReader::makeMemberOwnerName( _pType->_fullyQualifiedName, field._name );
            if ( field._declaration == StateDeclaration::Conflict )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORStateDeclarationConflict", "ERROR: '%#' carries more than one of PROPERTY(), SW_CODEC_STATE and SW_TRANSIENT - keep the one that says who carries the value.", owner );
                bValid = false;
                continue;
            }
            const bool bMarked = field._declaration == StateDeclaration::Codec || field._declaration == StateDeclaration::Transient;
            if ( bMarked && bStateType == false )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORStateDeclarationUnused", "ERROR: '%#' writes SW_CODEC_STATE or SW_TRANSIENT, but '%#' is neither a component nor a type with RuntimeState / SaveGame "
                                                                           "properties - remove it.",
                                 owner, _pType->_fullyQualifiedName );
                bValid = false;
                continue;
            }
            if ( field._declaration == StateDeclaration::Codec && bCodec == false )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORCodecStateWithoutCodec", "ERROR: SW_CODEC_STATE on '%#' - neither '%#' nor a base declares serializeState / serializeGameState, so nothing carries the value.",
                                 owner, _pType->_fullyQualifiedName );
                bValid = false;
                continue;
            }
            if ( bUndeclaredAllowed || bMarked || field._declaration == StateDeclaration::Property || MemberCollectorValidationInternal::hasStateValue( field._type ) == false )
                continue;
            // 남는 비트(`_reserved`)와 인터페이스 포트는 값이 아니다.
            bool bWithoutValue = StringUtil::startsWith( field._name, annotation::kReservedMemberPrefix );
            for ( const ParsedInterfaceExport& entry : _pType->_listInterfaceExport )
            {
                bWithoutValue = bWithoutValue || entry._portMember == field._name;
            }
            if ( bWithoutValue )
                continue;
            SW_LOG_ERROR_ID( "AstVisitor.ERRORStateUndeclared", "ERROR: '%#' has no state declaration - a value the game must get back (save, streaming, rollback, hot reload) is PROPERTY( RuntimeState ) "
                                                                "or SW_CODEC_STATE (carried by serializeState); a cache or per-frame value is SW_TRANSIENT.",
                             owner );
            bValid = false;
        }
        return bValid;
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
                                 _pType->_fullyQualifiedName, _listField.front()._name );
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
            const CollectedField* pField = nullptr;
            for ( const CollectedField& field : _listField )
            {
                if ( field._name == entry._portMember )
                    pField = &field;
            }
            if ( pField == nullptr )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORInterfacePortMissing", "ERROR: REFLECT_INTERFACE( %#, %# ) in '%#' names no data member - declare the port member in the same type.",
                                 entry._interfaceSpelling, entry._portMember, _pType->_fullyQualifiedName );
                bValid = false;
                continue;
            }
            entry._interfaceFQN = MemberCollectorValidationInternal::findPortInterface( pField->_type, entry._interfaceSpelling );
            if ( entry._interfaceFQN.empty() )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORInterfacePortBase", "ERROR: port '%#' of '%#' does not derive from interface '%#' - make the port type a nested 'struct ...Port final : %#'.",
                                 entry._portMember, _pType->_fullyQualifiedName, entry._interfaceSpelling, entry._interfaceSpelling );
                bValid = false;
            }
        }
        return bValid;
    }
} // namespace sw
