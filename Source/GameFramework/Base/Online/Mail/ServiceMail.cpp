#include "pch.h"

#include "GameFramework/Base/Online/Mail/ServiceMail.h"

#include "Core/Container/StringUtil.h"
#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    SW_LOG_CALLER( "ServiceMail" );

    namespace
    {
        struct ServiceMailInternal
        {
            static constexpr uint64 kRecordFormat = 1;

            static string makeMailKey( uint64 recipientAccountId, int64 createdMs, string_view idempotencyKey )
            {
                string key;
                ServiceKeyUtil::appendHex64( key, recipientAccountId );
                key.push_back( '/' );
                ServiceKeyUtil::appendHex64( key, static_cast<uint64>( createdMs < 0 ? 0 : createdMs ) );
                key.push_back( '.' );
                ServiceKeyUtil::appendHex64( key, StringUtil::computeHash64( idempotencyKey.data(), idempotencyKey.size(), false ) );
                return key;
            }

            static string makeSentKey( uint64 recipientAccountId, string_view idempotencyKey )
            {
                string key;
                ServiceKeyUtil::appendHex64( key, recipientAccountId );
                key.push_back( '/' );
                key += idempotencyKey;
                return key;
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
    const hashed_string& ServiceMail::getMailTable()
    {
        static const hashed_string s_table{ "mail_item" };
        return s_table;
    }

    const hashed_string& ServiceMail::getSentTable()
    {
        static const hashed_string s_table{ "mail_sent" };
        return s_table;
    }

    const hashed_string& ServiceMail::getExpiryTable()
    {
        static const hashed_string s_table{ "mail_expiry" };
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
        const bool bRecipientOk = message._recipientAccountId != 0 && message._fundingHolder != LedgerHolder::makeAccount( message._recipientAccountId );
        if ( bTextOk == false || bExpiryOk == false || bFundingOk == false || bReturnOk == false || bRecipientOk == false )
            return false;
        if ( attachmentCount > ServiceMailConstant::kMaxAttachmentCount || LedgerUtil::isValidEscrowToken( message._idempotencyKey ) == false )
            return false;
        for ( const ServiceMailAttachment& attachment : message._listAttachment )
        {
            const bool bAmountOk = 1 <= attachment._amount && attachment._amount <= LedgerConstant::kMaxAmount;
            if ( bAmountOk == false || LedgerUtil::isValidAssetId( attachment._assetId ) == false )
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

        const string             sentKey = ServiceMailInternal::makeSentKey( message._recipientAccountId, message._idempotencyKey );
        ServiceRecord            sentRecord;
        const ServiceStoreResult sentRead = connection.readRecord( getSentTable(), sentKey, sentRecord );
        if ( sentRead == ServiceStoreResult::Ok )
        {
            outMailKey.assign( reinterpret_cast<const utf8*>( sentRecord._bytes.data() ), sentRecord._bytes.size() );
            outbReplayed = true;
            return LedgerResult::Ok;
        }
        if ( sentRead != ServiceStoreResult::NotFound )
            return LedgerResult::Unavailable;

        const string       mailKey = ServiceMailInternal::makeMailKey( message._recipientAccountId, message._createdMs, message._idempotencyKey );
        ServiceTransaction pending;
        const bool         bEscrow = message._fundingHolder._kind != LedgerHolderKind::Mint && message._listAttachment.empty() == false;
        if ( bEscrow )
        {
            LedgerTransferRequest request;
            if ( LedgerJournalKey::makeFromToken( "mail.send", ServiceMailInternal::makeMailToken( mailKey ), request._journalKey ) == false )
                return LedgerResult::Invalid;
            request._reason           = "mail.send";
            request._timeMs           = message._createdMs;
            request._actorId          = message._actorId;
            request._actorKind        = message._actorKind;
            const LedgerHolder escrow = makeEscrowHolder( mailKey );
            for ( const ServiceMailAttachment& attachment : message._listAttachment )
            {
                LedgerPosting& posting = request._listPosting.emplace_back();
                posting._from          = message._fundingHolder;
                posting._to            = escrow;
                posting._assetId       = attachment._assetId;
                posting._amount        = attachment._amount;
            }
            LedgerTransferOutcome outcome;
            const LedgerResult    staged = Ledger::stageTransfer( connection, request, pending, outcome );
            if ( staged != LedgerResult::Ok )
                return staged;
            if ( outcome._bReplayed == SW_TRUE )
            {
                SW_LOG_ERROR( "Mail '%#' has a send journal but no sent record", mailKey.c_str() );
                return LedgerResult::Unavailable;
            }
        }

        ServiceMailRecord record;
        record._message = message;
        record._state   = ServiceMailState::Unread;
        pending.put( getMailTable(), mailKey, encodeRecord( record ), ServiceRecord::kAbsentVersion );
        pending.put( getSentTable(), sentKey, vector<uint8>( mailKey.begin(), mailKey.end() ), ServiceRecord::kAbsentVersion );
        if ( message._expiresMs > 0 )
            pending.put( getExpiryTable(), makeExpiryKey( message._expiresMs, mailKey ), vector<uint8>{}, ServiceRecord::kAbsentVersion );

        const size_t totalWriteCount = inoutTransaction.getWrites().size() + pending.getWrites().size();
        if ( totalWriteCount > static_cast<size_t>( ServiceTransaction::kMaxWriteCount ) )
            return LedgerResult::Invalid;
        Ledger::appendWrites( pending, inoutTransaction );
        outMailKey = mailKey;
        return LedgerResult::Ok;
    }

    ServiceMailExpiryAction ServiceMail::getDefaultExpiryAction( const LedgerHolder& fundingHolder )
    {
        return fundingHolder._kind == LedgerHolderKind::Account ? ServiceMailExpiryAction::ReturnToSender : ServiceMailExpiryAction::Discard;
    }

    LedgerHolder ServiceMail::makeEscrowHolder( string_view mailKey ) { return LedgerHolder::makeEscrow( "mail", ServiceMailInternal::makeMailToken( mailKey ) ); }

    string ServiceMail::makeExpiryKey( int64 expiresMs, string_view mailKey )
    {
        string key;
        ServiceKeyUtil::appendHex64( key, static_cast<uint64>( expiresMs < 0 ? 0 : expiresMs ) );
        key.push_back( '/' );
        key += mailKey;
        return key;
    }

    bool ServiceMail::parseRecipient( string_view mailKey, uint64& outAccountId )
    {
        const size_t width = static_cast<size_t>( ServiceKeyUtil::kHexWidth );
        if ( mailKey.size() <= width || mailKey[width] != '/' )
            return false;
        return ServiceKeyUtil::parseHex64( mailKey.substr( 0, width ), outAccountId );
    }

    vector<uint8> ServiceMail::encodeRecord( const ServiceMailRecord& record )
    {
        const ServiceMailMessage& message = record._message;
        BitWriter                 writer;
        writer.writeVarUint( ServiceMailInternal::kRecordFormat );
        writer.writeVarUint( static_cast<uint64>( record._state ) );
        writer.writeVarUint( static_cast<uint64>( message._expiryAction ) );
        writer.writeVarUint( static_cast<uint64>( message._actorKind ) );
        writer.writeBool( message._bLiteralText == SW_TRUE );
        writer.writeVarUint( message._recipientAccountId );
        writer.writeVarUint( message._actorId );
        writer.writeVarInt( message._createdMs );
        writer.writeVarInt( message._expiresMs );
        LedgerUtil::writeHolder( writer, message._fundingHolder );
        ServiceKeyUtil::writeString( writer, message._titleKey );
        ServiceKeyUtil::writeString( writer, message._body );
        ServiceKeyUtil::writeString( writer, message._senderName );
        ServiceKeyUtil::writeString( writer, message._idempotencyKey );
        writer.writeVarUint( message._listAttachment.size() );
        for ( const ServiceMailAttachment& attachment : message._listAttachment )
        {
            ServiceKeyUtil::writeString( writer, attachment._assetId );
            writer.writeVarInt( attachment._amount );
        }
        return writer.releaseBytes();
    }

    bool ServiceMail::decodeRecord( const vector<uint8>& bytes, ServiceMailRecord& outRecord )
    {
        ServiceMailMessage& message = outRecord._message;
        BitReader           reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        if ( reader.readVarUint() != ServiceMailInternal::kRecordFormat )
            return false;
        const uint64 state        = reader.readVarUint();
        const uint64 expiryAction = reader.readVarUint();
        const uint64 actorKind    = reader.readVarUint();
        const bool   bStateOk     = state < static_cast<uint64>( ServiceMailState::Count ) && expiryAction < static_cast<uint64>( ServiceMailExpiryAction::Count ) &&
                              actorKind < static_cast<uint64>( LedgerActorKind::Count );
        if ( bStateOk == false )
            return false;
        outRecord._state            = static_cast<ServiceMailState>( state );
        message._expiryAction       = static_cast<ServiceMailExpiryAction>( expiryAction );
        message._actorKind          = static_cast<LedgerActorKind>( actorKind );
        message._bLiteralText       = reader.readBool() ? SW_TRUE : SW_FALSE;
        message._recipientAccountId = reader.readVarUint();
        message._actorId            = reader.readVarUint();
        message._createdMs          = reader.readVarInt();
        message._expiresMs          = reader.readVarInt();
        if ( LedgerUtil::readHolder( reader, message._fundingHolder ) == false )
            return false;
        const bool bTextOk = ServiceKeyUtil::readString( reader, ServiceMailConstant::kMaxTitleSize, message._titleKey ) &&
                             ServiceKeyUtil::readString( reader, ServiceMailConstant::kMaxBodySize, message._body ) &&
                             ServiceKeyUtil::readString( reader, ServiceMailConstant::kMaxSenderNameSize, message._senderName ) &&
                             ServiceKeyUtil::readString( reader, ServiceMailConstant::kMaxIdempotencyKeySize, message._idempotencyKey );
        if ( bTextOk == false )
            return false;
        const uint64 attachmentCount = reader.readVarUint();
        if ( attachmentCount > static_cast<uint64>( ServiceMailConstant::kMaxAttachmentCount ) )
            return false;
        message._listAttachment.resize( static_cast<size_t>( attachmentCount ) );
        for ( ServiceMailAttachment& attachment : message._listAttachment )
        {
            if ( ServiceKeyUtil::readString( reader, LedgerConstant::kMaxAssetIdSize, attachment._assetId ) == false )
                return false;
            attachment._amount = reader.readVarInt();
        }
        return reader.hasOverflowed() == false;
    }
} // namespace sw
