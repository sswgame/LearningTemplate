#include "pch.h"

#include "Programs/ReflectionParser/DeclarationValidator.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/ReflectionUnits.h"

#include "Programs/ReflectionParser/AnnotationLookup.h"
#include "Programs/ReflectionParser/CursorReader.h"
#include "Programs/ReflectionParser/ParserConfig.h"
#include "Programs/ReflectionParser/ParserDefines.h"

namespace sw
{
    bool DeclarationValidator::isValidatorDeclared( const vector<ParsedMethodSignature>& listMethodSignature, const string_view name )
    {
        for ( const ParsedMethodSignature& signature : listMethodSignature )
        {
            const bool bMatchesValidationSignature = signature._name == name && signature._listParameterTypeName.size() == 1 &&
                                                     signature._listParameterTypeName[0] == annotation::kValidationContextTypeName;
            if ( bMatchesValidationSignature )
                return true;
        }
        return false;
    }

    const ParsedMethodSignature* DeclarationValidator::findMethodSignature( const vector<ParsedMethodSignature>& listMethodSignature, const string_view name, const size_t maxParameterCount )
    {
        for ( const ParsedMethodSignature& signature : listMethodSignature )
        {
            if ( signature._name == name && signature._listParameterTypeName.size() <= maxParameterCount )
                return &signature;
        }
        return nullptr;
    }

    bool DeclarationValidator::validateTickMode( const ParsedTypeInfo& type )
    {
        if ( type._tickMode.empty() || type._tickMode == "Batch" || type._tickMode == "Task" || type._tickMode == "Main" )
            return true;
        SW_LOG_ERROR_ID( "AstVisitor.ERRORUnknownTickMode", "ERROR: REFLECT( TickMode = %# ) on '%#' - TickMode is Batch, Task or Main.", type._tickMode, type._fullyQualifiedName );
        return false;
    }

