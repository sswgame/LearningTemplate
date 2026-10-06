#include "pch.h"

#include "GameFramework/Kits/Online/Leaderboard/LeaderboardTypes.h"

namespace sw
{
    const utf8* toString( LeaderboardResult result )
    {
        static constexpr const utf8* kArrName[] = { "Ok", "NotSignedIn", "Invalid", "UnknownBoard", "NotAllowed", "NotRanked", "Unavailable", "Conflict" };
        static_assert( SW_COUNT_OF( kArrName ) == static_cast<size_t>( LeaderboardResult::Count ), "LeaderboardResult names must match the enum" );
        const size_t index = static_cast<size_t>( result );
        return index < SW_COUNT_OF( kArrName ) ? kArrName[index] : "Unknown";
    }

    bool LeaderboardNameRule::isValidId( string_view id )
    {
        if ( id.empty() || id.size() > static_cast<size_t>( LeaderboardLimit::kMaxIdSize ) )
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
