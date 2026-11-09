#include "pch.h"

#include "Editor/Panels/Inspector/Model/PropertyEditConditionExpr.h"

#include "Core/String/StringUtil.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/ReflectionEditorMetadata.h"
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
            // 정수 · bool 판정은 내장 타입 표 한 곳(`ReflectedValueUtil::readInteger`)이 한다 — 이름 목록을 여기 두지 않는다.
            return ReflectedValueUtil::readInteger( ReflectedValueUtil::findBuiltinIndex( subject._typeName ), pValue, outValue ); // kNone 이면 false
        }
    } // namespace
} // namespace sw

namespace sw
{
    expected<PropertyEditConditionExpr> PropertyEditConditionExpr::parse( const TypeInfo& type, const PropertyInfo& prop )
    {
        PropertyEditConditionExpr expr;
        string_view               text = StringUtil::trim( prop._metadata->getEditor()._editCondition );
        if ( text.empty() )
            return expr;

        string_view  subjectName = text;
        string_view  valueText;
        const size_t equalPos    = text.find( "==" );
        const size_t notEqualPos = text.find( "!=" );
        if ( equalPos != string_view::npos || notEqualPos != string_view::npos )
        {
            const size_t opPos = ( equalPos != string_view::npos ) ? equalPos : notEqualPos;
            expr._bCompare     = true;
            expr._bNegate      = ( opPos == notEqualPos );
            subjectName        = StringUtil::trim( text.substr( 0, opPos ) );
            valueText          = StringUtil::trim( text.substr( opPos + 2 ) );
            // 열거자는 한정해 적어도 된다(`Mode::Orbit`) — 끝 이름으로 찾는다.
            const size_t scopePos = valueText.rfind( "::" );
            if ( scopePos != string_view::npos )
                valueText = valueText.substr( scopePos + 2 );
        }
        else if ( text.front() == '!' )
        {
            expr._bNegate = SW_TRUE;
            subjectName   = StringUtil::trim( text.substr( 1 ) );
        }

        if ( subjectName.empty() || ( expr._bCompare && valueText.empty() ) )
            return unexpected( Error{ ErrorKind::InvalidArgument, "malformed EditCondition (use name, !name, name == Value or name != Value)", text } );

        expr._pSubject = type.findPropertyInHierarchy( hashed_string( subjectName.data(), static_cast<uint32>( subjectName.size() ) ) );
        if ( expr._pSubject == nullptr )
        {
            return unexpected(
                Error{ ErrorKind::NotFound, "EditCondition names a property the type does not have", subjectName }.appendDetail( " in " ).appendDetail( type._fullyQualifiedName.c_str() ) );
        }

        expr._pEnum = editor::getSubsystem<TypeRegistry>().findEnum( expr._pSubject->_typeName );
        if ( expr._bCompare )
        {
            const bool bParsed = ( expr._pEnum != nullptr ) ? expr._pEnum->tryParseText( valueText, expr._compareValue )
                                                            : StringUtil::parseInt64( valueText, expr._compareValue );
            if ( bParsed == false )
            {
                return unexpected( Error{ ErrorKind::InvalidArgument, "the EditCondition value is not a value of the property's type", valueText }
                                       .appendDetail( " for " )
                                       .appendDetail( expr._pSubject->_typeName.c_str() ) );
            }
        }
        return expr;
    }

    bool PropertyEditConditionExpr::evaluate( const PropertyEditConditionExpr& expr, const ObjectAddress instance )
    {
        if ( expr._pSubject == nullptr || instance.get() == nullptr )
            return true;
        int64 value{ 0 };
        if ( readSubjectValue( expr, instance, value ) == false )
            return true;
        const bool bHolds = expr._bCompare ? ( value == expr._compareValue ) : ( value != 0 );
        return expr._bNegate ? ( bHolds == false ) : bHolds;
    }

    PropertyEditState PropertyEditConditionExpr::getEditState( const TypeInfo& type, const PropertyInfo& prop, const ObjectAddress instance )
    {
        if ( prop._metadata->getEditor()._editCondition.empty() )
            return PropertyEditState::Enabled;
        const expected<PropertyEditConditionExpr> expr = parse( type, prop );
        if ( expr.has_value() == false || evaluate( *expr, instance ) )
            return PropertyEditState::Enabled;
        return prop._metadata->getEditor()._bEditConditionHides == SW_TRUE ? PropertyEditState::Hidden : PropertyEditState::Disabled;
    }
} // namespace sw
