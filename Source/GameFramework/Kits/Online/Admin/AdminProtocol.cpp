#include "pch.h"

#include "GameFramework/Kits/Online/Admin/AdminProtocol.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct AdminProtocolInternal
        {
            static constexpr int32 kMaxBalanceCount   = 1024;
            static constexpr int32 kSanctionKindCount = static_cast<int32>( ServiceSanctionKind::Count );

            static void writeBalances( BitWriter& outWriter, const vector<LedgerBalance>& listBalance )
            {
                outWriter.writeVarUint( listBalance.size() );
                for ( const LedgerBalance& balance : listBalance )
                {
                    ServiceKeyUtil::writeString( outWriter, balance._assetId );
                    outWriter.writeVarInt( balance._amount );
                }
            }

            [[nodiscard]] static bool readBalances( BitReader& reader, vector<LedgerBalance>& outListBalance )
            {
                const uint64 count = reader.readVarUint();
                if ( count > static_cast<uint64>( kMaxBalanceCount ) )
                    return false;
                outListBalance.resize( static_cast<size_t>( count ) );
                for ( LedgerBalance& balance : outListBalance )
                {
                    if ( ServiceKeyUtil::readString( reader, LedgerConstant::kMaxAssetIdSize, balance._assetId ) == false )
                        return false;
                    balance._amount = reader.readVarInt();
                }
                return true;
            }

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

            static void writeAudit( BitWriter& outWriter, const ServiceAuditEntry& entry )
            {
                ServiceKeyUtil::writeString( outWriter, entry._actor );
                ServiceKeyUtil::writeString( outWriter, entry._action );
                ServiceKeyUtil::writeString( outWriter, entry._subject );
                ServiceKeyUtil::writeString( outWriter, entry._before );
                ServiceKeyUtil::writeString( outWriter, entry._after );
                ServiceKeyUtil::writeString( outWriter, entry._memo );
                outWriter.writeVarInt( entry._timeMs );
            }

            [[nodiscard]] static bool readAudit( BitReader& reader, ServiceAuditEntry& outEntry )
            {
                const bool bTextOk = ServiceKeyUtil::readString( reader, AdminProtocol::kMaxTextSize, outEntry._actor ) &&
                                     ServiceKeyUtil::readString( reader, AdminProtocol::kMaxTextSize, outEntry._action ) &&
                                     ServiceKeyUtil::readString( reader, AdminProtocol::kMaxTextSize, outEntry._subject ) &&
                                     ServiceKeyUtil::readString( reader, ServiceAuditEntry::kMaxStateSize, outEntry._before ) &&
                                     ServiceKeyUtil::readString( reader, ServiceAuditEntry::kMaxStateSize, outEntry._after ) &&
                                     ServiceKeyUtil::readString( reader, ServiceAuditEntry::kMaxMemoSize, outEntry._memo );
                outEntry._timeMs = reader.readVarInt();
                return bTextOk;
            }

            static void writeJournal( BitWriter& outWriter, const AdminJournalLine& line )
            {
                ServiceKeyUtil::writeString( outWriter, line._reason );
                ServiceKeyUtil::writeString( outWriter, line._memo );
                outWriter.writeVarInt( line._timeMs );
                outWriter.writeVarUint( line._actorId );
                outWriter.writeVarUint( static_cast<uint64>( line._actorKind ) );
                writeBalances( outWriter, line._listChange );
            }

            [[nodiscard]] static bool readJournal( BitReader& reader, AdminJournalLine& outLine )
            {
                const bool bTextOk = ServiceKeyUtil::readString( reader, LedgerConstant::kMaxReasonSize, outLine._reason ) &&
                                     ServiceKeyUtil::readString( reader, LedgerConstant::kMaxMemoSize, outLine._memo );
                if ( bTextOk == false )
                    return false;
                outLine._timeMs        = reader.readVarInt();
                outLine._actorId       = reader.readVarUint();
                const uint64 actorKind = reader.readVarUint();
                if ( actorKind >= static_cast<uint64>( LedgerActorKind::Count ) )
                    return false;
                outLine._actorKind = static_cast<LedgerActorKind>( actorKind );
                return readBalances( reader, outLine._listChange );
            }

            template <typename T>
            [[nodiscard]] static bool readEnum( BitReader& reader, T countValue, T& outValue )
            {
                const uint64 value = reader.readVarUint();
                if ( value >= static_cast<uint64>( countValue ) )
                    return false;
                outValue = static_cast<T>( value );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( AdminRole role )
    {
        static constexpr const utf8* kArrName[] = { "none", "viewer", "support", "operator", "super" };
        static_assert( SW_COUNT_OF( kArrName ) == static_cast<size_t>( AdminRole::Count ), "AdminRole names must match the enum" );
        const size_t index = static_cast<size_t>( role );
        return index < SW_COUNT_OF( kArrName ) ? kArrName[index] : "unknown";
    }

    const utf8* toString( AdminResult result )
    {
        static constexpr const utf8* kArrName[] = { "Ok", "NotSignedIn", "Forbidden", "InvalidRequest", "UnknownAccount", "InsufficientFunds", "CapExceeded", "Busy", "Unavailable" };
        static_assert( SW_COUNT_OF( kArrName ) == static_cast<size_t>( AdminResult::Count ), "AdminResult names must match the enum" );
        const size_t index = static_cast<size_t>( result );
        return index < SW_COUNT_OF( kArrName ) ? kArrName[index] : "Unknown";
    }

    void AdminProtocol::writeRequest( BitWriter& outWriter, const AdminRequest& request )
    {
        AdminProtocolInternal::writeAttachments( outWriter, request._listAttachment );
        outWriter.writeVarUint( request._listAccountId.size() );
        for ( const AccountId accountId : request._listAccountId )
        {
            outWriter.writeVarUint( accountId );
        }
        ServiceKeyUtil::writeString( outWriter, request._displayName );
        ServiceKeyUtil::writeString( outWriter, request._subject );
        ServiceKeyUtil::writeString( outWriter, request._assetId );
        ServiceKeyUtil::writeString( outWriter, request._reasonCode );
        ServiceKeyUtil::writeString( outWriter, request._memo );
        ServiceKeyUtil::writeString( outWriter, request._titleKey );
        ServiceKeyUtil::writeString( outWriter, request._body );
        ServiceKeyUtil::writeString( outWriter, request._cursor );
        outWriter.writeVarUint( request._accountId );
        outWriter.writeVarUint( request._batchId );
        outWriter.writeVarInt( request._amount );
        outWriter.writeVarInt( request._untilMs );
        outWriter.writeVarInt( request._startMs );
        outWriter.writeVarInt( request._endMs );
        outWriter.writeVarInt( request._maxCount );
        outWriter.writeVarUint( static_cast<uint64>( request._sanctionKind ) );
        outWriter.writeVarUint( static_cast<uint64>( request._role ) );
        outWriter.writeBool( request._bRefund == SW_TRUE );
    }

    bool AdminProtocol::readRequest( BitReader& reader, AdminRequest& outRequest )
    {
        if ( AdminProtocolInternal::readAttachments( reader, outRequest._listAttachment ) == false )
            return false;
        const uint64 accountCount = reader.readVarUint();
        if ( accountCount > static_cast<uint64>( kMaxBulkCount ) )
            return false;
        outRequest._listAccountId.resize( static_cast<size_t>( accountCount ) );
        for ( AccountId& accountId : outRequest._listAccountId )
        {
            accountId = reader.readVarUint();
        }
        const bool bTextOk = ServiceKeyUtil::readString( reader, kMaxTextSize, outRequest._displayName ) &&
                             ServiceKeyUtil::readString( reader, kMaxTextSize, outRequest._subject ) &&
                             ServiceKeyUtil::readString( reader, LedgerConstant::kMaxAssetIdSize, outRequest._assetId ) &&
                             ServiceKeyUtil::readString( reader, kMaxTextSize, outRequest._reasonCode ) && ServiceKeyUtil::readString( reader, kMaxMemoSize, outRequest._memo ) &&
                             ServiceKeyUtil::readString( reader, ServiceMailConstant::kMaxTitleSize, outRequest._titleKey ) &&
                             ServiceKeyUtil::readString( reader, ServiceMailConstant::kMaxBodySize, outRequest._body ) &&
                             ServiceKeyUtil::readString( reader, kMaxTextSize, outRequest._cursor );
        if ( bTextOk == false )
            return false;
        outRequest._accountId = reader.readVarUint();
        outRequest._batchId   = reader.readVarUint();
        outRequest._amount    = reader.readVarInt();
        outRequest._untilMs   = reader.readVarInt();
        outRequest._startMs   = reader.readVarInt();
        outRequest._endMs     = reader.readVarInt();
        const int64 maxCount  = reader.readVarInt();
        outRequest._maxCount  = static_cast<int32>( maxCount );
        const bool bEnumOk    = AdminProtocolInternal::readEnum( reader, ServiceSanctionKind::Count, outRequest._sanctionKind ) &&
                             AdminProtocolInternal::readEnum( reader, AdminRole::Count, outRequest._role );
        outRequest._bRefund    = reader.readBool() ? SW_TRUE : SW_FALSE;
        const bool bMaxCountOk = 1 <= maxCount && maxCount <= kMaxListCount;
        return bEnumOk && bMaxCountOk && reader.hasOverflowed() == false;
    }

    void AdminProtocol::writeReply( BitWriter& outWriter, const AdminReply& reply )
    {
        outWriter.writeVarUint( static_cast<uint64>( reply._result ) );
        outWriter.writeBool( reply._bReplayed == SW_TRUE );
        outWriter.writeBool( reply._bOnline == SW_TRUE );
        outWriter.writeVarUint( static_cast<uint64>( reply._targetRole ) );
        outWriter.writeVarInt( reply._processedCount );
        ServiceKeyUtil::writeString( outWriter, reply._nextCursor );
        outWriter.writeVarUint( reply._identity._accountId );
        ServiceKeyUtil::writeString( outWriter, reply._identity._displayName );
        outWriter.writeBool( reply._identity._bGuest == SW_TRUE );
        ServiceKeyUtil::writeString( outWriter, reply._sanction._reasonCode );
        for ( int32 kindIndex = 0; kindIndex < AdminProtocolInternal::kSanctionKindCount; ++kindIndex )
        {
            outWriter.writeVarInt( reply._sanction._arrUntilMs[kindIndex] );
        }
        AdminProtocolInternal::writeBalances( outWriter, reply._listBalance );
        outWriter.writeVarUint( reply._listJournal.size() );
        for ( const AdminJournalLine& line : reply._listJournal )
        {
            AdminProtocolInternal::writeJournal( outWriter, line );
        }
        outWriter.writeVarUint( reply._listAudit.size() );
        for ( const ServiceAuditEntry& entry : reply._listAudit )
        {
            AdminProtocolInternal::writeAudit( outWriter, entry );
        }
    }

    bool AdminProtocol::readReply( BitReader& reader, AdminReply& outReply )
    {
        if ( AdminProtocolInternal::readEnum( reader, AdminResult::Count, outReply._result ) == false )
            return false;
        outReply._bReplayed = reader.readBool() ? SW_TRUE : SW_FALSE;
        outReply._bOnline   = reader.readBool() ? SW_TRUE : SW_FALSE;
        if ( AdminProtocolInternal::readEnum( reader, AdminRole::Count, outReply._targetRole ) == false )
            return false;
        outReply._processedCount = static_cast<int32>( reader.readVarInt() );
        if ( ServiceKeyUtil::readString( reader, kMaxTextSize, outReply._nextCursor ) == false )
            return false;
        outReply._identity._accountId = reader.readVarUint();
        if ( ServiceKeyUtil::readString( reader, kMaxTextSize, outReply._identity._displayName ) == false )
            return false;
        outReply._identity._bGuest = reader.readBool() ? SW_TRUE : SW_FALSE;
        if ( ServiceKeyUtil::readString( reader, kMaxTextSize, outReply._sanction._reasonCode ) == false )
            return false;
        for ( int32 kindIndex = 0; kindIndex < AdminProtocolInternal::kSanctionKindCount; ++kindIndex )
        {
            outReply._sanction._arrUntilMs[kindIndex] = reader.readVarInt();
        }
        if ( AdminProtocolInternal::readBalances( reader, outReply._listBalance ) == false )
            return false;
        const uint64 journalCount = reader.readVarUint();
        if ( journalCount > static_cast<uint64>( kMaxListCount ) )
            return false;
        outReply._listJournal.resize( static_cast<size_t>( journalCount ) );
        for ( AdminJournalLine& line : outReply._listJournal )
        {
            if ( AdminProtocolInternal::readJournal( reader, line ) == false )
                return false;
        }
        const uint64 auditCount = reader.readVarUint();
        if ( auditCount > static_cast<uint64>( kMaxListCount ) )
            return false;
        outReply._listAudit.resize( static_cast<size_t>( auditCount ) );
        for ( ServiceAuditEntry& entry : outReply._listAudit )
        {
            if ( AdminProtocolInternal::readAudit( reader, entry ) == false )
                return false;
        }
        return reader.hasOverflowed() == false;
    }

    AdminResult AdminProtocol::fromErrorCode( uint16 errorCode )
    {
        switch ( errorCode )
        {
            case OnlineError::kOk:
                return AdminResult::Ok;
            case OnlineError::kUnauthenticated:
                return AdminResult::NotSignedIn;
            case OnlineError::kForbidden:
                return AdminResult::Forbidden;
            case OnlineError::kInvalidRequest:
                return AdminResult::InvalidRequest;
            case OnlineError::kConflict:
                return AdminResult::Busy;
            default:
                return AdminResult::Unavailable;
        }
    }

    AdminRole AdminProtocol::getRequiredRole( uint16 method, ServiceSanctionKind sanctionKind )
    {
        switch ( method )
        {
            case AdminMethod::kLookupAccount:
            case AdminMethod::kListAudit:
            {
                return AdminRole::Viewer;
            }
            case AdminMethod::kSetSanction:
            {
                if ( sanctionKind == ServiceSanctionKind::ChatMute )
                    return AdminRole::Support;
                return sanctionKind == ServiceSanctionKind::Suspend ? AdminRole::Operator : AdminRole::Super;
            }
            case AdminMethod::kAdjustAsset:
            case AdminMethod::kSendMail:
            {
                return AdminRole::Operator;
            }
            case AdminMethod::kBulkMail:
            case AdminMethod::kCreateCampaign:
            case AdminMethod::kSetRole:
            {
                return AdminRole::Super;
            }
            default:
            {
                return AdminRole::Count; // 모르는 메서드 — 누구도 갖지 못한 등급
            }
        }
    }
} // namespace sw
