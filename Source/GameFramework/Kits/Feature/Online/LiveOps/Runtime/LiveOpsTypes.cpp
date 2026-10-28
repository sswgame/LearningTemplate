#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Runtime/LiveOpsTypes.h"

namespace sw
{
    bool PushLimit::isValidProviderID( string_view providerID )
    {
        if ( providerID.empty() || providerID.size() > static_cast<size_t>( kMaxProviderIDSize ) )
            return false;
        for ( const utf8 ch : providerID )
        {
            const bool bAllowed = ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ) || ch == '_';
            if ( bAllowed == false )
                return false;
        }
        return true;
    }

    bool LiveOpsLimit::isValidKey( string_view key, int32 maxSize )
    {
        if ( key.empty() || key.size() > static_cast<size_t>( maxSize ) )
            return false;
        for ( const utf8 ch : key )
        {
            const bool bAllowed = ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ) || ch == '_' || ch == '.';
            if ( bAllowed == false )
                return false;
        }
        return true;
    }
} // namespace sw
