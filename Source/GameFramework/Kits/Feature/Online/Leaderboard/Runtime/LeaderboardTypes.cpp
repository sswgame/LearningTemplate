#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Runtime/LeaderboardTypes.h"

namespace sw
{
    bool LeaderboardNameRule::isValidID( string_view id )
    {
        if ( id.empty() || id.size() > static_cast<size_t>( LeaderboardLimit::kMaxIDSize ) )
            return false;
        for ( const utf8 character : id )
        {
            const bool bAllowed = ( 'a' <= character && character <= 'z' ) || ( '0' <= character && character <= '9' ) || character == '_';
            if ( bAllowed == false )
                return false;
        }
        return true;
    }
} // namespace sw
