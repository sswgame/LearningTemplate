#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Mailbox/Shared/MailboxClient.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Service/OnlineReplyUtil.h"

namespace sw
{
    MailboxClient::MailboxClient()
        : _callTable{}
        , _listMail{}
        , _pClient{ nullptr }
        , _nextRequestID{ 1 }
        , _revision{ 0 }
    {
    }

    void MailboxClient::initialize( OnlineServiceClient* pClient ) { _pClient = pClient; }

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

    void MailboxClient::onServicePush( uint16 kind, BitReader& body )
    {
        (void)kind; // 우편함은 아직 알림을 보내지 않는다. 새 우편은 다음 목록으로 알게 된다
        (void)body;
    }

    uint64 MailboxClient::send( uint16 method, const MailboxRequest& request, const NetIdempotencyKey& key, ReplyDelegate onReply )
    {
        const uint64      requestID = _nextRequestID++;
        const PendingCall pendingCall{ onReply, request._mailKey, key, requestID, method, static_cast<uint8>( method == MailboxMethod::kList && request._cursor.empty() ? SW_TRUE : SW_FALSE ) };
        if ( _pClient == nullptr )
        {
            MailboxClientReply reply;
            reply._requestID      = requestID;
            reply._method         = method;
            reply._idempotencyKey = key;
            reply._errorCode      = OnlineError::kUnavailable;
            reply._reply._result  = MailboxResult::Unavailable;
            if ( onReply.isBound() )
                onReply( reply );
            return requestID;
        }
        BitWriter body;
        MailboxProtocol::writeRequest( body, request );
        NetRequestOptions options;
        options._idempotencyKey = key;
        (void)_callTable.send( *_pClient, method, body, options, OnlineResponseDelegate::create<&MailboxClient::onResponse>( this ), pendingCall ); // 응답은 pendingCall._requestID 로 알린다
        return requestID;
    }

    void MailboxClient::onResponse( const OnlineResponse& response )
    {
        PendingCall call;
        if ( _callTable.take( response._requestID, call ) == false )
            return;
        MailboxClientReply reply;
        reply._requestID      = call._requestID;
        reply._method         = call._method;
        reply._idempotencyKey = call._key;
        reply._errorCode      = response._errorCode;
        if ( OnlineReplyUtil::readResponse<MailboxProtocol>( response, reply._reply ) && reply._reply._result == MailboxResult::Ok )
            applyToCache( call, reply._reply );
        if ( call._onReply.isBound() )
            call._onReply( reply );
    }

    void MailboxClient::applyToCache( const PendingCall& call, const MailboxReply& reply )
    {
        if ( call._method == MailboxMethod::kList )
        {
            if ( call._bFirstPage == SW_TRUE )
            {
                _listMail = reply._listMail;
                ++_revision;
            }
            return;
        }
        for ( size_t mailIndex = 0; mailIndex < _listMail.size(); ++mailIndex )
        {
            MailView& mail = _listMail[mailIndex];
            if ( mail._mailKey != call._mailKey )
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
