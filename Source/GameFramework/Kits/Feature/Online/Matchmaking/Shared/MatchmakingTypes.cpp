#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Shared/MatchmakingTypes.h"

namespace sw
{
    bool isValidMatchModeID( string_view modeID )
    {
        if ( modeID.empty() || modeID.size() > static_cast<size_t>( MatchmakingLimit::kMaxIDSize ) )
            return false;
        for ( const utf8 ch : modeID )
        {
            const bool bAllowed = ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ) || ch == '_';
            if ( bAllowed == false )
                return false;
        }
        return true;
    }

    bool PartySnapshot::hasMember( AccountID accountID ) const
    {
        for ( const AccountID memberID : _listMemberID )
        {
            if ( memberID == accountID )
                return true;
        }
        return false;
    }

    bool LobbySnapshot::hasMember( AccountID accountID ) const
    {
        for ( const LobbyMember& member : _listMember )
        {
            if ( member._accountID == accountID )
                return true;
        }
        return false;
    }

    bool LobbySnapshot::isEveryoneReady() const
    {
        if ( _listMember.size() < 2 )
            return false;
        for ( size_t index = 1; index < _listMember.size(); ++index )
        {
            if ( _listMember[index]._bReady == SW_FALSE )
                return false;
        }
        return true;
    }

    int32 MatchTicket::computeAverageRating() const
    {
        if ( _listMember.empty() )
            return 0;
        int64 ratingSum = 0;
        for ( const MatchMember& member : _listMember )
        {
            ratingSum += member._rating;
        }
        return static_cast<int32>( ratingSum / static_cast<int64>( _listMember.size() ) );
    }
} // namespace sw
