#include "pch.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Serialization/Base/SerializeContext.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"
#include "Engine/Utility/FloatCurve.h"

namespace sw
{
    SW_LOG_CALLER( "GameDataXML" );

    namespace
    {
        struct GameDataXMLInternal
        {
            static constexpr const utf8* kArrCurveChild[]        = { "Key" };
            static constexpr const utf8* kArrCurveKeyAttribute[] = { "time", "value", "interpolation" };

            /** @brief PROPERTY 이름의 데이터 속성 철자입니다(`readFields` 의 규칙 — 앞의 `_` 를 떼고, 불리언 `b` 접두는 떼어 첫 글자를 소문자로). */
            static string makeAttributeName( const PropertyInfo& prop )
            {
                string_view name = prop._name.view();
                if ( name.empty() == false && name.front() == '_' )
                    name.remove_prefix( 1 );
                const bool bBooleanPrefix = name.size() > 1 && name[0] == 'b' && 'A' <= name[1] && name[1] <= 'Z';
                if ( bBooleanPrefix == false )
                    return string( name );
                string attribute( name.substr( 1 ) );
                attribute[0] = static_cast<utf8>( attribute[0] - 'A' + 'a' );
                return attribute;
            }

            static bool isHandRead( string_view attribute, std::initializer_list<string_view> listHandRead )
            {
                return VectorUtil::contains( listHandRead, attribute );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool GameDataXML::readFields( const XMLNode& node, void* pInstance, const TypeInfo& typeInfo, string_view sourceName, std::initializer_list<string_view> listHandRead,
                                  AcceptAttributeFunction pfnHandRead )
    {
        using Internal = GameDataXMLInternal;
        // 오류 줄에 원소 이름과 id 를 함께 적는다(같은 원소가 여럿이면 어느 것인지 알아야 고친다).
        const utf8* const pID = node.findAttribute( "id" );
        string            element( node.getName() != nullptr ? node.getName() : "?" );
        if ( pID != nullptr )
            element += string( " id=\"" ) + pID + "\"";
        const utf8* const       pElement     = element.c_str();
        const SerializeContext& context      = SerializeContext::getDefault( SerializePurpose::Authoring );
        const auto&             listProperty = typeInfo.getPropertiesWithBase();
        bool                    bOk          = true;

        // 1) 적힌 속성마다 칸을 찾아 읽는다. 모르는 이름은 오타다 — 조용히 넘기면 그 칸은 기본값으로 남는다.
        for ( XMLAttribute attribute = node.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
        {
            const string_view attributeName{ attribute.getName() != nullptr ? attribute.getName() : "" };
            const bool        bHandRead = Internal::isHandRead( attributeName, listHandRead ) || ( pfnHandRead != nullptr && pfnHandRead( attributeName.data() ) );
            if ( bHandRead )
                continue;
            const PropertyInfo* pFound = nullptr;
            for ( const PropertyInfo& prop : listProperty )
            {
                if ( Internal::makeAttributeName( prop ) == attributeName )
                    pFound = &prop;
            }
            if ( pFound == nullptr )
            {
                SW_LOG_ERROR_ID( "GameData.UnknownField", "%#: <%# %#=...> is not a field of %#", sourceName, pElement, attributeName, typeInfo._name.c_str() );
                bOk = false;
                continue;
            }
            const utf8* const pValue = attribute.getValue() != nullptr ? attribute.getValue() : "";
            if ( SerializerUtil::applyPropertyText( *pFound, MutableObjectAddress{ pInstance }, pValue, context ) == false )
            {
                SW_LOG_ERROR_ID( "GameData.ValueUnreadable", "%#: <%# %#=\"%#\"> cannot be read as %#", sourceName, pElement, attributeName, pValue, pFound->_typeName.c_str() );
                bOk = false;
            }
        }

        // 2) 필수 칸이 빠졌는지 본다. 초기값은 형식일 뿐이라 대신 쓰지 않는다.
        for ( const PropertyInfo& prop : listProperty )
        {
            if ( prop._metadata->_bRequired == SW_FALSE )
                continue;
            const string attributeName = Internal::makeAttributeName( prop );
            if ( node.findAttribute( attributeName.c_str() ) == nullptr )
            {
                SW_LOG_ERROR_ID( "GameData.RequiredFieldMissing", "%#: <%#> needs %#=\"...\" (%#)", sourceName, pElement, attributeName.c_str(), typeInfo._name.c_str() );
                bOk = false;
            }
        }

        // 3) 범위는 자르지 않고 알린다 — 잘라 쓰면 데이터의 실수가 다른 값으로 조용히 돈다. 범위의 원본은 `PROPERTY( Min · Max )` 하나이고,
        //    바이너리 · JSON · XML 과 같은 읽기 공통 끝이 칸마다 알린다. 여기서는 어느 파일 · 원소인지만 덧붙인다.
        if ( SerializerUtil::isWithinDeclaredRanges( typeInfo, ObjectAddress{ pInstance } ) == false )
        {
            SW_LOG_ERROR_ID( "GameData.ValueOutOfRange", "%#: <%#> has values outside PROPERTY( Min, Max ) of %# (listed above)", sourceName, pElement, typeInfo._name.c_str() );
            bOk = false;
        }
        return bOk;
    }

    bool GameDataXML::readFloatCurve( const XMLNode& parent, const utf8* pElement, float32 minValue, FloatCurve& outCurve, string_view sourceName )
    {
        outCurve._listKey.clear();
        const XMLNode curveNode = parent.findChild( pElement );
        if ( curveNode.isValid() == false )
            return true;

        bool bValid = XMLNameCheck::reportUnknownChildren( curveNode, GameDataXMLInternal::kArrCurveChild, sourceName );
        for ( XMLNode keyNode = curveNode.findChild( "Key" ); keyNode; keyNode = keyNode.findNextSibling( "Key" ) )
        {
            bValid = XMLNameCheck::reportUnknownAttributes( keyNode, GameDataXMLInternal::kArrCurveKeyAttribute, sourceName ) && bValid;
            // 키의 시간과 값은 필수다. 값은 @p minValue 아래면 자르지 않고 오류다.
            const std::optional<float32> time  = keyNode.readRequiredAttribute<float32>( "time" );
            const std::optional<float32> value = keyNode.readRequiredAttribute<float32>( "value" );
            if ( time.has_value() == false || value.has_value() == false )
            {
                bValid = false;
                continue;
            }
            if ( *value < minValue )
            {
                SW_LOG_ERROR( "%#: <Key time=\"%#\" value=\"%#\"> is below %#", sourceName, *time, *value, minValue );
                bValid = false;
                continue;
            }
            FloatCurveKey key{};
            key._time                           = *time;
            key._value                          = *value;
            key._interpolation                  = CurveInterpolation::Linear;
            const string_view interpolationText = keyNode.getAttributeText( "interpolation" );
            if ( interpolationText.empty() == false )
            {
                if ( StringUtil::equals( interpolationText, "linear" ) )
                    key._interpolation = CurveInterpolation::Linear;
                else if ( StringUtil::equals( interpolationText, "constant" ) )
                    key._interpolation = CurveInterpolation::Constant;
                else if ( StringUtil::equals( interpolationText, "cubic" ) )
                    key._interpolation = CurveInterpolation::Cubic;
                else
                {
                    SW_LOG_ERROR( "%#: <Key> has unknown interpolation '%#'", sourceName, interpolationText );
                    bValid = false;
                    continue;
                }
            }
            outCurve._listKey.push_back( key );
        }
        outCurve.sortAndComputeAutoTangents();
        return bValid;
    }
} // namespace sw
