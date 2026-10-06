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
        }
        return "Unknown";
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
