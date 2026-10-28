#include "pch.h"

#include "Engine/Reflection/PropertyEditCondition.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/ReflectValue.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    namespace
    {
        /** @brief 보는 프로퍼티의 값을 정수로 읽습니다(bool · 비트필드 · 정수 · 열거형). 읽을 수 없는 타입이면 false 입니다. */
        [[nodiscard]] bool readSubjectValue( const PropertyEditConditionExpr& expr, const ObjectAddress instance, int64& outValue )
        {
            const PropertyInfo& subject = *expr._pSubject;
            if ( subject._bIsBitField == SW_TRUE )
            {
                outValue = subject.getValue<bool>( instance.get() ) ? 1 : 0;
                return true;
            }
            const void* pValue = subject.getRawPtr( instance.get() );
            if ( expr._pEnum != nullptr )
            {
                outValue = expr._pEnum->readValueFromMemory( pValue );
                return true;
            }
            // 정수 · bool 판정은 내장 타입 표 한 곳(`ReflectValueUtil::readInteger`)이 한다 — 이름 목록을 여기 두지 않는다.
            return ReflectValueUtil::readInteger( ReflectValueUtil::findBuiltinIndex( subject._typeName ), pValue, outValue );
        }
    } // namespace
} // namespace sw

namespace sw
{
    bool PropertyEditCondition::parse( const TypeInfo& type, const PropertyInfo& prop, PropertyEditConditionExpr& outExpr, string& outError )
    {
        outExpr = PropertyEditConditionExpr{};
        outError.clear();
#if defined( SW_SHIPPING )
        (void)type;
        (void)prop;
        return true;
#else
        string_view text = StringUtil::trim( prop._metadata->_editCondition );
        if ( text.empty() )
            return true;

        string_view  subjectName = text;
        string_view  valueText;
        const size_t equalPos    = text.find( "==" );
        const size_t notEqualPos = text.find( "!=" );
        if ( equalPos != string_view::npos || notEqualPos != string_view::npos )
        {
            const size_t opPos = ( equalPos != string_view::npos ) ? equalPos : notEqualPos;
            outExpr._bCompare  = true;
            outExpr._bNegate   = ( opPos == notEqualPos );
            subjectName        = StringUtil::trim( text.substr( 0, opPos ) );
            valueText          = StringUtil::trim( text.substr( opPos + 2 ) );
            // 열거자는 한정해 적어도 된다(`Mode::Orbit`) — 끝 이름으로 찾는다.
            const size_t scopePos = valueText.rfind( "::" );
            if ( scopePos != string_view::npos )
                valueText = valueText.substr( scopePos + 2 );
        }
        else if ( text.front() == '!' )
        {
            outExpr._bNegate = true;
            subjectName      = StringUtil::trim( text.substr( 1 ) );
        }

        if ( subjectName.empty() || ( outExpr._bCompare && valueText.empty() ) )
        {
            outError = "malformed EditCondition '" + string( text ) + "' (use name, !name, name == Value or name != Value)";
            return false;
        }

        outExpr._pSubject = type.findPropertyInHierarchy( hashed_string( subjectName.data(), static_cast<uint32>( subjectName.size() ) ) );
        if ( outExpr._pSubject == nullptr )
        {
            outError = "EditCondition names '" + string( subjectName ) + "', which is not a property of " + type._fullyQualifiedName.c_str();
            return false;
        }

        outExpr._pEnum = engine::getSubsystem<TypeRegistry>().findEnum( outExpr._pSubject->_typeName );
        if ( outExpr._bCompare )
        {
            const bool bParsed = ( outExpr._pEnum != nullptr ) ? outExpr._pEnum->tryParseText( valueText, outExpr._compareValue )
                                                               : StringUtil::parseInt64( valueText, outExpr._compareValue );
            if ( bParsed == false )
            {
                outError          = "EditCondition value '" + string( valueText ) + "' is not a value of " + outExpr._pSubject->_typeName.c_str();
                outExpr._pSubject = nullptr;
                return false;
            }
        }
        return true;
#endif
    }

    bool PropertyEditCondition::evaluate( const PropertyEditConditionExpr& expr, const ObjectAddress instance )
    {
        if ( expr._pSubject == nullptr || instance.get() == nullptr )
            return true;
        int64 value{ 0 };
        if ( readSubjectValue( expr, instance, value ) == false )
            return true;
        const bool bHolds = expr._bCompare ? ( value == expr._compareValue ) : ( value != 0 );
        return expr._bNegate ? ( bHolds == false ) : bHolds;
    }

    PropertyEditState PropertyEditCondition::getEditState( const TypeInfo& type, const PropertyInfo& prop, const ObjectAddress instance )
    {
#if defined( SW_SHIPPING )
        (void)type;
        (void)prop;
        (void)instance;
        return PropertyEditState::Enabled;
#else
        if ( prop._metadata->_editCondition.empty() )
            return PropertyEditState::Enabled;
        PropertyEditConditionExpr expr;
        string                    error;
        if ( parse( type, prop, expr, error ) == false || evaluate( expr, instance ) )
            return PropertyEditState::Enabled;
        return prop._metadata->_bEditConditionHides == SW_TRUE ? PropertyEditState::Hidden : PropertyEditState::Disabled;
#endif
    }
} // namespace sw
