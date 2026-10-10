#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Social/Shared/SocialClient.h"

#include "Core/Network/BitStream.h"

namespace sw
{
    SocialClient::SocialClient()
        : _callTable{}
        , _notificationBuffer{}
        , _pClient{ nullptr }
    {
    }

    void SocialClient::initialize( OnlineServiceClient* pClient ) { _pClient = pClient; }

    uint64 SocialClient::requestFriend( AccountID otherID, const SocialReplyDelegate& onReply )
    {
        SocialRequest request;
        request._otherID = otherID;
        return send( SocialMethod::kRequestFriend, request, onReply );
    }

    uint64 SocialClient::requestFriendByName( string_view displayName, const SocialReplyDelegate& onReply )
    {
        SocialRequest request;
        request._text = string( displayName );
        return send( SocialMethod::kRequestFriendByName, request, onReply );
    }

    uint64 SocialClient::respondFriend( AccountID requesterID, bool bAccept, const SocialReplyDelegate& onReply )
    {
        SocialRequest request;
        request._otherID = requesterID;
        request._bAccept = bAccept ? SW_TRUE : SW_FALSE;
        return send( SocialMethod::kRespondFriend, request, onReply );
    }

    uint64 SocialClient::removeFriend( AccountID otherID, const SocialReplyDelegate& onReply )
    {
        SocialRequest request;
        request._otherID = otherID;
        return send( SocialMethod::kRemoveFriend, request, onReply );
    }

    uint64 SocialClient::block( AccountID otherID, const SocialReplyDelegate& onReply )
    {
        SocialRequest request;
        request._otherID = otherID;
        return send( SocialMethod::kBlock, request, onReply );
    }

    uint64 SocialClient::unblock( AccountID otherID, const SocialReplyDelegate& onReply )
    {
        SocialRequest request;
        request._otherID = otherID;
        return send( SocialMethod::kUnblock, request, onReply );
    }

    uint64 SocialClient::listLinks( const SocialReplyDelegate& onReply ) { return send( SocialMethod::kListLinks, SocialRequest{}, onReply ); }

    uint64 SocialClient::setPresence( SocialPresenceStatus status, string_view activity, const SocialReplyDelegate& onReply )
    {
        SocialRequest request;
        request._status = status;
        request._text   = string( activity );
        return send( SocialMethod::kSetPresence, request, onReply );
    }

    uint64 SocialClient::queryFriendPresence( const SocialReplyDelegate& onReply ) { return send( SocialMethod::kFriendPresence, SocialRequest{}, onReply ); }

    uint64 SocialClient::createGuild( string_view name, const SocialReplyDelegate& onReply )
    {
        SocialRequest request;
        request._text = string( name );
        return send( SocialMethod::kGuildCreate, request, onReply );
    }

    uint64 SocialClient::inviteToGuild( AccountID targetID, const SocialReplyDelegate& onReply )
    {
        SocialRequest request;
        request._otherID = targetID;
        return send( SocialMethod::kGuildInvite, request, onReply );
    }

    uint64 SocialClient::acceptGuildInvite( uint64 guildID, const SocialReplyDelegate& onReply )
    {
        SocialRequest request;
        request._guildID = guildID;
        return send( SocialMethod::kGuildAccept, request, onReply );
    }

    uint64 SocialClient::leaveGuild( const SocialReplyDelegate& onReply ) { return send( SocialMethod::kGuildLeave, SocialRequest{}, onReply ); }

    uint64 SocialClient::kickFromGuild( AccountID targetID, const SocialReplyDelegate& onReply )
    {
        SocialRequest request;
        request._otherID = targetID;
        return send( SocialMethod::kGuildKick, request, onReply );
    }

    uint64 SocialClient::setGuildRole( AccountID targetID, GuildRole role, const SocialReplyDelegate& onReply )
    {
        SocialRequest request;
        request._otherID = targetID;
        request._role    = role;
        return send( SocialMethod::kGuildSetRole, request, onReply );
    }

    uint64 SocialClient::setGuildNotice( string_view notice, const SocialReplyDelegate& onReply )
    {
        SocialRequest request;
        request._text = string( notice );
        return send( SocialMethod::kGuildSetNotice, request, onReply );
    }

    uint64 SocialClient::requestGuild( const SocialReplyDelegate& onReply ) { return send( SocialMethod::kGuildGet, SocialRequest{}, onReply ); }

    void SocialClient::onServicePush( uint16 kind, BitReader& body )
    {
        if ( kind != SocialMethod::kPushNotification )
            return;
        SocialNotification notification;
        if ( SocialProtocol::readNotification( body, notification ) )
            _notificationBuffer.push( std::move( notification ) );
    }

    uint64 SocialClient::send( uint16 method, const SocialRequest& request, const SocialReplyDelegate& onReply )
    {
        if ( _pClient == nullptr )
        {
            SocialClientReply reply;
            reply._method        = method;
            reply._errorCode     = OnlineError::kUnavailable;
            reply._reply._result = SocialResult::Unavailable;
            if ( onReply.isBound() )
                onReply( reply );
            return 0;
        }
        BitWriter body;
        SocialProtocol::writeRequest( body, request );
        NetRequestOptions options;
        const bool        bChange = SocialMethod::isLinkChange( method ) || ( SocialMethod::isGuild( method ) && method != SocialMethod::kGuildGet );
        if ( bChange )
            options._idempotencyKey = NetIdempotencyKey::makeRandom();
        return _callTable.send( *_pClient, method, body, options, OnlineResponseDelegate::create<&SocialClient::onResponse>( this ), PendingCall{ onReply, method } );
    }

    void SocialClient::onResponse( const OnlineResponse& response )
    {
        PendingCall call;
        if ( _callTable.take( response._requestID, call ) == false )
            return;
        SocialClientReply reply;
        reply._requestID = response._requestID;
        reply._method    = call._method;
        reply._errorCode = response._errorCode;
        if ( response._errorCode != OnlineError::kOk )
        {
            reply._reply._result = SocialProtocol::fromErrorCode( response._errorCode );
        }
        else
        {
            BitReader reader( response._pBody, response._bodySize );
            if ( SocialProtocol::readReply( reader, reply._reply ) == false )
            {
                reply._reply         = SocialReply{};
                reply._reply._result = SocialResult::Unavailable;
            }
        }
        if ( call._onReply.isBound() )
            call._onReply( reply );
    }
} // namespace sw
