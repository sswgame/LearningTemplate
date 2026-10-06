#include "pch.h"

#include "GameFramework/Kits/Online/Mailbox/MailboxProtocol.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct MailboxProtocolInternal
        {
            static constexpr int32 kMaxBalanceCount = 1024;

            static void writeAttachments( BitWriter& outWriter, const vector<ServiceMailAttachment>& listAttachment )
            {
                outWriter.writeVarUint( listAttachment.size() );
                for ( const ServiceMailAttachment& attachment : listAttachment )
                {
                    ServiceKeyUtil::writeString( outWriter, attachment._assetId );
                    outWriter.writeVarInt( attachment._amount );
                }
            }

            [[nodiscard]] static bool readAttachments( BitReader& reader, vector<ServiceMailAttachment>& outListAttachment )
            {
                const uint64 count = reader.readVarUint();
                if ( count > static_cast<uint64>( ServiceMailConstant::kMaxAttachmentCount ) )
                    return false;
                outListAttachment.resize( static_cast<size_t>( count ) );
                for ( ServiceMailAttachment& attachment : outListAttachment )
                {
                    if ( ServiceKeyUtil::readString( reader, LedgerConstant::kMaxAssetIdSize, attachment._assetId ) == false )
                        return false;
                    attachment._amount = reader.readVarInt();
                }
                return true;
            }

            static void writeMail( BitWriter& outWriter, const MailView& mail )
            {
                ServiceKeyUtil::writeString( outWriter, mail._mailKey );
                ServiceKeyUtil::writeString( outWriter, mail._titleKey );
                ServiceKeyUtil::writeString( outWriter, mail._body );
                ServiceKeyUtil::writeString( outWriter, mail._senderName );
                outWriter.writeVarInt( mail._createdMs );
                outWriter.writeVarInt( mail._expiresMs );
                outWriter.writeVarUint( static_cast<uint64>( mail._state ) );
                outWriter.writeBool( mail._bLiteralText == SW_TRUE );
                outWriter.writeBool( mail._bCampaign == SW_TRUE );
                writeAttachments( outWriter, mail._listAttachment );
            }

            [[nodiscard]] static bool readMail( BitReader& reader, MailView& outMail )
            {
                const bool bTextOk = ServiceKeyUtil::readString( reader, MailboxProtocol::kMaxKeySize, outMail._mailKey ) &&
                                     ServiceKeyUtil::readString( reader, ServiceMailConstant::kMaxTitleSize, outMail._titleKey ) &&
                                     ServiceKeyUtil::readString( reader, ServiceMailConstant::kMaxBodySize, outMail._body ) &&
                                     ServiceKeyUtil::readString( reader, ServiceMailConstant::kMaxSenderNameSize, outMail._senderName );
                if ( bTextOk == false )
                    return false;
                outMail._createdMs = reader.readVarInt();
                outMail._expiresMs = reader.readVarInt();
                const uint64 state = reader.readVarUint();
                if ( state >= static_cast<uint64>( ServiceMailState::Count ) )
                    return false;
                outMail._state        = static_cast<ServiceMailState>( state );
                outMail._bLiteralText = reader.readBool() ? SW_TRUE : SW_FALSE;
                outMail._bCampaign    = reader.readBool() ? SW_TRUE : SW_FALSE;
                return readAttachments( reader, outMail._listAttachment );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( MailboxResult result )
    {
        static constexpr const utf8* kArrName[] = { "Ok", "NotSignedIn", "InvalidRequest", "NotFound", "AlreadyClaimed",
                                                    "Expired", "HasAttachments", "CapExceeded", "Busy", "Unavailable" };
        static_assert( SW_COUNT_OF( kArrName ) == static_cast<size_t>( MailboxResult::Count ), "MailboxResult names must match the enum" );
        const size_t index = static_cast<size_t>( result );
        return index < SW_COUNT_OF( kArrName ) ? kArrName[index] : "Unknown";
    }

    void MailboxProtocol::writeRequest( BitWriter& outWriter, const MailboxRequest& request )
    {
        ServiceKeyUtil::writeString( outWriter, request._mailKey );
        ServiceKeyUtil::writeString( outWriter, request._cursor );
        outWriter.writeVarInt( request._maxCount );
    }

    bool MailboxProtocol::readRequest( BitReader& reader, MailboxRequest& outRequest )
    {
        const bool  bTextOk  = ServiceKeyUtil::readString( reader, kMaxKeySize, outRequest._mailKey ) && ServiceKeyUtil::readString( reader, kMaxKeySize, outRequest._cursor );
        const int64 maxCount = reader.readVarInt();
        outRequest._maxCount = static_cast<int32>( maxCount );
        const bool bCountOk  = 1 <= maxCount && maxCount <= kMaxListCount;
        return bTextOk && reader.hasOverflowed() == false && bCountOk;
    }

    void MailboxProtocol::writeReply( BitWriter& outWriter, const MailboxReply& reply )
    {
        outWriter.writeVarUint( static_cast<uint64>( reply._result ) );
        outWriter.writeVarInt( reply._claimedCount );
        ServiceKeyUtil::writeString( outWriter, reply._nextCursor );
        outWriter.writeVarUint( reply._listBalance.size() );
        for ( const LedgerBalance& balance : reply._listBalance )
        {
            ServiceKeyUtil::writeString( outWriter, balance._assetId );
            outWriter.writeVarInt( balance._amount );
        }
        outWriter.writeVarUint( reply._listMail.size() );
        for ( const MailView& mail : reply._listMail )
            MailboxProtocolInternal::writeMail( outWriter, mail );
    }

    bool MailboxProtocol::readReply( BitReader& reader, MailboxReply& outReply )
    {
        const uint64 result = reader.readVarUint();
        if ( result >= static_cast<uint64>( MailboxResult::Count ) )
            return false;
        outReply._result       = static_cast<MailboxResult>( result );
        outReply._claimedCount = static_cast<int32>( reader.readVarInt() );
        if ( ServiceKeyUtil::readString( reader, kMaxKeySize, outReply._nextCursor ) == false )
            return false;
        const uint64 balanceCount = reader.readVarUint();
        if ( balanceCount > static_cast<uint64>( MailboxProtocolInternal::kMaxBalanceCount ) )
            return false;
        outReply._listBalance.resize( static_cast<size_t>( balanceCount ) );
        for ( LedgerBalance& balance : outReply._listBalance )
        {
            if ( ServiceKeyUtil::readString( reader, LedgerConstant::kMaxAssetIdSize, balance._assetId ) == false )
                return false;
            balance._amount = reader.readVarInt();
        }
        const uint64 mailCount = reader.readVarUint();
        if ( mailCount > static_cast<uint64>( kMaxListCount * 2 ) ) // 캠페인이 개인 우편 앞에 끼어든다
            return false;
        outReply._listMail.resize( static_cast<size_t>( mailCount ) );
        for ( MailView& mail : outReply._listMail )
        {
            if ( MailboxProtocolInternal::readMail( reader, mail ) == false )
                return false;
        }
        return reader.hasOverflowed() == false;
    }

    MailboxResult MailboxProtocol::fromErrorCode( uint16 errorCode )
    {
        switch ( errorCode )
        {
            case OnlineError::kOk:
                return MailboxResult::Ok;
            case OnlineError::kUnauthenticated:
                return MailboxResult::NotSignedIn;
            case OnlineError::kInvalidRequest:
                return MailboxResult::InvalidRequest;
            case OnlineError::kConflict:
                return MailboxResult::Busy;
            default:
                return MailboxResult::Unavailable;
        }
    }
} // namespace sw
