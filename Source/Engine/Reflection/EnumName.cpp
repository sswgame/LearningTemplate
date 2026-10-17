#include "pch.h"

#include "Engine/Reflection/EnumName.h"

#include "Engine/Common/EngineServices.h"

namespace sw
{
    const utf8* getEnumName( const hashed_string& enumTypeFqn, int64 value )
    {
        const utf8* pName = findEnumName( enumTypeFqn, value );
        return pName != nullptr ? pName : EnumNameCache::kUnknownName;
    }

    const utf8* findEnumName( const hashed_string& enumTypeFqn, int64 value )
    {
        if ( engine::areEngineServicesBound() == false )
            return nullptr;
        const EnumInfo* pInfo = engine::getSubsystem<TypeRegistry>().findEnumByTypeFqn( enumTypeFqn );
        return pInfo != nullptr ? pInfo->valueToCString( value ) : nullptr;
    }

    const EnumInfo* EnumNameCache::find( const hashed_string& enumTypeFqn ) const
    {
        if ( engine::areEngineServicesBound() == false )
            return nullptr;
        // 세대는 찾기 **전에** 읽는다. 찾는 사이 등록이나 해제가 끼면 옛 세대가 남아 다음 호출이 다시 찾는다.
        const uint32    generation = TypeRegistry::getGeneration();
        const EnumInfo* pCached    = nullptr;
        if ( _slot.tryGet( generation, pCached ) )
            return pCached;
        const EnumInfo* pInfo = engine::getSubsystem<TypeRegistry>().findEnumByTypeFqn( enumTypeFqn );
        _slot.set( generation, pInfo );
        return pInfo;
    }
} // namespace sw
