#include "pch.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

#include "Engine/Curve/FloatCurve.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    namespace
    {
        struct GameDataXMLInternal
        {
            static constexpr const utf8* kArrCurveChild[]        = { "Key" };
            static constexpr const utf8* kArrCurveKeyAttribute[] = { "time", "value", "interpolation" };
        };
    } // namespace
} // namespace sw

namespace sw
{
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
                SW_LOG_ERROR_ID( "GameData.CurveKeyBelowMin", "%#: <Key time=\"%#\" value=\"%#\"> is below %#", sourceName, *time, *value, minValue );
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
                    SW_LOG_ERROR_ID( "GameData.CurveInterpolationUnknown", "%#: <Key> has unknown interpolation '%#'", sourceName, interpolationText );
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
