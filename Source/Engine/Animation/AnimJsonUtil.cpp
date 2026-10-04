#include "pch.h"

#include "Engine/Animation/AnimJsonUtil.h"

#include "Core/Log/Logger.h"

#include "Engine/Animation/Pose.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
    SW_LOG_CALLER( "AnimJsonUtil" );

    bool AnimJsonUtil::hasOnlyKnownKeys( const JsonValue& object, std::initializer_list<string_view> listKnownKey, string_view context )
    {
        if ( object.isObject() == false )
        {
            SW_LOG_ERROR( "%#: expected a JSON object", context );
            return false;
        }
        bool bKnown = true;
        for ( const string& memberName : object.getMemberNames() )
        {
            const bool bFound = std::find( listKnownKey.begin(), listKnownKey.end(), string_view( memberName ) ) != listKnownKey.end();
            if ( bFound )
                continue;
            SW_LOG_ERROR( "%#: unknown key '%#'", context, memberName.c_str() );
            bKnown = false;
        }
        return bKnown;
    }

    bool AnimJsonUtil::readFloats( const JsonValue& value, float32* pOutValue, uint32 count )
    {
        if ( value.isArray() == false || value.size() != count )
            return false;
        for ( uint32 index = 0; index < count; ++index )
        {
            const JsonValue element = value.at( index );
            if ( element.isNumber() == false )
                return false;
            pOutValue[index] = static_cast<float32>( element.asFloat() );
        }
        return true;
    }

    void AnimJsonUtil::writeFloats( const JsonValue& value, const float32* pValue, uint32 count )
    {
        value.setArray();
        for ( uint32 index = 0; index < count; ++index )
            value.pushBack().setFloat( static_cast<float64>( pValue[index] ) );
    }

    bool AnimJsonUtil::readBoneTransform( const JsonValue& object, BoneTransform& outTransform )
    {
        float32    arrTranslation[3]{};
        float32    arrRotation[4]{};
        float32    arrScale[3]{};
        const bool bRead = readFloats( object.get( "translation" ), arrTranslation, 3 ) && readFloats( object.get( "rotation" ), arrRotation, 4 ) &&
                           readFloats( object.get( "scale" ), arrScale, 3 );
        if ( bRead == false )
            return false;
        outTransform._translation = float3{ arrTranslation };
        outTransform._rotation    = quaternion{ arrRotation }.normalize();
        outTransform._scale       = float3{ arrScale };
        return true;
    }

    void AnimJsonUtil::writeBoneTransform( const JsonValue& object, const BoneTransform& transform )
    {
        const float32 arrTranslation[3] = { transform._translation._x, transform._translation._y, transform._translation._z };
        const float32 arrRotation[4]    = { transform._rotation._x, transform._rotation._y, transform._rotation._z, transform._rotation._w };
        const float32 arrScale[3]       = { transform._scale._x, transform._scale._y, transform._scale._z };
        writeFloats( object.set( "translation" ), arrTranslation, 3 );
        writeFloats( object.set( "rotation" ), arrRotation, 4 );
        writeFloats( object.set( "scale" ), arrScale, 3 );
    }
} // namespace sw
