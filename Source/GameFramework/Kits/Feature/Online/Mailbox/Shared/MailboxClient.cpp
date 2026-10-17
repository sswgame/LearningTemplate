#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Mailbox/Shared/MailboxClient.h"

namespace sw
{
    MailboxClient::MailboxClient()
        : _listMail{}
        , _revision{ 0 }
    {
    }

    uint64 MailboxClient::requestList( string_view cursor, int32 maxCount, ReplyDelegate onReply )
    {
        MailboxRequest request;
        request._cursor   = string( cursor );
        request._maxCount = maxCount;
        return send( MailboxMethod::kList, request, NetIdempotencyKey{}, onReply );
    }

    uint64 MailboxClient::requestMarkRead( string_view mailKey, ReplyDelegate onReply )
    {
        MailboxRequest request;
        request._mailKey = string( mailKey );
        return send( MailboxMethod::kMarkRead, request, NetIdempotencyKey{}, onReply );
    }

    uint64 MailboxClient::requestClaim( string_view mailKey, const NetIdempotencyKey& key, ReplyDelegate onReply )
    {
        MailboxRequest request;
        request._mailKey = string( mailKey );
        return send( MailboxMethod::kClaim, request, key.isValid() ? key : NetIdempotencyKey::makeRandom(), onReply );
    }

    uint64 MailboxClient::requestClaimAll( const NetIdempotencyKey& key, ReplyDelegate onReply )
    {
        return send( MailboxMethod::kClaimAll, MailboxRequest{}, key.isValid() ? key : NetIdempotencyKey::makeRandom(), onReply );
    }

    uint64 MailboxClient::requestDelete( string_view mailKey, ReplyDelegate onReply )
    {
        MailboxRequest request;
        request._mailKey = string( mailKey );
        return send( MailboxMethod::kDelete, request, NetIdempotencyKey{}, onReply );
    }

    int32 MailboxClient::countUnread() const
    {
        int32 unreadCount = 0;
        for ( const MailView& mail : _listMail )
        {
            unreadCount += mail._state == ServiceMailState::Unread ? 1 : 0;
        }
        return unreadCount;
    }

    uint64 MailboxClient::send( uint16 method, const MailboxRequest& request, const NetIdempotencyKey& key, ReplyDelegate onReply )
    {
        const bool bFirstPage = method == MailboxMethod::kList && request._cursor.empty();
        return sendRequest( method, request, key, onReply, MailboxCallContext{ request._mailKey, static_cast<uint8>( bFirstPage ? SW_TRUE : SW_FALSE ) } );
    }

    void MailboxClient::onCallSucceeded( const Call& call, const MailboxReply& reply )
    {
        if ( call._method == MailboxMethod::kList )
        {
            if ( call._context._bFirstPage == SW_TRUE )
            {
                _listMail = reply._listMail;
                ++_revision;
            }
            return;
        }
        for ( size_t mailIndex = 0; mailIndex < _listMail.size(); ++mailIndex )
        {
            MailView& mail = _listMail[mailIndex];
            if ( mail._mailKey != call._context._mailKey )
                continue;
            if ( call._method == MailboxMethod::kDelete || ( call._method == MailboxMethod::kClaim && mail._bCampaign == SW_TRUE ) )
                _listMail.erase( _listMail.begin() + static_cast<ptrdiff_t>( mailIndex ) ); // 받은 캠페인은 목록에서 뺀다
            else if ( call._method == MailboxMethod::kClaim )
                mail._state = ServiceMailState::Claimed;
            else if ( call._method == MailboxMethod::kMarkRead && mail._state == ServiceMailState::Unread )
                mail._state = ServiceMailState::Read;
            ++_revision;
            return;
        }
    }
} // namespace sw
