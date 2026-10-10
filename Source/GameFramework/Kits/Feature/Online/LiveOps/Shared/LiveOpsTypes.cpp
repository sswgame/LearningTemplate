#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Shared/LiveOpsTypes.h"

namespace sw
{
    const utf8* toString( LiveOpsResult result )
    {
        switch ( result )
        {
            case LiveOpsResult::Ok:
                return "Ok";
            case LiveOpsResult::NotFound:
                return "NotFound";
            case LiveOpsResult::Invalid:
                return "Invalid";
            case LiveOpsResult::Unavailable:
                return "Unavailable";
            case LiveOpsResult::RateLimited:
                return "RateLimited";
            case LiveOpsResult::UnknownProvider:
                return "UnknownProvider";
            case LiveOpsResult::Count:
                break;
        }
        return "Unknown";
    }

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
