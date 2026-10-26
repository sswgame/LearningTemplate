#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Social/Shared/SocialClient.h"

#include "Core/Serialization/BitStream.h"

namespace sw
{
    SocialClient::SocialClient()
        : _notificationBuffer{}
    {
    }

    uint64 SocialClient::requestFriend( AccountID otherID, const ReplyDelegate& onReply )
    {
        SocialRequest request;
        request._otherID = otherID;
        return send( SocialMethod::kRequestFriend, request, onReply );
    }

    uint64 SocialClient::requestFriendByName( string_view displayName, const ReplyDelegate& onReply )
    {
        SocialRequest request;
        request._text = string( displayName );
        return send( SocialMethod::kRequestFriendByName, request, onReply );
    }

    uint64 SocialClient::respondFriend( AccountID requesterID, bool bAccept, const ReplyDelegate& onReply )
    {
        SocialRequest request;
        request._otherID = requesterID;
        request._bAccept = bAccept ? SW_TRUE : SW_FALSE;
        return send( SocialMethod::kRespondFriend, request, onReply );
    }

    uint64 SocialClient::removeFriend( AccountID otherID, const ReplyDelegate& onReply )
    {
        SocialRequest request;
        request._otherID = otherID;
        return send( SocialMethod::kRemoveFriend, request, onReply );
    }

    uint64 SocialClient::block( AccountID otherID, const ReplyDelegate& onReply )
    {
        SocialRequest request;
        request._otherID = otherID;
        return send( SocialMethod::kBlock, request, onReply );
    }

    uint64 SocialClient::unblock( AccountID otherID, const ReplyDelegate& onReply )
    {
        SocialRequest request;
        request._otherID = otherID;
        return send( SocialMethod::kUnblock, request, onReply );
    }

    uint64 SocialClient::listLinks( const ReplyDelegate& onReply )
    {
        return send( SocialMethod::kListLinks, SocialRequest{}, onReply );
    }

    uint64 SocialClient::setPresence( SocialPresenceStatus status, string_view activity, const ReplyDelegate& onReply )
    {
        SocialRequest request;
        request._status = status;
        request._text   = string( activity );
        return send( SocialMethod::kSetPresence, request, onReply );
    }

    uint64 SocialClient::queryFriendPresence( const ReplyDelegate& onReply )
    {
        return send( SocialMethod::kFriendPresence, SocialRequest{}, onReply );
    }

    uint64 SocialClient::createGuild( string_view name, const ReplyDelegate& onReply )
    {
        SocialRequest request;
        request._text = string( name );
        return send( SocialMethod::kGuildCreate, request, onReply );
    }

    uint64 SocialClient::inviteToGuild( AccountID targetID, const ReplyDelegate& onReply )
    {
        SocialRequest request;
        request._otherID = targetID;
        return send( SocialMethod::kGuildInvite, request, onReply );
    }

    uint64 SocialClient::acceptGuildInvite( uint64 guildID, const ReplyDelegate& onReply )
    {
        SocialRequest request;
        request._guildID = guildID;
        return send( SocialMethod::kGuildAccept, request, onReply );
    }

    uint64 SocialClient::leaveGuild( const ReplyDelegate& onReply )
    {
        return send( SocialMethod::kGuildLeave, SocialRequest{}, onReply );
    }

    uint64 SocialClient::kickFromGuild( AccountID targetID, const ReplyDelegate& onReply )
    {
        SocialRequest request;
        request._otherID = targetID;
        return send( SocialMethod::kGuildKick, request, onReply );
    }

    uint64 SocialClient::setGuildRole( AccountID targetID, GuildRole role, const ReplyDelegate& onReply )
    {
        SocialRequest request;
        request._otherID = targetID;
        request._role    = role;
        return send( SocialMethod::kGuildSetRole, request, onReply );
    }

    uint64 SocialClient::setGuildNotice( string_view notice, const ReplyDelegate& onReply )
    {
        SocialRequest request;
        request._text = string( notice );
        return send( SocialMethod::kGuildSetNotice, request, onReply );
    }

    uint64 SocialClient::requestGuild( const ReplyDelegate& onReply )
    {
        return send( SocialMethod::kGuildGet, SocialRequest{}, onReply );
    }

    uint64 SocialClient::depositToGuild( string_view assetID, int64 amount, const ReplyDelegate& onReply )
    {
        SocialRequest request;
        request._assetID = string( assetID );
        request._amount  = amount;
        return send( SocialMethod::kGuildDeposit, request, onReply );
    }

    uint64 SocialClient::withdrawFromGuild( string_view assetID, int64 amount, const ReplyDelegate& onReply )
    {
        SocialRequest request;
        request._assetID = string( assetID );
        request._amount  = amount;
        return send( SocialMethod::kGuildWithdraw, request, onReply );
    }

    uint64 SocialClient::requestGuildStorage( const ReplyDelegate& onReply )
    {
        return send( SocialMethod::kGuildStorage, SocialRequest{}, onReply );
    }

    void SocialClient::onServicePush( uint16 kind, BitReader& body )
    {
        if ( kind != SocialMethod::kPushNotification )
            return;
        SocialNotification notification;
        if ( SocialProtocol::MessageTypes::read( body, notification ) == FixedReadResult::Ok )
            _notificationBuffer.push( std::move( notification ) );
    }

    uint64 SocialClient::send( uint16 method, const SocialRequest& request, const ReplyDelegate& onReply )
    {
        const bool              bChange = SocialMethod::isLinkChange( method ) || ( SocialMethod::isGuild( method ) && SocialMethod::isGuildRead( method ) == false );
        const NetIdempotencyKey key     = bChange ? NetIdempotencyKey::makeRandom() : NetIdempotencyKey{};
        return sendRequest( method, request, key, onReply );
    }
} // namespace sw
