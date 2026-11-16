#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingClient.h"

#include "GameFramework/Runtime/Online/Service/OnlineClientRegistry.h"

namespace sw
{
    MatchmakingClient::MatchmakingClient()
        : _notificationConsumerLock{}
        , _partyBuffer{}
        , _inviteBuffer{}
        , _lobbyBuffer{}
        , _matchBuffer{}
        , _party{}
    {
    }

    void MatchmakingClient::onPush( MatchmakingMethod::Party, const PartySnapshot& body )
    {
        _party              = body;
        PartySnapshot party = body;
        _partyBuffer.push( std::move( party ) );
    }

    void MatchmakingClient::onPush( MatchmakingMethod::PartyInvited, const PartyInvite& body )
    {
        _inviteBuffer.push( body );
    }

    void MatchmakingClient::onPush( MatchmakingMethod::Lobby, const LobbySnapshot& body )
    {
        LobbySnapshot lobby = body;
        _lobbyBuffer.push( std::move( lobby ) );
    }

    void MatchmakingClient::onPush( MatchmakingMethod::Match, const MatchAssignment& body )
    {
        MatchAssignment assignment = body;
        _matchBuffer.push( std::move( assignment ) );
    }
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_CLIENT( MatchmakingClient );
} // namespace sw
