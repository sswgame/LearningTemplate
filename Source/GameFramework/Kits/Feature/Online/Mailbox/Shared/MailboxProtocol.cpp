#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Mailbox/Shared/MailboxProtocol.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Service/OnlineLimit.h"
#include "GameFramework/Base/Online/Service/OnlineReplyUtil.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct MailboxProtocolInternal
        {
            static void writeAttachments( BitWriter& outWriter, const vector<ServiceMailAttachment>& listAttachment )
            {
                outWriter.writeVarUint( listAttachment.size() );
                for ( const ServiceMailAttachment& attachment : listAttachment )
                {
                    ServiceKeyUtil::writeString( outWriter, attachment._assetID );
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
                    if ( ServiceKeyUtil::readString( reader, LedgerConstant::kMaxAssetIDSize, attachment._assetID ) == false )
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
                // 메일 키 길이 상한은 `OnlineLimit::kMaxKeySize` 를 따른다.
                const bool bTextOk = ServiceKeyUtil::readString( reader, OnlineLimit::kMaxKeySize, outMail._mailKey ) &&
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
    void MailboxProtocol::writeRequest( BitWriter& outWriter, const MailboxRequest& request )
    {
        ServiceKeyUtil::writeString( outWriter, request._mailKey );
        ServiceKeyUtil::writeString( outWriter, request._cursor );
        outWriter.writeVarInt( request._maxCount );
    }

    bool MailboxProtocol::readRequest( BitReader& reader, MailboxRequest& outRequest )
    {
        // 메일 키와 커서 길이 상한은 `OnlineLimit::kMaxKeySize` 를 따른다.
        const bool  bTextOk  = ServiceKeyUtil::readString( reader, OnlineLimit::kMaxKeySize, outRequest._mailKey ) && ServiceKeyUtil::readString( reader, OnlineLimit::kMaxKeySize, outRequest._cursor );
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
            ServiceKeyUtil::writeString( outWriter, balance._assetID );
            outWriter.writeVarInt( balance._amount );
        }
        outWriter.writeVarUint( reply._listMail.size() );
        for ( const MailView& mail : reply._listMail )
        {
            MailboxProtocolInternal::writeMail( outWriter, mail );
        }
    }

    bool MailboxProtocol::readReply( BitReader& reader, MailboxReply& outReply )
    {
        if ( OnlineReplyUtil::readEnum( reader, MailboxResult::Count, outReply._result ) == false )
            return false;
        outReply._claimedCount = static_cast<int32>( reader.readVarInt() );
        // 커서 길이 상한은 `OnlineLimit::kMaxKeySize` 를 따른다.
        if ( ServiceKeyUtil::readString( reader, OnlineLimit::kMaxKeySize, outReply._nextCursor ) == false )
            return false;
        const uint64 balanceCount = reader.readVarUint();
        if ( balanceCount > static_cast<uint64>( OnlineLimit::kMaxBalanceCount ) )
            return false;
        outReply._listBalance.resize( static_cast<size_t>( balanceCount ) );
        for ( LedgerBalance& balance : outReply._listBalance )
        {
            if ( ServiceKeyUtil::readString( reader, LedgerConstant::kMaxAssetIDSize, balance._assetID ) == false )
                return false;
            balance._amount = reader.readVarInt();
        }
        const uint64 mailCount = reader.readVarUint();
        if ( mailCount > static_cast<uint64>( kMaxListCount * 2 ) ) // 캠페인이 개인 우편 앞에 끼어들기 때문이다
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
        static constexpr OnlineErrorResult<MailboxResult> kArrErrorResult[] = {
            {             OnlineError::kOk,             MailboxResult::Ok},
            {OnlineError::kUnauthenticated,    MailboxResult::NotSignedIn},
            { OnlineError::kInvalidRequest, MailboxResult::InvalidRequest},
            {       OnlineError::kConflict,           MailboxResult::Busy},
        };
        return OnlineError::toResult( errorCode, kArrErrorResult, MailboxResult::Unavailable );
    }
} // namespace sw

namespace sw
{
    const OnlineProtocolEntry MailboxProtocol::kOnlineEntry{ "Mailbox", OnlineMethodRange::kMailbox, MailboxProtocol::kVersion };
} // namespace sw
