#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Shared/MatchmakingClient.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    MatchmakingClient::MatchmakingClient()
        : _partyBuffer{}
        , _inviteBuffer{}
        , _lobbyBuffer{}
        , _matchBuffer{}
        , _party{}
    {
    }

    uint64 MatchmakingClient::createParty( const ReplyDelegate& onReply )
    {
        return sendCall( MatchmakingMethod::kPartyCreate, BitWriter{}, NetIdempotencyKey{}, onReply );
    }

    uint64 MatchmakingClient::inviteToParty( AccountID targetID, const ReplyDelegate& onReply )
    {
        BitWriter body;
        body.writeVarUint( targetID );
        return sendCall( MatchmakingMethod::kPartyInvite, body, NetIdempotencyKey{}, onReply );
    }

    uint64 MatchmakingClient::acceptPartyInvite( uint64 partyID, const ReplyDelegate& onReply )
    {
        BitWriter body;
        body.writeVarUint( partyID );
        return sendCall( MatchmakingMethod::kPartyAccept, body, NetIdempotencyKey{}, onReply );
    }

    uint64 MatchmakingClient::leaveParty( const ReplyDelegate& onReply )
    {
        return sendCall( MatchmakingMethod::kPartyLeave, BitWriter{}, NetIdempotencyKey{}, onReply );
    }

    uint64 MatchmakingClient::kickFromParty( AccountID targetID, const ReplyDelegate& onReply )
    {
        BitWriter body;
        body.writeVarUint( targetID );
        return sendCall( MatchmakingMethod::kPartyKick, body, NetIdempotencyKey{}, onReply );
    }

    uint64 MatchmakingClient::createLobby( const LobbySnapshot& request, const ReplyDelegate& onReply )
    {
        BitWriter body;
        MatchmakingProtocol::writeLobby( body, request );
        return sendCall( MatchmakingMethod::kLobbyCreate, body, NetIdempotencyKey{}, onReply );
    }

    uint64 MatchmakingClient::listLobbies( string_view modeID, const ReplyDelegate& onReply )
    {
        BitWriter body;
        ServiceKeyUtil::writeString( body, modeID );
        return sendCall( MatchmakingMethod::kLobbyList, body, NetIdempotencyKey{}, onReply );
    }

    uint64 MatchmakingClient::joinLobby( uint64 lobbyID, const ReplyDelegate& onReply )
    {
        BitWriter body;
        body.writeVarUint( lobbyID );
        return sendCall( MatchmakingMethod::kLobbyJoin, body, NetIdempotencyKey{}, onReply );
    }

    uint64 MatchmakingClient::leaveLobby( uint64 lobbyID, const ReplyDelegate& onReply )
    {
        BitWriter body;
        body.writeVarUint( lobbyID );
        return sendCall( MatchmakingMethod::kLobbyLeave, body, NetIdempotencyKey{}, onReply );
    }

    uint64 MatchmakingClient::setLobbyReady( uint64 lobbyID, bool bReady, const ReplyDelegate& onReply )
    {
        BitWriter body;
        body.writeVarUint( lobbyID );
        body.writeBool( bReady );
        return sendCall( MatchmakingMethod::kLobbyReady, body, NetIdempotencyKey{}, onReply );
    }

    uint64 MatchmakingClient::startLobby( uint64 lobbyID, const ReplyDelegate& onReply )
    {
        BitWriter body;
        body.writeVarUint( lobbyID );
        return sendCall( MatchmakingMethod::kLobbyStart, body, NetIdempotencyKey{}, onReply );
    }

    uint64 MatchmakingClient::joinQueue( string_view modeID, string_view region, const ReplyDelegate& onReply )
    {
        BitWriter body;
        ServiceKeyUtil::writeString( body, modeID );
        ServiceKeyUtil::writeString( body, region );
        return sendCall( MatchmakingMethod::kQueueJoin, body, NetIdempotencyKey{}, onReply );
    }

    uint64 MatchmakingClient::leaveQueue( const ReplyDelegate& onReply )
    {
        return sendCall( MatchmakingMethod::kQueueLeave, BitWriter{}, NetIdempotencyKey{}, onReply );
    }

    void MatchmakingClient::onServicePush( uint16 kind, BitReader& body )
    {
        switch ( kind )
        {
            case MatchmakingMethod::kPushParty:
            {
                PartySnapshot party;
                if ( MatchmakingProtocol::readParty( body, party ) == false )
                    return;
                _party = party;
                _partyBuffer.push( std::move( party ) );
                break;
            }
            case MatchmakingMethod::kPushPartyInvite:
            {
                PartyInvite invite;
                if ( MatchmakingProtocol::readInvite( body, invite ) )
                    _inviteBuffer.push( invite );
                break;
            }
            case MatchmakingMethod::kPushLobby:
            {
                LobbySnapshot lobby;
                if ( MatchmakingProtocol::readLobby( body, lobby ) )
                    _lobbyBuffer.push( std::move( lobby ) );
                break;
            }
            case MatchmakingMethod::kPushMatch:
            {
                MatchAssignment assignment;
                if ( MatchmakingProtocol::readAssignment( body, assignment ) )
                    _matchBuffer.push( std::move( assignment ) );
                break;
            }
            default:
            {
                break;
            }
        }
    }
} // namespace sw
