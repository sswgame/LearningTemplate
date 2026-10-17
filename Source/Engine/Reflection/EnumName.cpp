#include "pch.h"

#include "Engine/Reflection/EnumName.h"

#include "Engine/Common/EngineServices.h"

namespace sw
{
    const utf8* getEnumName( const hashed_string& enumTypeFqn, int64 value )
    {
        const utf8* pName = findEnumName( enumTypeFqn, value );
        return pName != nullptr ? pName : "Unknown";
    }

    const utf8* findEnumName( const hashed_string& enumTypeFqn, int64 value )
    {
        if ( engine::areEngineServicesBound() == false )
            return nullptr;
        const EnumInfo* pInfo = engine::getSubsystem<TypeRegistry>().findEnumByTypeFqn( enumTypeFqn );
        return pInfo != nullptr ? pInfo->valueToCString( value ) : nullptr;
    }
} // namespace sw
