#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingClient.h"

#include "Core/Serialization/BitStream.h"

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
        return sendRequest( MatchmakingMethod::kPartyInvite, MatchmakingIDRequest{ targetID }, NetIdempotencyKey{}, onReply );
    }

    uint64 MatchmakingClient::acceptPartyInvite( uint64 partyID, const ReplyDelegate& onReply )
    {
        return sendRequest( MatchmakingMethod::kPartyAccept, MatchmakingIDRequest{ partyID }, NetIdempotencyKey{}, onReply );
    }

    uint64 MatchmakingClient::leaveParty( const ReplyDelegate& onReply )
    {
        return sendCall( MatchmakingMethod::kPartyLeave, BitWriter{}, NetIdempotencyKey{}, onReply );
    }

    uint64 MatchmakingClient::kickFromParty( AccountID targetID, const ReplyDelegate& onReply )
    {
        return sendRequest( MatchmakingMethod::kPartyKick, MatchmakingIDRequest{ targetID }, NetIdempotencyKey{}, onReply );
    }

    uint64 MatchmakingClient::createLobby( const LobbySnapshot& request, const ReplyDelegate& onReply )
    {
        return sendRequest( MatchmakingMethod::kLobbyCreate, request, NetIdempotencyKey{}, onReply );
    }

    uint64 MatchmakingClient::listLobbies( string_view modeID, const ReplyDelegate& onReply )
    {
        MatchmakingModeRequest request;
        request._modeID = string( modeID );
        return sendRequest( MatchmakingMethod::kLobbyList, request, NetIdempotencyKey{}, onReply );
    }

    uint64 MatchmakingClient::joinLobby( uint64 lobbyID, const ReplyDelegate& onReply )
    {
        return sendRequest( MatchmakingMethod::kLobbyJoin, MatchmakingIDRequest{ lobbyID }, NetIdempotencyKey{}, onReply );
    }

    uint64 MatchmakingClient::leaveLobby( uint64 lobbyID, const ReplyDelegate& onReply )
    {
        return sendRequest( MatchmakingMethod::kLobbyLeave, MatchmakingIDRequest{ lobbyID }, NetIdempotencyKey{}, onReply );
    }

    uint64 MatchmakingClient::setLobbyReady( uint64 lobbyID, bool bReady, const ReplyDelegate& onReply )
    {
        MatchmakingReadyRequest request;
        request._lobbyID = lobbyID;
        request._bReady  = bReady ? SW_TRUE : SW_FALSE;
        return sendRequest( MatchmakingMethod::kLobbyReady, request, NetIdempotencyKey{}, onReply );
    }

    uint64 MatchmakingClient::startLobby( uint64 lobbyID, const ReplyDelegate& onReply )
    {
        return sendRequest( MatchmakingMethod::kLobbyStart, MatchmakingIDRequest{ lobbyID }, NetIdempotencyKey{}, onReply );
    }

    uint64 MatchmakingClient::joinQueue( string_view modeID, string_view region, const ReplyDelegate& onReply )
    {
        MatchmakingModeRequest request;
        request._modeID = string( modeID );
        request._region = string( region );
        return sendRequest( MatchmakingMethod::kQueueJoin, request, NetIdempotencyKey{}, onReply );
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
                if ( MatchmakingProtocol::MessageTypes::read( body, party ) != FixedReadResult::Ok )
                    return;
                _party = party;
                _partyBuffer.push( std::move( party ) );
                break;
            }
            case MatchmakingMethod::kPushPartyInvite:
            {
                PartyInvite invite;
                if ( MatchmakingProtocol::MessageTypes::read( body, invite ) == FixedReadResult::Ok )
                    _inviteBuffer.push( invite );
                break;
            }
            case MatchmakingMethod::kPushLobby:
            {
                LobbySnapshot lobby;
                if ( MatchmakingProtocol::MessageTypes::read( body, lobby ) == FixedReadResult::Ok )
                    _lobbyBuffer.push( std::move( lobby ) );
                break;
            }
            case MatchmakingMethod::kPushMatch:
            {
                MatchAssignment assignment;
                if ( MatchmakingProtocol::MessageTypes::read( body, assignment ) == FixedReadResult::Ok )
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
