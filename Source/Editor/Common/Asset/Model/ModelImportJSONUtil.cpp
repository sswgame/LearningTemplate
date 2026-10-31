#include "pch.h"

#include "Editor/Common/Asset/Model/ModelImportJSONUtil.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    SW_LOG_CALLER( "ModelImportJSONUtil" );

    bool ModelImportJSONUtil::hasOnlyKnownKeys( const JSONValue& object, std::initializer_list<string_view> listKnownKey, string_view context )
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

    bool ModelImportJSONUtil::readFloats( const JSONValue& value, float32* pOutValue, uint32 count )
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
} // namespace sw
