#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Mailbox/Runtime/MailboxClient.h"

#include "GameFramework/Runtime/Online/Service/OnlineClientRegistry.h"

namespace sw
{
    MailboxClient::MailboxClient()
        : _listMail{}
        , _newMailGate{}
        , _newMailBuffer{}
        , _revision{ 0 }
    {
    }

    void MailboxClient::onPush( MailboxMethod::NewMail, const MailboxNewMail& body )
    {
        MailboxNewMail newMail = body;
        _newMailBuffer.push( std::move( newMail ) );
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

    int32 MailboxClient::findMailIndex( const string& mailKey ) const
    {
        for ( size_t mailIndex = 0; mailIndex < _listMail.size(); ++mailIndex )
        {
            if ( _listMail[mailIndex]._mailKey == mailKey )
                return static_cast<int32>( mailIndex );
        }
        return -1;
    }

    void MailboxClient::onCallSucceeded( MailboxMethod::List, const MailboxListRequest& request, const MailboxListReply& reply )
    {
        if ( request._cursor.empty() == false )
            return;
        _listMail = reply._listMail;
        ++_revision;
    }

    void MailboxClient::onCallSucceeded( MailboxMethod::MarkRead, const MailboxMailRequest& request, const MailboxResultReply& reply )
    {
        (void)reply;
        const int32 mailIndex = findMailIndex( request._mailKey );
        if ( mailIndex < 0 )
            return;
        MailView& mail = _listMail[static_cast<size_t>( mailIndex )];
        if ( mail._state == ServiceMailState::Unread )
            mail._state = ServiceMailState::Read;
        ++_revision;
    }

    void MailboxClient::onCallSucceeded( MailboxMethod::Claim, const MailboxMailRequest& request, const MailboxClaimReply& reply )
    {
        (void)reply;
        const int32 mailIndex = findMailIndex( request._mailKey );
        if ( mailIndex < 0 )
            return;
        MailView& mail = _listMail[static_cast<size_t>( mailIndex )];
        if ( mail._bCampaign == SW_TRUE )
            _listMail.erase( _listMail.begin() + static_cast<ptrdiff_t>( mailIndex ) ); // 받은 캠페인은 목록에서 뺀다
        else
            mail._state = ServiceMailState::Claimed;
        ++_revision;
    }

    void MailboxClient::onCallSucceeded( MailboxMethod::Delete, const MailboxMailRequest& request, const MailboxResultReply& reply )
    {
        (void)reply;
        const int32 mailIndex = findMailIndex( request._mailKey );
        if ( mailIndex < 0 )
            return;
        _listMail.erase( _listMail.begin() + static_cast<ptrdiff_t>( mailIndex ) );
        ++_revision;
    }
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_CLIENT( MailboxClient );
} // namespace sw
