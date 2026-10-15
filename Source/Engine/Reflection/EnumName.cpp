#include "pch.h"

#include "Engine/Reflection/EnumName.h"

#include "Engine/Common/EngineServices.h"

namespace sw
{
    const utf8* getEnumName( const hashed_string& enumTypeFqn, int64 value )
    {
        if ( engine::areEngineServicesBound() == false )
            return "Unknown";
        const EnumInfo* pInfo = engine::getSubsystem<TypeRegistry>().findEnumByTypeFqn( enumTypeFqn );
        const utf8*     pName = pInfo != nullptr ? pInfo->valueToCString( value ) : nullptr;
        return pName != nullptr ? pName : "Unknown";
    }
} // namespace sw
