#include "pch.h"

#include "Engine/Animation/AnimationJSONUtil.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    SW_LOG_CALLER( "AnimationJSONUtil" );

    bool AnimationJSONUtil::hasOnlyKnownKeys( const JSONValue& object, std::initializer_list<string_view> listKnownKey, string_view context )
    {
        if ( object.isObject() == false )
        {
            SW_LOG_ERROR( "%#: expected a JSON object", context );
            return false;
        }
        bool bKnown = true;
        for ( const string& memberName : object.getMemberNames() )
        {
            const bool bFound = VectorUtil::contains( listKnownKey, string_view( memberName ) );
            if ( bFound )
                continue;
            SW_LOG_ERROR( "%#: unknown key '%#'", context, memberName.c_str() );
            bKnown = false;
        }
        return bKnown;
    }

    bool AnimationJSONUtil::readFloats( const JSONValue& value, float32* pOutValue, uint32 count )
    {
        if ( value.isArray() == false || value.size() != count )
            return false;
        for ( uint32 index = 0; index < count; ++index )
        {
            const JSONValue element = value.at( index );
            if ( element.isNumber() == false )
                return false;
            const std::optional<float32> number = element.read<float32>();
            if ( number.has_value() == false )
                return false;
            pOutValue[index] = *number;
        }
        return true;
    }

    void AnimationJSONUtil::writeFloats( const JSONValue& value, const float32* pValue, uint32 count )
    {
        value.setArray();
        for ( uint32 index = 0; index < count; ++index )
        {
            value.pushBack().setFloat( static_cast<float64>( pValue[index] ) );
        }
    }
} // namespace sw
