#include "pch.h"

#include "GameFramework/Runtime/Online/Mail/Server/ServiceMail.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Runtime/Online/Ledger/Server/Ledger.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceCommitRunner.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordReader.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordTable.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceTTLRule.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct ServiceMailInternal
        {
            static string makeMailKey( uint64 recipientAccountID, int64 createdMs, string_view idempotencyKey )
            {
                string key;
                ServiceKeyUtil::appendHex64( key, recipientAccountID );
                key.push_back( '/' );
                ServiceKeyUtil::appendHex64( key, static_cast<uint64>( createdMs < 0 ? 0 : createdMs ) );
                key.push_back( '.' );
                ServiceKeyUtil::appendHex64( key, HashUtil::computeStringHash64( idempotencyKey.data(), idempotencyKey.size(), false ) );
                return key;
            }

            static string makeSentKey( uint64 recipientAccountID, string_view idempotencyKey )
            {
                StringBuilder<constant::buffer::kMax128> key; // 계정은 `ServiceKeyUtil::appendHex64` 와 같은 소문자 16 진 16자다
                key.appendFormat( "%016llx/%#", static_cast<unsigned long long>( recipientAccountID ), idempotencyKey );
                return string( key.view() );
            }

            static string makeMailToken( string_view mailKey )
            {
                string token{ mailKey };
                for ( utf8& ch : token )
                {
                    if ( ch == '/' )
                        ch = '.';
                }
                return token;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const ServiceRecordTable<ServiceMailStoreRecord>& ServiceMail::getMailTable()
    {
        static const ServiceRecordTable<ServiceMailStoreRecord> s_table{ "mail_item" };
        return s_table;
    }

    const ServiceRecordTable<ServiceMailSentRecord>& ServiceMail::getSentTable()
    {
        static const ServiceRecordTable<ServiceMailSentRecord> s_table{ "mail_sent" };
        return s_table;
    }

    bool ServiceMail::readSentTimeMs( const ServiceRecord& record, int64& outTimeMs )
    {
        // 값은 우편 키 `<받는 계정 16 진>/<만든 시각 16 진>.<멱등 키 해시 16 진>` 이다
        const size_t          width = static_cast<size_t>( ServiceKeyUtil::kHexWidth );
        ServiceMailSentRecord sent;
        if ( getSentTable().decode( record, sent ) == false )
            return false;
        const string_view mailKey   = sent._mailKey;
        uint64            createdMs = 0;
        const bool        bShapeOk  = mailKey.size() == width * 3 + 2 && mailKey[width] == '/' && mailKey[width * 2 + 1] == '.';
        if ( bShapeOk == false || ServiceKeyUtil::parseHex64( mailKey.substr( width + 1, width ), createdMs ) == false )
            return false;
        outTimeMs = static_cast<int64>( createdMs );
        return true;
    }

    const ServiceIndexTable& ServiceMail::getSentExpiryTable()
    {
        static const ServiceIndexTable s_table{ "mail_sent_expiry" };
        return s_table;
    }

    ServiceTTLRule ServiceMail::makeSentTTLRule( int64 expireAfterMs )
    {
        ServiceTTLRule rule;
        rule._table           = getSentTable();
        rule._ttlIndexTable   = getSentExpiryTable();
        rule._expireAfterMs   = expireAfterMs;
        rule._pfnReadTime     = &readSentTimeMs;
        rule._pBackfillSource = ServiceTTLRule::makeRecordBackfillSource<&readSentTimeMs>();
        rule._backfillVersion = 0;
        return rule;
    }

    const ServiceIndexTable& ServiceMail::getExpiryTable()
    {
        static const ServiceIndexTable s_table{ "mail_expiry" };
        return s_table;
    }

    bool ServiceMail::isValidMessage( const ServiceMailMessage& message )
    {
        const int32 attachmentCount = static_cast<int32>( message._listAttachment.size() );
        const bool  bTextOk         = message._titleKey.size() <= static_cast<size_t>( ServiceMailConstant::kMaxTitleSize ) &&
                             message._body.size() <= static_cast<size_t>( ServiceMailConstant::kMaxBodySize ) &&
                             message._senderName.size() <= static_cast<size_t>( ServiceMailConstant::kMaxSenderNameSize );
        const bool bExpiryOk    = message._expiresMs == 0 || message._expiresMs > message._createdMs;
        const bool bFundingOk   = message._fundingHolder.isValid() && message._fundingHolder._kind != LedgerHolderKind::Sink;
        const bool bReturnOk    = message._expiryAction != ServiceMailExpiryAction::ReturnToSender || message._fundingHolder._kind == LedgerHolderKind::Account;
        const bool bRecipientOk = message._recipientAccountID != 0 && message._fundingHolder != LedgerHolder::makeAccount( message._recipientAccountID );
        const bool bInvalidMail = bTextOk == false || bExpiryOk == false || bFundingOk == false || bReturnOk == false || bRecipientOk == false;
        if ( bInvalidMail )
            return false;
        if ( attachmentCount > ServiceMailConstant::kMaxAttachmentCount || LedgerUtil::isValidEscrowToken( message._idempotencyKey ) == false )
            return false;
        for ( const ServiceMailAttachment& attachment : message._listAttachment )
        {
            const bool bAmountOk = 1 <= attachment._amount && attachment._amount <= LedgerConstant::kMaxAmount;
            if ( bAmountOk == false || LedgerUtil::isValidAssetID( attachment._assetID ) == false )
                return false;
        }
        return true;
    }

    LedgerResult ServiceMail::stageSend( IServiceStoreConnection& connection, const ServiceMailMessage& message, ServiceTransaction& inoutTransaction, string& outMailKey,
                                         bool& outbReplayed )
    {
        outMailKey.clear();
        outbReplayed = false;
        if ( isValidMessage( message ) == false )
            return LedgerResult::Invalid;

        const string             sentKey = ServiceMailInternal::makeSentKey( message._recipientAccountID, message._idempotencyKey );
        ServiceMailSentRecord    sentRecord;
        const ServiceStoreResult sentRead = getSentTable().read( connection, sentKey, sentRecord );
        if ( sentRead == ServiceStoreResult::Ok )
        {
            outMailKey   = std::move( sentRecord._mailKey );
            outbReplayed = true;
            return LedgerResult::Ok;
        }
        if ( sentRead != ServiceStoreResult::NotFound )
            return LedgerResult::Unavailable;

        const string       mailKey = ServiceMailInternal::makeMailKey( message._recipientAccountID, message._createdMs, message._idempotencyKey );
        ServiceTransaction pending;
        const bool         bEscrow = message._fundingHolder._kind != LedgerHolderKind::Mint && message._listAttachment.empty() == false;
        if ( bEscrow )
        {
            LedgerTransferRequest request;
            if ( LedgerJournalKey::makeFromToken( "mail.send", ServiceMailInternal::makeMailToken( mailKey ), request._journalKey ) == false )
                return LedgerResult::Invalid;
            request._reason           = "mail.send";
            request._timeMs           = message._createdMs;
            request._actorID          = message._actorID;
            request._actorKind        = message._actorKind;
            const LedgerHolder escrow = makeEscrowHolder( mailKey );
            for ( const ServiceMailAttachment& attachment : message._listAttachment )
            {
                LedgerPosting& posting = request._listPosting.emplace_back();
                posting._from          = message._fundingHolder;
                posting._to            = escrow;
                posting._assetID       = attachment._assetID;
                posting._amount        = attachment._amount;
            }
            LedgerTransferResult result;
            const LedgerResult   staged = Ledger::stageTransfer( connection, request, pending, result );
            if ( staged != LedgerResult::Ok )
                return staged;
            if ( result._bReplayed == SW_TRUE )
            {
                SW_LOG_ERROR_ID( "ServiceMail.MailSendJournalBut", "Mail '%#' has a send journal but no sent record", mailKey.c_str() );
                return LedgerResult::Unavailable;
            }
        }

        ServiceMailRecord record;
        record._message = message;
        record._state   = ServiceMailState::Unread;
        pending.put( getMailTable(), mailKey, encodeRecord( record ), ServiceRecord::kAbsentVersion );
        pending.put( getSentTable(), sentKey, ServiceMailSentRecord{ mailKey, {} }, ServiceRecord::kAbsentVersion );
        pending.put( getSentExpiryTable(), ServiceTTLRule::makeTTLIndexKey( message._createdMs, sentKey ), ServiceRecord::kAnyVersion );
        if ( message._expiresMs > 0 )
            pending.put( getExpiryTable(), makeExpiryKey( message._expiresMs, mailKey ), ServiceRecord::kAbsentVersion );
        pending.put( getNotificationTable(), makeExpiryKey( message._createdMs, mailKey ), // 키 모양(시각 16 진 + 우편 키)이 TTL 색인과 같다
                     ServiceMailNotificationRecord{ message._titleKey, {} }, ServiceRecord::kAbsentVersion );

        const size_t totalWriteCount = inoutTransaction.getWrites().size() + pending.getWrites().size();
        if ( totalWriteCount > static_cast<size_t>( ServiceTransaction::kMaxWriteCount ) )
            return LedgerResult::Invalid;
        Ledger::appendWrites( pending, inoutTransaction );
        outMailKey = mailKey;
        return LedgerResult::Ok;
    }

    const ServiceRecordTable<ServiceMailNotificationRecord>& ServiceMail::getNotificationTable()
    {
        static const ServiceRecordTable<ServiceMailNotificationRecord> s_table{ "mail_notice" };
        return s_table;
    }

    ServiceStoreResult ServiceMail::claimNotifications( IServiceStoreConnection& connection, int32 maxCount, vector<ServiceMailNotification>& outListNotification )
    {
        outListNotification.clear();
        const int32              batchCount = maxCount < ServiceTransaction::kMaxWriteCount ? maxCount : ServiceTransaction::kMaxWriteCount;
        vector<ServiceRecord>    listRecord;
        const ServiceStoreResult listed = ServiceRecordReader::readPage( connection, getNotificationTable(), string_view{}, string_view{}, batchCount, false, listRecord )._result;
        if ( listed != ServiceStoreResult::Ok || listRecord.empty() )
            return listed;
        ServiceTransaction              transaction;
        vector<ServiceMailNotification> listNotification;
        for ( const ServiceRecord& record : listRecord )
        {
            const size_t                  separator = record._key.find( '/' );
            ServiceMailNotificationRecord stored;
            if ( separator == string::npos || getNotificationTable().decode( record, stored ) == false )
                return ServiceStoreResult::Invalid;
            ServiceMailNotification& notification = listNotification.emplace_back();
            notification._mailKey                 = record._key.substr( separator + 1 );
            notification._titleKey                = std::move( stored._titleKey );
            if ( parseRecipient( notification._mailKey, notification._recipientAccountID ) == false )
                return ServiceStoreResult::Invalid;
            transaction.erase( getNotificationTable(), record._key, record._version );
        }
        const ServiceStoreResult committed = ServiceCommitRunner::commitOnce( connection, transaction );
        if ( committed == ServiceStoreResult::Ok )
            outListNotification = std::move( listNotification );
        return committed;
    }

    ServiceMailExpiryAction ServiceMail::getDefaultExpiryAction( const LedgerHolder& fundingHolder )
    {
        return fundingHolder._kind == LedgerHolderKind::Account ? ServiceMailExpiryAction::ReturnToSender : ServiceMailExpiryAction::Discard;
    }

    LedgerHolder ServiceMail::makeEscrowHolder( string_view mailKey )
    {
        return LedgerHolder::makeEscrow( "mail", ServiceMailInternal::makeMailToken( mailKey ) );
    }

    string ServiceMail::makeExpiryKey( int64 expiresMs, string_view mailKey )
    {
        return ServiceTTLRule::makeTTLIndexKey( expiresMs, mailKey ); // TTL 규칙의 TTL 색인과 같은 모양이다
    }

    bool ServiceMail::parseRecipient( string_view mailKey, uint64& outAccountID )
    {
        const size_t width = static_cast<size_t>( ServiceKeyUtil::kHexWidth );
        if ( mailKey.size() <= width || mailKey[width] != '/' )
            return false;
        return ServiceKeyUtil::parseHex64( mailKey.substr( 0, width ), outAccountID );
    }

    ServiceMailStoreRecord ServiceMail::encodeRecord( const ServiceMailRecord& record )
    {
        ServiceMailStoreRecord stored;
        stored._message = record._message;
        stored._state   = record._state;
        return stored;
    }

    bool ServiceMail::decodeRecord( const ServiceRecord& raw, ServiceMailRecord& outRecord )
    {
        ServiceMailStoreRecord stored;
        if ( getMailTable().decode( raw, stored ) == false )
            return false;
        const ServiceMailMessage& message  = stored._message;
        const bool                bStateOk = stored._state != ServiceMailState::Count && message._expiryAction != ServiceMailExpiryAction::Count &&
                              message._actorKind != LedgerActorKind::Count && message._fundingHolder.isValid();
        if ( bStateOk == false )
            return false; // 누구도 가질 수 없는 값 · 규칙 밖 보유자다
        outRecord._message = std::move( stored._message );
        outRecord._state   = stored._state;
        return true;
    }
} // namespace sw
