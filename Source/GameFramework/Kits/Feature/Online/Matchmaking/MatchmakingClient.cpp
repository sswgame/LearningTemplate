#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/MatchmakingClient.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    MatchmakingClient::MatchmakingClient()
        : _callTable{}
        , _partyBuffer{}
        , _inviteBuffer{}
        , _lobbyBuffer{}
        , _matchBuffer{}
        , _party{}
        , _pClient{ nullptr }
    {
    }

    void MatchmakingClient::initialize( OnlineServiceClient* pClient ) { _pClient = pClient; }

    uint64 MatchmakingClient::createParty( const ReplyDelegate& onReply ) { return send( MatchmakingMethod::kPartyCreate, BitWriter{}, onReply ); }

    uint64 MatchmakingClient::inviteToParty( AccountId targetId, const ReplyDelegate& onReply )
    {
        BitWriter body;
        body.writeVarUint( targetId );
        return send( MatchmakingMethod::kPartyInvite, body, onReply );
    }

    uint64 MatchmakingClient::acceptPartyInvite( uint64 partyId, const ReplyDelegate& onReply )
    {
        BitWriter body;
        body.writeVarUint( partyId );
        return send( MatchmakingMethod::kPartyAccept, body, onReply );
    }

    uint64 MatchmakingClient::leaveParty( const ReplyDelegate& onReply ) { return send( MatchmakingMethod::kPartyLeave, BitWriter{}, onReply ); }

    uint64 MatchmakingClient::kickFromParty( AccountId targetId, const ReplyDelegate& onReply )
    {
        BitWriter body;
        body.writeVarUint( targetId );
        return send( MatchmakingMethod::kPartyKick, body, onReply );
    }

    uint64 MatchmakingClient::createLobby( const LobbySnapshot& request, const ReplyDelegate& onReply )
    {
        BitWriter body;
        MatchmakingProtocol::writeLobby( body, request );
        return send( MatchmakingMethod::kLobbyCreate, body, onReply );
    }

    uint64 MatchmakingClient::listLobbies( string_view modeId, const ReplyDelegate& onReply )
    {
        BitWriter body;
        ServiceKeyUtil::writeString( body, modeId );
        return send( MatchmakingMethod::kLobbyList, body, onReply );
    }

    uint64 MatchmakingClient::joinLobby( uint64 lobbyId, const ReplyDelegate& onReply )
    {
        BitWriter body;
        body.writeVarUint( lobbyId );
        return send( MatchmakingMethod::kLobbyJoin, body, onReply );
    }

    uint64 MatchmakingClient::leaveLobby( uint64 lobbyId, const ReplyDelegate& onReply )
    {
        BitWriter body;
        body.writeVarUint( lobbyId );
        return send( MatchmakingMethod::kLobbyLeave, body, onReply );
    }

    uint64 MatchmakingClient::setLobbyReady( uint64 lobbyId, bool bReady, const ReplyDelegate& onReply )
    {
        BitWriter body;
        body.writeVarUint( lobbyId );
        body.writeBool( bReady );
        return send( MatchmakingMethod::kLobbyReady, body, onReply );
    }

    uint64 MatchmakingClient::startLobby( uint64 lobbyId, const ReplyDelegate& onReply )
    {
        BitWriter body;
        body.writeVarUint( lobbyId );
        return send( MatchmakingMethod::kLobbyStart, body, onReply );
    }

    uint64 MatchmakingClient::joinQueue( string_view modeId, string_view region, const ReplyDelegate& onReply )
    {
        BitWriter body;
        ServiceKeyUtil::writeString( body, modeId );
        ServiceKeyUtil::writeString( body, region );
        return send( MatchmakingMethod::kQueueJoin, body, onReply );
    }

    uint64 MatchmakingClient::leaveQueue( const ReplyDelegate& onReply ) { return send( MatchmakingMethod::kQueueLeave, BitWriter{}, onReply ); }

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

    uint64 MatchmakingClient::send( uint16 method, const BitWriter& body, const ReplyDelegate& onReply )
    {
        if ( _pClient == nullptr )
        {
            MatchmakingClientReply reply;
            reply._errorCode     = OnlineError::kUnavailable;
            reply._reply._result = MatchmakingResult::Unavailable;
            if ( onReply.isBound() )
                onReply( reply );
            return 0;
        }
        return _callTable.send( *_pClient, method, body, NetRequestOptions{}, OnlineResponseDelegate::create<&MatchmakingClient::onResponse>( this ), onReply );
    }

    void MatchmakingClient::onResponse( const OnlineResponse& response )
    {
        ReplyDelegate onReply;
        if ( _callTable.take( response._requestId, onReply ) == false )
            return;
        MatchmakingClientReply reply;
        reply._requestId = response._requestId;
        reply._errorCode = response._errorCode;
        if ( response._errorCode != OnlineError::kOk || response.isOk() == false )
        {
            reply._reply._result = MatchmakingProtocol::fromErrorCode( response._errorCode == OnlineError::kOk ? OnlineError::kUnavailable : response._errorCode );
        }
        else
        {
            BitReader reader( response._pBody, response._bodySize );
            if ( MatchmakingProtocol::readReply( reader, reply._reply ) == false )
            {
                reply._reply         = MatchmakingReply{};
                reply._reply._result = MatchmakingResult::Invalid;
            }
        }
        if ( onReply.isBound() )
            onReply( reply );
    }
} // namespace sw
