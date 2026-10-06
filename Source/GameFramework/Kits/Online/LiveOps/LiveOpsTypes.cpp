#include "pch.h"

#include "GameFramework/Kits/Online/LiveOps/LiveOpsTypes.h"

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
