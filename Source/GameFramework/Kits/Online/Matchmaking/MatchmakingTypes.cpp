#include "pch.h"

#include "GameFramework/Kits/Online/Matchmaking/MatchmakingTypes.h"

namespace sw
{
    const utf8* toString( MatchmakingResult result )
    {
        switch ( result )
        {
            case MatchmakingResult::Ok:
                return "Ok";
            case MatchmakingResult::UnknownMode:
                return "UnknownMode";
            case MatchmakingResult::AlreadyQueued:
                return "AlreadyQueued";
            case MatchmakingResult::NotQueued:
                return "NotQueued";
            case MatchmakingResult::PartyTooLarge:
                return "PartyTooLarge";
            case MatchmakingResult::NotPartyLeader:
                return "NotPartyLeader";
            case MatchmakingResult::PartyFull:
                return "PartyFull";
            case MatchmakingResult::NotInParty:
                return "NotInParty";
            case MatchmakingResult::AlreadyInParty:
                return "AlreadyInParty";
            case MatchmakingResult::LobbyFull:
                return "LobbyFull";
            case MatchmakingResult::NotInLobby:
                return "NotInLobby";
            case MatchmakingResult::NotLobbyOwner:
                return "NotLobbyOwner";
            case MatchmakingResult::LobbyNotReady:
                return "LobbyNotReady";
            case MatchmakingResult::InviteMissing:
                return "InviteMissing";
            case MatchmakingResult::NoServer:
                return "NoServer";
            case MatchmakingResult::Invalid:
                return "Invalid";
            case MatchmakingResult::Unavailable:
                return "Unavailable";
            case MatchmakingResult::Conflict:
                return "Conflict";
            case MatchmakingResult::Count:
                break;
        }
        return "Unknown";
    }

    bool isValidMatchModeId( string_view modeId )
    {
        if ( modeId.empty() || modeId.size() > static_cast<size_t>( MatchmakingLimit::kMaxIdSize ) )
            return false;
        for ( const utf8 ch : modeId )
        {
            const bool bAllowed = ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ) || ch == '_';
            if ( bAllowed == false )
                return false;
        }
        return true;
    }

    bool PartySnapshot::hasMember( AccountId accountId ) const
    {
        for ( const AccountId memberId : _listMemberId )
        {
            if ( memberId == accountId )
                return true;
        }
        return false;
    }

    bool LobbySnapshot::hasMember( AccountId accountId ) const
    {
        for ( const LobbyMember& member : _listMember )
        {
            if ( member._accountId == accountId )
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
            ratingSum += member._rating;
        return static_cast<int32>( ratingSum / static_cast<int64>( _listMember.size() ) );
    }
} // namespace sw