    bool DeclarationValidator::validateMemberFunctions( ParsedTypeInfo& type, const vector<ParsedMethodSignature>& listMethodSignature )
    {
        bool bValid = true;
        if ( type._validate.empty() == false && isValidatorDeclared( listMethodSignature, type._validate ) == false )
        {
            SW_LOG_ERROR_ID( "AstVisitor.ERRORREFLECTValidateNeeds", "ERROR: REFLECT( Validate = %# ) on '%#' needs a method of the same type: void %#( ValidationContext& context ) [const].",
                             type._validate, type._fullyQualifiedName, type._validate );
            bValid = false;
        }
        for ( ParsedPropertyInfo& prop : type._listProperty )
        {
            if ( prop._validate.empty() == false && isValidatorDeclared( listMethodSignature, prop._validate ) == false )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYValidateNeeds", "ERROR: PROPERTY( Validate = %# ) on '%#' needs a method of the same type: void %#( ValidationContext& context ) [const].",
                                 prop._validate, CursorReader::makeMemberOwnerName( type._fullyQualifiedName, prop._name ), prop._validate );
                bValid = false;
            }
            if ( prop._repNotify.empty() )
                continue;
            const string                 owner      = CursorReader::makeMemberOwnerName( type._fullyQualifiedName, prop._name );
            const ParsedMethodSignature* pSignature = findMethodSignature( listMethodSignature, prop._repNotify, 1 );
            const bool                   bTakesOld  = pSignature != nullptr && pSignature->_listParameterTypeName.size() == 1;
            if ( pSignature == nullptr || ( bTakesOld && pSignature->_listParameterTypeName[0] != prop._typeName ) )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYRepNotifyNeeds", "ERROR: PROPERTY( RepNotify = %# ) on '%#' needs a method of the same type named '%#' taking nothing or the old "
                                                                           "value (const %#&).",
                                 prop._repNotify, owner, prop._repNotify, prop._typeName );
                bValid = false;
                continue;
            }
            prop._bRepNotifyTakesOldValue = bTakesOld ? SW_TRUE : SW_FALSE;
        }
        return bValid;
    }

    bool DeclarationValidator::validatePropertyFlags( const ParsedTypeInfo& type )
    {
        bool bValid = true;
        for ( const ParsedPropertyInfo& prop : type._listProperty )
        {
            if ( prop._bRuntimeState == SW_FALSE )
                continue;
            const string owner = CursorReader::makeMemberOwnerName( type._fullyQualifiedName, prop._name );
            if ( prop._bTransient == SW_TRUE )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYRuntimeStateTransient", "ERROR: PROPERTY( RuntimeState, Transient ) on '%#' - RuntimeState is written to snapshots and saves, Transient is never "
                                                                                  "written. Keep one.",
                                 owner );
                bValid = false;
            }
            if ( prop._bRequired == SW_TRUE )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYRuntimeStateRequired", "ERROR: PROPERTY( RuntimeState, Required ) on '%#' - authoring files never write a RuntimeState field, so it cannot be "
                                                                                 "required.",
                                 owner );
                bValid = false;
            }
        }
        return bValid;
    }

    bool DeclarationValidator::validatePropertyRedirects( const ParsedTypeInfo& type )
    {
        bool                              bValid       = true;
        const vector<ParsedPropertyInfo>& listProperty = type._listProperty;
        vector<string_view>               listSeenAlias{};
        for ( const ParsedPropertyInfo& prop : listProperty )
        {
            for ( const string& alias : prop._listAlias )
            {
                const string owner = CursorReader::makeMemberOwnerName( type._fullyQualifiedName, prop._name );
                for ( const ParsedPropertyInfo& other : listProperty )
                {
                    if ( StringUtil::equals( alias, other._name, true ) )
                    {
                        SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYAliasCurrent", "ERROR: PROPERTY( Alias = %# ) on '%#' is the current name of '%#' - an old name must not be a field that exists now.", alias,
                                         owner, other._name );
                        bValid = false;
                    }
                }
                for ( const string_view seen : listSeenAlias )
                {
                    if ( StringUtil::equals( alias, seen, true ) )
                    {
                        SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYAliasDeclared", "ERROR: PROPERTY( Alias = %# ) on '%#' is declared twice in '%#' - one old name reads into one field.", alias, owner,
                                         type._fullyQualifiedName );
                        bValid = false;
                    }
                }
                listSeenAlias.push_back( alias );
            }
        }
        return bValid;
    }

    const ParsedEnumeratorInfo* DeclarationValidator::findEnumeratorIgnoreCase( const ParsedEnumInfo& enumInfo, const string_view name )
    {
        for ( const ParsedEnumeratorInfo& enumerator : enumInfo._listEnumerator )
        {
            if ( StringUtil::equals( enumerator._name, name, true ) )
                return &enumerator;
        }
        return nullptr;
    }

    bool DeclarationValidator::validateEnumeratorRedirects( const ParsedEnumInfo& enumInfo )
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
                    SW_LOG_ERROR_ID( "AstVisitor.ERRORENUMExpectsOld", "ERROR: ENUM( %# ) on '%#' expects \"Old:Target\", got '%#:%#'.", pKey, enumInfo._fullyQualifiedName, oldName, targetName );
                    bValid = false;
                    continue;
                }
                const ParsedEnumeratorInfo* pTarget = findEnumeratorIgnoreCase( enumInfo, targetName );
                if ( pTarget == nullptr || pTarget->_name != targetName )
                {
                    SW_LOG_ERROR_ID( "AstVisitor.ERRORENUMNotEnumerator", "ERROR: ENUM( %# = \"%#:%#\" ) on '%#' - '%#' is not an enumerator of it (spell it as declared).", pKey, oldName, targetName,
                                     enumInfo._fullyQualifiedName, targetName );
                    bValid = false;
                }
                const ParsedEnumeratorInfo* pCurrent = findEnumeratorIgnoreCase( enumInfo, oldName );
                if ( pCurrent != nullptr )
                {
                    SW_LOG_ERROR_ID( "AstVisitor.ERRORENUMStillEnumerator", "ERROR: ENUM( %# = \"%#:%#\" ) on '%#' - '%#' is still an enumerator; an old name must be one that no longer exists.", pKey,
                                     oldName, targetName, enumInfo._fullyQualifiedName, pCurrent->_name );
                    bValid = false;
                }
                for ( const string_view seen : listSeenOld )
                {
                    if ( StringUtil::equals( seen, oldName, true ) )
                    {
                        SW_LOG_ERROR_ID( "AstVisitor.ERRORENUMDeclaresOld", "ERROR: ENUM() on '%#' declares the old enumerator name '%#' twice (ValueAlias and Removed together count once).",
                                         enumInfo._fullyQualifiedName, oldName );
                        bValid = false;
                    }
                }
                listSeenOld.push_back( oldName );
            }
        }
        return bValid;
    }

    bool DeclarationValidator::applyDisplayMeta( ParsedPropertyInfo& prop, const CXType fieldType, const string_view owner )
    {
        // `Meta` 의 `Units` 는 철자 검사를 받지 않으므로 표에 없는 글자(`HP`)만 받는다 — 표에 있는 단위는 `Units = …` 로 적는다.
        for ( const auto& [key, value] : prop._listCustomMeta )
        {
            if ( key == "Units" && ReflectUnit::findUnit( value ) != nullptr )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORWritesMetaUnits", "ERROR: '%#' writes Meta = \"Units=%#\" - a known unit is written PROPERTY( Units = %# ).", owner, value, value );
                return false;
            }
        }

        if ( prop._units.empty() == false )
        {
            if ( ReflectUnit::findUnit( prop._units ) == nullptr )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYUnitsUnknown", "ERROR: PROPERTY( Units = %# ) on '%#' - unknown unit. Known: %#. A free-form label is written Meta = \"Units=…\".",
                                 prop._units, owner, ReflectUnit::makeUnitList() );
                return false;
            }
            for ( const auto& [key, value] : prop._listCustomMeta )
            {
                if ( key == "Units" )
                {
                    SW_LOG_ERROR_ID( "AstVisitor.ERRORGivesUnitsTwice", "ERROR: '%#' gives Units twice (Units = %# and Meta = \"Units=%#\").", owner, prop._units, value );
                    return false;
                }
            }
            prop._listCustomMeta.emplace_back( "Units", prop._units );
        }

        if ( prop._editCondition.empty() == false && isEditConditionWellFormed( prop._editCondition ) == false )
        {
            SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYEditConditionUse", "ERROR: PROPERTY( EditCondition = \"%#\" ) on '%#' - use name, !name, name == Value or name != Value.", prop._editCondition, owner );
            return false;
        }

        const bool bFixedArrayOfContainers = fieldType.kind == CXType_ConstantArray && prop._containerTree != nullptr && prop._containerTree->_elementNested != nullptr;
        if ( bFixedArrayOfContainers )
        {
            SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYFixedArray", "ERROR: PROPERTY() on '%#' is a fixed array of containers - use a container of containers (vector<array<…>>) instead.", owner );
            return false;
        }
        return true;
    }

    bool DeclarationValidator::isIdentifier( const string_view text ) noexcept
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

    bool DeclarationValidator::isEditConditionWellFormed( const string_view text )
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

    bool DeclarationValidator::isInterpAllowed( const ParsedPropertyInfo& prop, const string_view owner )
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
        SW_LOG_ERROR_ID( "AstVisitor.ERRORPROPERTYInterpOnly", "ERROR: PROPERTY( Interp ) on '%#' (%#) - only a single number, float2/3/4 or quaternion can be animated by a value track.",
                         owner, prop._typeName );
        return false;
    }

    bool DeclarationValidator::validateBusBodyExport( const ParsedTypeInfo& type, const bool bBusBody, const bool bExported, const bool bModuleLocal )
    {
        const bool bMissingExport = bBusBody && bExported == false;
        if ( bMissingExport == false || bModuleLocal )
            return true;
        SW_LOG_ERROR_ID( "AstVisitor.ERRORDeclaresServerBus", "ERROR: '%#' declares '%#' (a server bus body type) but has no module export macro. "
                                                              "Another module's ServerBusTopic<T> calls T::StaticType(), which then fails to link. "
                                                              "Write the module's macro before the name: 'struct SW_KIT_API %#' (SW_GF_API in Base, SW_KIT_SERVER_API in a server kit).",
                         type._fullyQualifiedName, annotation::kBusVersionMember, type._name );
        return false;
    }

    bool DeclarationValidator::isInsideReflectType( const CXCursor member, const utf8* pMacroName, const ParserConfig& config )
    {
        const CXCursor parent = clang_getCursorSemanticParent( member );
        if ( AnnotationLookup::hasAnnotation( parent, annotation::kReflect, config ) )
            return true;

        SW_LOG_ERROR_ID( "AstVisitor.ERRORUsedClassStruct", "ERROR: %#() is used in class/struct '%#', but it lacks REFLECT()!", pMacroName,
                         CursorReader::makeFullyQualifiedName( parent ).c_str() );
        return false;
    }
} // namespace sw
