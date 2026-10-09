#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Admin/Server/AdminStoreLogic.h"

#include "Core/String/formatString.h"

#include "GameFramework/Base/Online/Audit/ServiceAuditLog.h"
#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Mail/ServiceMail.h"
#include "GameFramework/Base/Online/Mail/ServiceMailCampaign.h"
#include "GameFramework/Base/Online/Sanction/ServiceSanction.h"
#include "GameFramework/Base/Online/Store/ServiceIdempotency.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct AdminStoreLogicInternal
        {
            static constexpr const utf8* kGmSenderName = "mail.sender.gm";

            static AdminResult finish( AdminReply& outReply, AdminResult result )
            {
                outReply._result = result;
                return result;
            }

            static string makeActor( AccountId adminId ) { return "gm." + ServiceKeyUtil::makeHex64( adminId ); }
            static string makeSubject( AccountId accountId ) { return "acct." + ServiceKeyUtil::makeHex64( accountId ); }

            static string formatAmount( string_view assetId, int64 amount )
            {
                utf8 arrBuffer[constant::kMaxBuffer128];
                formatstring( arrBuffer, constant::kMaxBuffer128, "%#=%#", assetId, amount );
                return string( arrBuffer );
            }

            static string formatSanction( const ServiceSanctionState& state )
            {
                utf8 arrBuffer[constant::kMaxBuffer256];
                formatstring( arrBuffer, constant::kMaxBuffer256, "mute=%# suspend=%# ban=%# reason=%#", state._arrUntilMs[0], state._arrUntilMs[1], state._arrUntilMs[2],
                              state._reasonCode );
                return string( arrBuffer );
            }

            static AdminResult toAdminResult( LedgerResult result )
            {
                switch ( result )
                {
                    case LedgerResult::Ok:
                        return AdminResult::Ok;
                    case LedgerResult::InsufficientFunds:
                        return AdminResult::InsufficientFunds;
                    case LedgerResult::CapExceeded:
                        return AdminResult::CapExceeded;
                    case LedgerResult::Conflict:
                        return AdminResult::Busy;
                    case LedgerResult::JournalKeyReused:
                    case LedgerResult::Invalid:
                        return AdminResult::InvalidRequest;
                    case LedgerResult::Unavailable:
                        return AdminResult::Unavailable;
                }
                return AdminResult::Unavailable;
            }

            static ServiceMailMessage makeGmMail( const AdminCommand& command, AccountId recipient, string idempotencyKey )
            {
                const AdminRequest& request = command._request;
                ServiceMailMessage  message;
                message._recipientAccountId = recipient;
                message._listAttachment     = request._listAttachment;
                message._titleKey           = request._titleKey;
                message._body               = request._body;
                message._senderName         = kGmSenderName;
                message._bLiteralText       = SW_TRUE;
                message._fundingHolder      = LedgerHolder::makeMint();
                message._actorId            = command._adminId;
                message._actorKind          = LedgerActorKind::Admin;
                message._createdMs          = command._nowMs;
                message._expiresMs          = command._nowMs + ServiceMailConstant::kDefaultRetentionMs;
                message._expiryAction       = ServiceMail::getDefaultExpiryAction( message._fundingHolder );
                message._idempotencyKey     = std::move( idempotencyKey );
                return message;
            }

            static AdminResult stageAdjust( IServiceStoreConnection& connection, const AdminCommand& command, ServiceTransaction& inoutTransaction, AdminReply& outReply,
                                            ServiceAuditEntry& inoutAudit )
            {
                const AdminRequest& request   = command._request;
                const bool          bGrant    = request._amount > 0;
                const int64         magnitude = bGrant ? request._amount : -request._amount;
                const LedgerHolder  account   = LedgerHolder::makeAccount( request._accountId );
                LedgerBalance       before;
                if ( Ledger::readBalance( connection, account, request._assetId, before ) != ServiceStoreResult::Ok )
                    return AdminResult::Unavailable;
                const bool            bRefund = bGrant == false && request._bRefund == SW_TRUE;
                LedgerTransferRequest transfer;
                transfer._journalKey = LedgerJournalKey::makeFromIdempotency( LedgerJournalKey::makeAdminScope( command._adminId ), command._keyHigh, command._keyLow );
                transfer._reason     = bGrant ? "admin.grant" : ( bRefund ? "refund.revoke" : "admin.revoke" );
                transfer._memo       = request._memo.substr( 0, static_cast<size_t>( LedgerConstant::kMaxMemoSize ) );
                transfer._pPolicy    = command._pPolicy;
                transfer._timeMs     = command._nowMs;
                transfer._actorId    = command._adminId;
                transfer._actorKind  = LedgerActorKind::Admin;
                transfer._bAllowDebt = bRefund ? SW_TRUE : SW_FALSE; // 환불 회수만 빚 허용 — 이미 쓴 재화도 거둔다
                if ( bGrant )
                    transfer._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), account, request._assetId, magnitude } );
                else
                    transfer._listPosting.push_back( LedgerPosting{ account, LedgerHolder::makeSink(), request._assetId, magnitude } );
                LedgerTransferOutcome outcome;
                const LedgerResult    staged = Ledger::stageTransfer( connection, transfer, inoutTransaction, outcome );
                if ( staged != LedgerResult::Ok )
                    return toAdminResult( staged );
                int64 after = before._amount;
                for ( const LedgerTransferOutcome::HolderBalance& holderBalance : outcome._listHolderBalance )
                {
                    outReply._listBalance.push_back( holderBalance._balance );
                    after = holderBalance._balance._amount;
                }
                inoutAudit._action = transfer._reason;
                inoutAudit._before = formatAmount( request._assetId, before._amount );
                inoutAudit._after  = formatAmount( request._assetId, after );
                return AdminResult::Ok;
            }

            static AdminResult stageSanction( IServiceStoreConnection& connection, const AdminCommand& command, ServiceTransaction& inoutTransaction, AdminReply& outReply,
                                              ServiceAuditEntry& inoutAudit )
            {
                const AdminRequest&  request = command._request;
                ServiceSanctionState state;
                if ( ServiceSanction::readState( connection, request._accountId, state ) != ServiceStoreResult::Ok )
                    return AdminResult::Unavailable;
                inoutAudit._before                                             = formatSanction( state );
                const bool bBan                                                = request._sanctionKind == ServiceSanctionKind::Ban && request._untilMs != 0;
                state._arrUntilMs[static_cast<int32>( request._sanctionKind )] = bBan ? ServiceSanctionState::kPermanentMs : request._untilMs;
                if ( request._reasonCode.empty() == false )
                    state._reasonCode = request._reasonCode;
                ServiceSanction::stageWrite( inoutTransaction, request._accountId, state );
                inoutAudit._action        = "admin.sanction";
                inoutAudit._after         = formatSanction( state );
                outReply._sanction        = state;
                const bool bLocksAccount  = request._sanctionKind != ServiceSanctionKind::ChatMute && request._untilMs > command._nowMs;
                outReply._bRevokeSessions = bLocksAccount ? SW_TRUE : SW_FALSE;
                return AdminResult::Ok;
            }

            static AdminResult stageBulkMail( IServiceStoreConnection& connection, const AdminCommand& command, ServiceTransaction& inoutTransaction, AdminReply& outReply,
                                              ServiceAuditEntry& inoutAudit )
            {
                const AdminRequest& request = command._request;
                for ( const AccountId recipient : request._listAccountId )
                {
                    // 받는 계정마다 배치 키 — 끊긴 뒤 다시 돌리면 이미 보낸 계정은 건너뛴다.
                    string             key = "bulk." + ServiceKeyUtil::makeHex64( request._batchId ) + "." + ServiceKeyUtil::makeHex64( recipient );
                    string             mailKey;
                    bool               bReplayed = false;
                    const LedgerResult staged    = ServiceMail::stageSend( connection, makeGmMail( command, recipient, std::move( key ) ), inoutTransaction, mailKey, bReplayed );
                    if ( staged != LedgerResult::Ok )
                        return toAdminResult( staged );
                    outReply._processedCount += bReplayed ? 0 : 1;
                }
                inoutAudit._action  = "admin.bulk_mail";
                inoutAudit._subject = "batch." + ServiceKeyUtil::makeHex64( request._batchId );
                inoutAudit._after   = formatAmount( "sent", outReply._processedCount );
                return AdminResult::Ok;
            }

            static AdminResult stageSetRole( IServiceStoreConnection& connection, const AdminCommand& command, ServiceTransaction& inoutTransaction, ServiceAuditEntry& inoutAudit )
            {
                const AdminRequest& request = command._request;
                if ( request._accountId == command._adminId )
                    return AdminResult::Forbidden; // 자기 등급은 못 바꾼다(마지막 Super 가 스스로 잠그지 않게)
                AdminRole current = AdminRole::None;
                uint64    version = 0;
                if ( AdminStoreLogic::readRole( connection, request._accountId, current, version ) != ServiceStoreResult::Ok )
                    return AdminResult::Unavailable;
                const string key = ServiceKeyUtil::makeHex64( request._accountId );
                if ( request._role == AdminRole::None )
                {
                    if ( version != 0 )
                        inoutTransaction.erase( AdminStoreLogic::getRoleTable(), key, version );
                }
                else
                {
                    inoutTransaction.put( AdminStoreLogic::getRoleTable(), key, vector<uint8>{ static_cast<uint8>( request._role ) }, version );
                }
                inoutAudit._action = "admin.role";
                inoutAudit._before = toString( current );
                inoutAudit._after  = toString( request._role );
                return AdminResult::Ok;
            }

            /** @brief 바꾸는 명령 하나의 효과를 @p inoutTransaction 에 붙입니다(감사 줄 포함). */
            static AdminResult stageEffect( IServiceStoreConnection& connection, const AdminCommand& command, ServiceTransaction& inoutTransaction, AdminReply& outReply )
            {
                const AdminRequest& request = command._request;
                ServiceAuditEntry   audit;
                audit._actor       = makeActor( command._adminId );
                audit._subject     = makeSubject( request._accountId );
                audit._memo        = request._memo;
                audit._timeMs      = command._nowMs;
                AdminResult staged = AdminResult::InvalidRequest;
                switch ( command._method )
                {
                    case AdminMethod::kAdjustAsset:
                    {
                        staged = stageAdjust( connection, command, inoutTransaction, outReply, audit );
                        break;
                    }
                    case AdminMethod::kSetSanction:
                    {
                        staged = stageSanction( connection, command, inoutTransaction, outReply, audit );
                        break;
                    }
                    case AdminMethod::kSendMail:
                    {
                        string             mailKey;
                        bool               bReplayed = false;
                        const string       key       = "gm." + ServiceKeyUtil::makeHex64( command._adminId ) + "." + ServiceKeyUtil::makeHex64( command._keyLow );
                        const LedgerResult sent      = ServiceMail::stageSend( connection, makeGmMail( command, request._accountId, key ), inoutTransaction, mailKey, bReplayed );
                        staged                       = toAdminResult( sent );
                        audit._action                = "admin.mail";
                        audit._after                 = mailKey;
                        break;
                    }
                    case AdminMethod::kBulkMail:
                    {
                        staged = stageBulkMail( connection, command, inoutTransaction, outReply, audit );
                        break;
                    }
                    case AdminMethod::kCreateCampaign:
                    {
                        ServiceMailCampaign campaign;
                        campaign._campaignId     = request._batchId;
                        campaign._listAttachment = request._listAttachment;
                        campaign._titleKey       = request._titleKey;
                        campaign._body           = request._body;
                        campaign._senderName     = kGmSenderName;
                        campaign._bLiteralText   = SW_TRUE;
                        campaign._startMs        = request._startMs;
                        campaign._endMs          = request._endMs;
                        campaign._actorId        = command._adminId;
                        staged                   = ServiceMailCampaignTable::stageCreate( campaign, inoutTransaction ) ? AdminResult::Ok : AdminResult::InvalidRequest;
                        audit._action            = "admin.campaign";
                        audit._subject           = "campaign." + ServiceKeyUtil::makeHex64( request._batchId );
                        break;
                    }
                    case AdminMethod::kSetRole:
                    {
                        staged = stageSetRole( connection, command, inoutTransaction, audit );
                        break;
                    }
                    default:
                    {
                        break;
                    }
                }
                if ( staged != AdminResult::Ok )
                    return staged;
                ServiceAuditLog::stageEntry( inoutTransaction, audit, command._keyHigh, command._keyLow );
                return AdminResult::Ok;
            }

            static bool isWellFormed( const AdminCommand& command )
            {
                const AdminRequest& request = command._request;
                if ( AdminMethod::isMutating( command._method ) == false )
                    return true;
                const bool bKeyOk  = ( command._keyHigh | command._keyLow ) != 0;
                const bool bMemoOk = request._memo.empty() == false && request._memo.size() <= static_cast<size_t>( AdminProtocol::kMaxMemoSize );
                if ( bKeyOk == false || bMemoOk == false )
                    return false;
                const bool bHasAccount = request._accountId != kInvalidAccountId;
                switch ( command._method )
                {
                    case AdminMethod::kAdjustAsset:
                        return bHasAccount && request._amount != 0 && LedgerUtil::isValidAssetId( request._assetId );
                    case AdminMethod::kSetSanction:
                        return bHasAccount && request._untilMs >= 0;
                    case AdminMethod::kSendMail:
                        return bHasAccount;
                    case AdminMethod::kBulkMail:
                        return request._listAccountId.empty() == false && static_cast<int32>( request._listAccountId.size() ) <= AdminProtocol::kMaxBulkCount &&
                               request._batchId != 0;
                    case AdminMethod::kSetRole:
                        return bHasAccount;
                    default:
                        return true;
                }
            }

            static AdminResult lookup( IServiceStoreConnection& connection, const AdminRequest& request, AdminReply& outReply )
            {
                if ( request._accountId == kInvalidAccountId )
                    return AdminResult::UnknownAccount;
                const LedgerHolder account = LedgerHolder::makeAccount( request._accountId );
                if ( Ledger::listBalances( connection, account, outReply._listBalance ) != ServiceStoreResult::Ok )
                    return AdminResult::Unavailable;
                if ( ServiceSanction::readState( connection, request._accountId, outReply._sanction ) != ServiceStoreResult::Ok )
                    return AdminResult::Unavailable;
                uint64 roleVersion = 0;
                if ( AdminStoreLogic::readRole( connection, request._accountId, outReply._targetRole, roleVersion ) != ServiceStoreResult::Ok )
                    return AdminResult::Unavailable;
                vector<LedgerJournalEntry> listEntry;
                string                     ignoredCursor;
                if ( Ledger::listHistory( connection, account, "", AdminProtocol::kMaxLookupCount, listEntry, ignoredCursor ) != ServiceStoreResult::Ok )
                    return AdminResult::Unavailable;
                for ( const LedgerJournalEntry& entry : listEntry )
                {
                    AdminJournalLine& line = outReply._listJournal.emplace_back();
                    line._reason           = entry._reason;
                    line._memo             = entry._memo;
                    line._timeMs           = entry._timeMs;
                    line._actorId          = entry._actorId;
                    line._actorKind        = entry._actorKind;
                    for ( const LedgerPosting& posting : entry._listPosting )
                    {
                        const int64 sign = posting._to == account ? 1 : ( posting._from == account ? -1 : 0 );
                        if ( sign != 0 )
                            line._listChange.push_back( LedgerBalance{ posting._assetId, sign * posting._amount, 0 } );
                    }
                }
                const string             subject = makeSubject( request._accountId ) + "/";
                const ServiceStoreResult listed  = ServiceAuditLog::listEntries( connection, subject, "", AdminProtocol::kMaxLookupCount, outReply._listAudit, ignoredCursor );
                return listed == ServiceStoreResult::Ok ? AdminResult::Ok : AdminResult::Unavailable;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const hashed_string& AdminStoreLogic::getRoleTable()
    {
        static const hashed_string s_table{ "admin_role" };
        return s_table;
    }

    ServiceStoreResult AdminStoreLogic::readRole( IServiceStoreConnection& connection, AccountId accountId, AdminRole& outRole, uint64& outVersion )
    {
        outRole    = AdminRole::None;
        outVersion = 0;
        ServiceRecord            record;
        const ServiceStoreResult read = connection.readRecord( getRoleTable(), ServiceKeyUtil::makeHex64( accountId ), record );
        if ( read == ServiceStoreResult::NotFound )
            return ServiceStoreResult::Ok;
        if ( read != ServiceStoreResult::Ok )
            return read;
        const bool bValid = record._bytes.size() == 1 && record._bytes[0] < static_cast<uint8>( AdminRole::Count );
        outRole           = bValid ? static_cast<AdminRole>( record._bytes[0] ) : AdminRole::None;
        outVersion        = record._version;
        return ServiceStoreResult::Ok;
    }

    AdminResult AdminStoreLogic::execute( IServiceStoreConnection& connection, const AdminCommand& command, AdminReply& outReply )
    {
        outReply          = AdminReply{};
        AdminRole role    = AdminRole::None;
        uint64    version = 0;
        if ( readRole( connection, command._adminId, role, version ) != ServiceStoreResult::Ok )
            return AdminStoreLogicInternal::finish( outReply, AdminResult::Unavailable );
        const AdminRole required = AdminProtocol::getRequiredRole( command._method, command._request._sanctionKind );
        if ( required == AdminRole::Count || role < required )
            return AdminStoreLogicInternal::finish( outReply, AdminResult::Forbidden );
        if ( AdminStoreLogicInternal::isWellFormed( command ) == false )
            return AdminStoreLogicInternal::finish( outReply, AdminResult::InvalidRequest );
        if ( command._method == AdminMethod::kLookupAccount )
            return AdminStoreLogicInternal::finish( outReply, AdminStoreLogicInternal::lookup( connection, command._request, outReply ) );
        if ( command._method == AdminMethod::kListAudit )
        {
            const ServiceStoreResult listed = ServiceAuditLog::listEntries( connection, command._request._subject, command._request._cursor, command._request._maxCount,
                                                                            outReply._listAudit, outReply._nextCursor );
            return AdminStoreLogicInternal::finish( outReply, listed == ServiceStoreResult::Ok ? AdminResult::Ok : AdminResult::Unavailable );
        }

        const string scope = AdminStoreLogicInternal::makeActor( command._adminId );
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            vector<uint8>            listStoredReply;
            const ServiceStoreResult found = ServiceIdempotency::findReply( connection, scope, command._keyHigh, command._keyLow, listStoredReply );
            if ( found == ServiceStoreResult::Ok )
            {
                // 지난 결과 — 멱등 기록에는 결과 한 바이트만 있다(세션 끊기 표시는 실리지 않아 재시도가 두 번 끊지 않는다).
                outReply._bReplayed  = SW_TRUE;
                const bool bStoredOk = listStoredReply.size() == 1 && listStoredReply[0] < static_cast<uint8>( AdminResult::Count );
                return AdminStoreLogicInternal::finish( outReply, bStoredOk ? static_cast<AdminResult>( listStoredReply[0] ) : AdminResult::Ok );
            }
            if ( found != ServiceStoreResult::NotFound )
                return AdminStoreLogicInternal::finish( outReply, AdminResult::Unavailable );

            AdminReply         attemptReply;
            ServiceTransaction transaction;
            const AdminResult  staged = AdminStoreLogicInternal::stageEffect( connection, command, transaction, attemptReply );
            if ( staged != AdminResult::Ok )
            {
                outReply = attemptReply;
                return AdminStoreLogicInternal::finish( outReply, staged );
            }
            ServiceIdempotency::addReply( transaction, scope, command._keyHigh, command._keyLow, vector<uint8>{ static_cast<uint8>( AdminResult::Ok ) } );
            const ServiceStoreResult committed = connection.commit( transaction );
            if ( committed == ServiceStoreResult::Ok )
            {
                outReply = attemptReply;
                return AdminStoreLogicInternal::finish( outReply, AdminResult::Ok );
            }
            if ( committed == ServiceStoreResult::Invalid )
                return AdminStoreLogicInternal::finish( outReply, AdminResult::InvalidRequest );
            if ( committed == ServiceStoreResult::Unavailable )
            {
                // 적용됐는지 모른다 — 멱등 기록이 있으면 들어간 것이다.
                const ServiceStoreResult recheck = ServiceIdempotency::findReply( connection, scope, command._keyHigh, command._keyLow, listStoredReply );
                if ( recheck != ServiceStoreResult::Ok )
                    return AdminStoreLogicInternal::finish( outReply, AdminResult::Unavailable );
                outReply            = attemptReply;
                outReply._bReplayed = SW_TRUE;
                return AdminStoreLogicInternal::finish( outReply, AdminResult::Ok );
            }
            // Conflict — 다음 돌기의 멱등 기록 확인이 "이미 됐다" 를 가린다
        }
        return AdminStoreLogicInternal::finish( outReply, AdminResult::Busy );
    }

    AdminResult AdminStoreLogic::seedRole( IServiceStoreConnection& connection, AccountId accountId, AdminRole role, int64 nowMs )
    {
        AdminRole current = AdminRole::None;
        uint64    version = 0;
        if ( readRole( connection, accountId, current, version ) != ServiceStoreResult::Ok )
            return AdminResult::Unavailable;
        if ( version != 0 )
            return AdminResult::Ok; // 이미 있다 — 설정 파일이 운영 중 바뀐 등급을 덮지 않는다
        ServiceTransaction transaction;
        transaction.put( getRoleTable(), ServiceKeyUtil::makeHex64( accountId ), vector<uint8>{ static_cast<uint8>( role ) }, ServiceRecord::kAbsentVersion );
        ServiceAuditEntry audit;
        audit._actor   = "system.bootstrap";
        audit._action  = "admin.role";
        audit._subject = AdminStoreLogicInternal::makeSubject( accountId );
        audit._after   = toString( role );
        audit._timeMs  = nowMs;
        ServiceAuditLog::stageEntry( transaction, audit, accountId, static_cast<uint64>( nowMs ) );
        const ServiceStoreResult committed = connection.commit( transaction );
        return committed == ServiceStoreResult::Ok || committed == ServiceStoreResult::Conflict ? AdminResult::Ok : AdminResult::Unavailable;
    }
} // namespace sw
