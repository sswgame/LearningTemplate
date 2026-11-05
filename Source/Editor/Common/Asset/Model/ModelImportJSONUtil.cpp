#include "pch.h"

#include "Editor/Common/Asset/Model/ModelImportJSONUtil.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
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
