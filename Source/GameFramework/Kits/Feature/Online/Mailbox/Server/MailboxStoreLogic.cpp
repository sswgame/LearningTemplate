#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Mailbox/Server/MailboxStoreLogic.h"

#include "Core/String/StringUtil.h"

#include "GameFramework/Runtime/Online/Ledger/Server/Ledger.h"
#include "GameFramework/Runtime/Online/Mail/Server/ServiceMail.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStoreUtil.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct MailboxStoreLogicInternal
        {
            static MailView makeView( const ServiceMailRecord& record )
            {
                MailView view;
                view._mailKey        = record._mailKey;
                view._titleKey       = record._message._titleKey;
                view._body           = record._message._body;
                view._senderName     = record._message._senderName;
                view._createdMs      = record._message._createdMs;
                view._expiresMs      = record._message._expiresMs;
                view._state          = record._state;
                view._bLiteralText   = record._message._bLiteralText;
                view._listAttachment = record._message._listAttachment;
                return view;
            }

            static bool isExpired( const ServiceMailMessage& message, int64 nowMs ) { return message._expiresMs > 0 && message._expiresMs <= nowMs; }

            static string makeAccountPrefix( uint64 accountID )
            {
                string prefix = ServiceKeyUtil::makeHex64( accountID );
                prefix.push_back( '/' );
                return prefix;
            }

            /** @brief 우편 하나를 읽어 해독합니다. 받는 계정 확인도 포함합니다. */
            static MailboxServiceResult readMail( IServiceStoreConnection& connection, uint64 accountID, string_view mailKey, ServiceMailRecord& outRecord )
            {
                uint64 recipient = 0;
                if ( ServiceMail::parseRecipient( mailKey, recipient ) == false || recipient != accountID )
                    return MailboxResult::NotFound;
                ServiceRecord            record;
                const ServiceStoreResult read = connection.readRecord( ServiceMail::getMailTable(), mailKey, record );
                if ( read == ServiceStoreResult::NotFound )
                    return MailboxResult::NotFound;
                if ( read != ServiceStoreResult::Ok || ServiceMail::decodeRecord( record._bytes, outRecord ) == false )
                    return MailboxServiceResult::fail( OnlineError::kUnavailable );
                outRecord._mailKey = string( mailKey );
                outRecord._version = record._version;
                return MailboxResult::Ok;
            }

            static MailboxServiceResult toResult( LedgerResult result )
            {
                switch ( result )
                {
                    case LedgerResult::Ok:
                        return MailboxResult::Ok;
                    case LedgerResult::CapExceeded:
                        return MailboxResult::CapExceeded;
                    case LedgerResult::Conflict:
                        return MailboxServiceResult::fail( OnlineError::kBusy );
                    case LedgerResult::AccountDeleted:
                        return MailboxServiceResult::fail( OnlineError::kInvalidRequest ); // 탈퇴한 계정에는 다시 해도 갈 수 없다
                    case LedgerResult::InsufficientFunds:
                    case LedgerResult::JournalKeyReused:
                    case LedgerResult::Invalid:
                    case LedgerResult::Unavailable:
                        return MailboxServiceResult::fail( OnlineError::kUnavailable );
                }
                return MailboxServiceResult::fail( OnlineError::kUnavailable );
            }

            static void collectAccountBalances( const LedgerTransferResult& serviceResult, uint64 accountID, vector<LedgerBalance>& inoutListBalance )
            {
                const LedgerHolder account = LedgerHolder::makeAccount( accountID );
                for ( const LedgerTransferResult::HolderBalance& holderBalance : serviceResult._listHolderBalance )
                {
                    if ( holderBalance._holder != account )
                        continue;
                    bool bReplaced = false;
                    for ( LedgerBalance& existing : inoutListBalance )
                    {
                        if ( existing._assetID == holderBalance._balance._assetID )
                        {
                            existing  = holderBalance._balance;
                            bReplaced = true;
                        }
                    }
                    if ( bReplaced == false )
                        inoutListBalance.push_back( holderBalance._balance );
                }
            }

            static MailboxServiceResult claimCampaign( IServiceStoreConnection& connection, uint64 campaignID, const MailboxClaimInput& input, MailboxClaimReply& outReply )
            {
                ServiceRecord            campaignRecord;
                const ServiceStoreResult read = connection.readRecord( ServiceMailCampaign::getCampaignTable(), ServiceKeyUtil::makeHex64( campaignID ), campaignRecord );
                if ( read == ServiceStoreResult::NotFound )
                    return MailboxResult::NotFound;
                ServiceMailCampaign campaign;
                if ( read != ServiceStoreResult::Ok || ServiceMailCampaign::decode( campaignRecord._bytes, campaign ) == false )
                    return MailboxServiceResult::fail( OnlineError::kUnavailable );
                if ( campaign.isActive( input._nowMs ) == false )
                    return MailboxResult::Expired;
                const string          claimKey = ServiceMailCampaign::makeClaimKey( campaignID, input._accountID );
                LedgerTransferRequest request;
                if ( LedgerJournalKey::makeFromToken( "mail.campaign", claimKey, request._journalKey ) == false )
                    return MailboxServiceResult::fail( OnlineError::kInvalidRequest );
                request._reason    = "mail.campaign";
                request._memo      = campaign._titleKey.substr( 0, static_cast<size_t>( LedgerConstant::kMaxMemoSize ) );
                request._pPolicy   = input._pPolicy;
                request._timeMs    = input._nowMs;
                request._actorID   = input._accountID;
                request._actorKind = LedgerActorKind::Player;
                for ( const ServiceMailAttachment& attachment : campaign._listAttachment )
                {
                    request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), LedgerHolder::makeAccount( input._accountID ), attachment._assetID, attachment._amount } );
                }
                for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
                {
                    ServiceTransaction   transaction;
                    LedgerTransferResult serviceResult;
                    const LedgerResult   staged = Ledger::stageTransfer( connection, request, transaction, serviceResult );
                    if ( staged != LedgerResult::Ok )
                        return toResult( staged );
                    if ( serviceResult._bReplayed == SW_TRUE )
                        return MailboxResult::AlreadyClaimed;
                    transaction.put( ServiceMailCampaign::getClaimTable(), claimKey, vector<uint8>{}, ServiceRecord::kAbsentVersion );
                    const ServiceStoreResult committed = connection.commit( transaction );
                    if ( committed == ServiceStoreResult::Ok )
                    {
                        collectAccountBalances( serviceResult, input._accountID, outReply._listBalance );
                        ++outReply._claimedCount;
                        return MailboxResult::Ok;
                    }
                    const LedgerResult resolved = Ledger::resolveConflict( connection, request, serviceResult );
                    if ( resolved == LedgerResult::Ok )
                        return MailboxResult::AlreadyClaimed;
                    if ( resolved != LedgerResult::Conflict || committed == ServiceStoreResult::Unavailable )
                        return MailboxServiceResult::fail( OnlineError::kUnavailable );
                }
                return MailboxServiceResult::fail( OnlineError::kBusy );
            }

            /** @brief 이동으로 바뀐 계정 보유자의 잔액을 모읍니다. */
            static void collectAccountChanges( const LedgerTransferResult& serviceResult, vector<MailboxBalanceChange>& outListChange )
            {
                for ( const LedgerTransferResult::HolderBalance& holderBalance : serviceResult._listHolderBalance )
                {
                    if ( holderBalance._holder._kind == LedgerHolderKind::Account )
                        outListChange.push_back( MailboxBalanceChange{ holderBalance._holder._accountID, holderBalance._balance } );
                }
            }

            /** @brief 만료 인덱스 하나를 처리할 트랜잭션을 붙입니다. 실패하면 false 를 반환하며 그 인덱스는 다음 정리가 다시 봅니다. */
            static bool stageExpiry( IServiceStoreConnection& connection, const ServiceRecord& index, string_view mailKey, int64 nowMs, ServiceTransaction& inoutTransaction,
                                     bool& outbReturned, bool& outbDiscarded, vector<MailboxBalanceChange>& outListChange )
            {
                outbReturned  = false;
                outbDiscarded = false;
                inoutTransaction.erase( ServiceMail::getExpiryTable(), index._key, index._version );
                ServiceRecord            record;
                const ServiceStoreResult read = connection.readRecord( ServiceMail::getMailTable(), mailKey, record );
                if ( read != ServiceStoreResult::Ok && read != ServiceStoreResult::NotFound )
                    return false;
                ServiceMailRecord mail;
                if ( read == ServiceStoreResult::NotFound || ServiceMail::decodeRecord( record._bytes, mail ) == false )
                    return true; // 없는 우편이므로 인덱스만 지운다
                if ( mail._state == ServiceMailState::Claimed )
                {
                    // 받은 뒤 보관 기간이 지났다. 첨부는 이미 받는 계정에 있고 수령 분개는 원장에 남으므로 우편만 지운다
                    if ( isExpired( mail._message, nowMs ) )
                        inoutTransaction.erase( ServiceMail::getMailTable(), mailKey, record._version );
                    return true;
                }
                inoutTransaction.erase( ServiceMail::getMailTable(), mailKey, record._version );
                const bool bEscrowed = mail._message._fundingHolder._kind != LedgerHolderKind::Mint && mail._message._listAttachment.empty() == false;
                if ( bEscrowed == false )
                    return true;
                const LedgerHolder escrow    = ServiceMail::makeEscrowHolder( mailKey );
                outbReturned                 = mail._message._expiryAction == ServiceMailExpiryAction::ReturnToSender;
                outbDiscarded                = outbReturned == false;
                const LedgerHolder    target = outbReturned ? mail._message._fundingHolder : LedgerHolder::makeSink();
                LedgerTransferRequest request;
                if ( LedgerJournalKey::makeFromToken( "mail.expire", escrow._escrowToken, request._journalKey ) == false )
                    return false;
                request._reason    = outbReturned ? "mail.return" : "mail.expire";
                request._timeMs    = nowMs;
                request._actorKind = LedgerActorKind::System;
                request._pPolicy   = nullptr; // 돌려주기는 상한을 보지 않는다. 보낸 쪽이 가득 차도 재화가 사라지면 안 되기 때문이다
                for ( const ServiceMailAttachment& attachment : mail._message._listAttachment )
                {
                    request._listPosting.push_back( LedgerPosting{ escrow, target, attachment._assetID, attachment._amount } );
                }
                LedgerTransferResult serviceResult;
                LedgerResult         staged = Ledger::stageTransfer( connection, request, inoutTransaction, serviceResult );
                if ( staged == LedgerResult::AccountDeleted && outbReturned )
                {
                    // 보낸 계정이 탈퇴로 지워졌다 — 돌려줄 곳이 없으므로 버리는 만료와 같이 소각한다(같은 분개 키)
                    outbReturned    = false;
                    outbDiscarded   = true;
                    request._reason = "mail.expire";
                    for ( LedgerPosting& posting : request._listPosting )
                    {
                        posting._to = LedgerHolder::makeSink();
                    }
                    staged = Ledger::stageTransfer( connection, request, inoutTransaction, serviceResult );
                }
                if ( staged != LedgerResult::Ok || serviceResult._bReplayed == SW_TRUE )
                    return false;
                if ( outbReturned )
                    collectAccountChanges( serviceResult, outListChange );
                return true;
            }

            static constexpr int32 kErasePageSize = 32; ///< 탈퇴 정리가 한 번에 읽는 레코드 수

            /** @brief 지운 계정의 받은 우편 하나를 지우는 쓰기를 붙입니다. 받지 않은 맡긴 첨부는 소각합니다. 읽지 못하는 우편도 지웁니다(지운 계정의 것이다). */
            [[nodiscard]] static bool stageEraseMail( IServiceStoreConnection& connection, const ServiceRecord& record, int64 nowMs, ServiceTransaction& inoutTransaction )
            {
                inoutTransaction.erase( ServiceMail::getMailTable(), record._key, record._version );
                ServiceMailRecord mail;
                if ( ServiceMail::decodeRecord( record._bytes, mail ) == false )
                {
                    SW_LOG_WARNING_ID( "MailboxStoreLogic.ErasedMailUnreadable", "The mail '%#' of a deleted account is unreadable - it is erased without its indexes", record._key.c_str() );
                    return true;
                }
                if ( mail._message._expiresMs > 0 )
                    inoutTransaction.erase( ServiceMail::getExpiryTable(), ServiceMail::makeExpiryKey( mail._message._expiresMs, record._key ) );
                inoutTransaction.erase( ServiceMail::getNotificationTable(), ServiceMail::makeExpiryKey( mail._message._createdMs, record._key ) ); // 알림 키는 만든 시각 + 우편 키다
                const bool bEscrowed = mail._state != ServiceMailState::Claimed && mail._message._fundingHolder._kind != LedgerHolderKind::Mint && mail._message._listAttachment.empty() == false;
                if ( bEscrowed == false )
                    return true;
                const LedgerHolder    escrow = ServiceMail::makeEscrowHolder( record._key );
                LedgerTransferRequest request;
                if ( LedgerJournalKey::makeFromToken( "mail.expire", escrow._escrowToken, request._journalKey ) == false )
                    return false;
                request._reason    = "mail.expire";
                request._timeMs    = nowMs;
                request._actorKind = LedgerActorKind::System;
                for ( const ServiceMailAttachment& attachment : mail._message._listAttachment )
                {
                    request._listPosting.push_back( LedgerPosting{ escrow, LedgerHolder::makeSink(), attachment._assetID, attachment._amount } );
                }
                LedgerTransferResult serviceResult;
                return Ledger::stageTransfer( connection, request, inoutTransaction, serviceResult ) == LedgerResult::Ok;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    MailboxServiceResult MailboxStoreLogic::listMail( IServiceStoreConnection& connection, uint64 accountID, int64 nowMs, const vector<ServiceMailCampaign>& listActiveCampaign,
                                                      const MailboxListRequest& request, MailboxListReply& outReply )
    {
        outReply = MailboxListReply{};
        if ( request._cursor.empty() )
        {
            for ( const ServiceMailCampaign& campaign : listActiveCampaign )
            {
                if ( campaign.isActive( nowMs ) == false )
                    continue;
                ServiceRecord            claimRecord;
                const ServiceStoreResult claimed =
                    connection.readRecord( ServiceMailCampaign::getClaimTable(), ServiceMailCampaign::makeClaimKey( campaign._campaignID, accountID ), claimRecord );
                if ( claimed == ServiceStoreResult::Ok )
                    continue;
                if ( claimed != ServiceStoreResult::NotFound )
                    return MailboxServiceResult::fail( OnlineError::kUnavailable );
                MailView& view       = outReply._listMail.emplace_back();
                view._mailKey        = string( MailboxProtocol::kCampaignPrefix ) + ServiceKeyUtil::makeHex64( campaign._campaignID );
                view._titleKey       = campaign._titleKey;
                view._body           = campaign._body;
                view._senderName     = campaign._senderName;
                view._createdMs      = campaign._startMs;
                view._expiresMs      = campaign._endMs;
                view._bLiteralText   = campaign._bLiteralText;
                view._bCampaign      = SW_TRUE;
                view._listAttachment = campaign._listAttachment;
            }
        }
        vector<ServiceRecord>    listRecord;
        const ServiceStoreResult listed = connection.listRecords( ServiceMail::getMailTable(), MailboxStoreLogicInternal::makeAccountPrefix( accountID ), request._cursor,
                                                                  request._maxCount, true, listRecord );
        if ( listed != ServiceStoreResult::Ok )
            return MailboxServiceResult::fail( OnlineError::kUnavailable );
        for ( const ServiceRecord& record : listRecord )
        {
            ServiceMailRecord mail;
            if ( ServiceMail::decodeRecord( record._bytes, mail ) == false )
            {
                SW_LOG_ERROR_ID( "MailboxStoreLogic.MailUnreadable", "Mail '%#' is unreadable", record._key );
                continue;
            }
            if ( MailboxStoreLogicInternal::isExpired( mail._message, nowMs ) )
                continue; // 곧 정리될 우편이므로 목록에는 보이지 않는다
            mail._mailKey = record._key;
            outReply._listMail.push_back( MailboxStoreLogicInternal::makeView( mail ) );
        }
        if ( static_cast<int32>( listRecord.size() ) == request._maxCount )
            outReply._nextCursor = listRecord.back()._key;
        return MailboxResult::Ok;
    }

    MailboxServiceResult MailboxStoreLogic::markRead( IServiceStoreConnection& connection, uint64 accountID, string_view mailKey )
    {
        ServiceMailRecord          mail;
        const MailboxServiceResult read = MailboxStoreLogicInternal::readMail( connection, accountID, mailKey, mail );
        if ( read != MailboxResult::Ok || mail._state != ServiceMailState::Unread )
            return read;
        mail._state = ServiceMailState::Read;
        ServiceTransaction transaction;
        transaction.put( ServiceMail::getMailTable(), mail._mailKey, ServiceMail::encodeRecord( mail ), mail._version );
        const ServiceStoreResult committed = connection.commit( transaction );
        if ( committed == ServiceStoreResult::Conflict )
            return MailboxResult::Ok; // 그사이 받았거나 읽었으므로 읽음 표시는 덮어쓰지 않는다
        return committed == ServiceStoreResult::Ok ? MailboxServiceResult{ MailboxResult::Ok } : MailboxServiceResult::fail( OnlineError::kUnavailable );
    }

    MailboxServiceResult MailboxStoreLogic::claim( IServiceStoreConnection& connection, const MailboxClaimInput& input, MailboxClaimReply& outReply )
    {
        const string_view mailKey = input._mailKey;
        if ( StringUtil::startsWith( mailKey, MailboxProtocol::kCampaignPrefix ) )
        {
            uint64 campaignID = 0;
            if ( ServiceKeyUtil::parseHex64( mailKey.substr( string_view( MailboxProtocol::kCampaignPrefix ).size() ), campaignID ) == false )
                return MailboxResult::NotFound;
            return MailboxStoreLogicInternal::claimCampaign( connection, campaignID, input, outReply );
        }
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            ServiceMailRecord          mail;
            const MailboxServiceResult read = MailboxStoreLogicInternal::readMail( connection, input._accountID, mailKey, mail );
            if ( read != MailboxResult::Ok )
                return read;
            if ( mail._state == ServiceMailState::Claimed )
                return MailboxResult::AlreadyClaimed;
            if ( MailboxStoreLogicInternal::isExpired( mail._message, input._nowMs ) )
                return MailboxResult::Expired;
            if ( input._claimedRetentionMs <= 0 )
                return MailboxServiceResult::fail( OnlineError::kInvalidRequest ); // 받은 우편의 보관 기간은 설정 필수 값이다

            ServiceTransaction    transaction;
            LedgerTransferRequest request;
            LedgerTransferResult  serviceResult;
            const bool            bHasAttachment = mail._message._listAttachment.empty() == false;
            if ( bHasAttachment )
            {
                const LedgerHolder escrow  = ServiceMail::makeEscrowHolder( mailKey );
                const LedgerHolder funding = mail._message._fundingHolder._kind == LedgerHolderKind::Mint ? LedgerHolder::makeMint() : escrow;
                if ( LedgerJournalKey::makeFromToken( "mail.claim", escrow._escrowToken, request._journalKey ) == false )
                    return MailboxServiceResult::fail( OnlineError::kInvalidRequest );
                request._reason    = "mail.claim";
                request._memo      = mail._message._titleKey.substr( 0, static_cast<size_t>( LedgerConstant::kMaxMemoSize ) );
                request._pPolicy   = input._pPolicy;
                request._timeMs    = input._nowMs;
                request._actorID   = input._accountID;
                request._actorKind = LedgerActorKind::Player;
                for ( const ServiceMailAttachment& attachment : mail._message._listAttachment )
                {
                    request._listPosting.push_back( LedgerPosting{ funding, LedgerHolder::makeAccount( input._accountID ), attachment._assetID, attachment._amount } );
                }
                const LedgerResult staged = Ledger::stageTransfer( connection, request, transaction, serviceResult );
                if ( staged != LedgerResult::Ok )
                {
                    if ( staged == LedgerResult::InsufficientFunds )
                        SW_LOG_ERROR_ID( "MailboxStoreLogic.MailEscrowHoldsLess", "Mail '%#' escrow holds less than its attachments", mailKey );
                    return MailboxStoreLogicInternal::toResult( staged );
                }
                if ( serviceResult._bReplayed == SW_TRUE )
                    return MailboxResult::AlreadyClaimed; // 분개와 상태는 한 트랜잭션이라 여기에 올 수 없다. 방어 코드이다
            }
            // 만료를 "받은 뒤 보관 기간" 으로 다시 건다. 만료 정리가 그때 받은 우편을 지우고, 목록은 그때부터 보이지 않는다
            const int64 purgeMs = input._nowMs + input._claimedRetentionMs;
            if ( mail._message._expiresMs != purgeMs )
            {
                if ( mail._message._expiresMs > 0 )
                    transaction.erase( ServiceMail::getExpiryTable(), ServiceMail::makeExpiryKey( mail._message._expiresMs, mailKey ) );
                transaction.put( ServiceMail::getExpiryTable(), ServiceMail::makeExpiryKey( purgeMs, mailKey ), vector<uint8>{}, ServiceRecord::kAbsentVersion );
            }
            mail._state              = ServiceMailState::Claimed;
            mail._message._expiresMs = purgeMs;
            transaction.put( ServiceMail::getMailTable(), mail._mailKey, ServiceMail::encodeRecord( mail ), mail._version );
            const ServiceStoreResult committed = connection.commit( transaction );
            if ( committed == ServiceStoreResult::Ok )
            {
                MailboxStoreLogicInternal::collectAccountBalances( serviceResult, input._accountID, outReply._listBalance );
                ++outReply._claimedCount;
                return MailboxResult::Ok;
            }
            if ( committed == ServiceStoreResult::Invalid )
                return MailboxServiceResult::fail( OnlineError::kInvalidRequest );
            if ( bHasAttachment )
            {
                const LedgerResult resolved = Ledger::resolveConflict( connection, request, serviceResult );
                if ( resolved == LedgerResult::Ok )
                {
                    // 응답만 잃었거나(이번 수령이 들어갔다) 다른 기기가 먼저 받았다. 어느 쪽이든 원장은 한 번만 움직인다.
                    MailboxStoreLogicInternal::collectAccountBalances( serviceResult, input._accountID, outReply._listBalance );
                    return MailboxResult::Ok;
                }
                if ( resolved == LedgerResult::Unavailable || committed == ServiceStoreResult::Unavailable )
                    return MailboxServiceResult::fail( OnlineError::kUnavailable );
            }
            else if ( committed == ServiceStoreResult::Unavailable )
            {
                return MailboxServiceResult::fail( OnlineError::kUnavailable );
            }
            // Conflict 이다. 그사이 읽음 표시 등으로 버전이 바뀌었으므로 다시 읽고 다시 시도한다
        }
        return MailboxServiceResult::fail( OnlineError::kBusy );
    }

    MailboxServiceResult MailboxStoreLogic::claimAll( IServiceStoreConnection& connection, uint64 accountID, int64 nowMs, int64 claimedRetentionMs, const ILedgerPolicy* pPolicy,
                                                      MailboxClaimReply& outReply )
    {
        outReply = MailboxClaimReply{};
        vector<ServiceRecord>    listRecord;
        const ServiceStoreResult listed = connection.listRecords( ServiceMail::getMailTable(), MailboxStoreLogicInternal::makeAccountPrefix( accountID ), "",
                                                                  MailboxProtocol::kMaxListCount, true, listRecord );
        if ( listed != ServiceStoreResult::Ok )
            return MailboxServiceResult::fail( OnlineError::kUnavailable );
        MailboxServiceResult firstFailure = MailboxResult::Ok;
        for ( const ServiceRecord& record : listRecord )
        {
            if ( outReply._claimedCount >= kMaxClaimAllCount )
                break;
            ServiceMailRecord mail;
            const bool        bClaimable = ServiceMail::decodeRecord( record._bytes, mail ) && mail._state != ServiceMailState::Claimed && mail._message._listAttachment.empty() == false &&
                                    MailboxStoreLogicInternal::isExpired( mail._message, nowMs ) == false;
            if ( bClaimable == false )
                continue;
            MailboxClaimInput input;
            input._mailKey                           = record._key;
            input._accountID                         = accountID;
            input._nowMs                             = nowMs;
            input._pPolicy                           = pPolicy;
            input._claimedRetentionMs                = claimedRetentionMs;
            const MailboxServiceResult result        = claim( connection, input, outReply );
            const bool                 bFirstFailure = result != MailboxResult::Ok && result != MailboxResult::AlreadyClaimed && firstFailure == MailboxResult::Ok;
            if ( bFirstFailure )
                firstFailure = result; // 하나가 상한에 걸려도 나머지는 받는다. 첫 실패를 알린다
        }
        const MailboxServiceResult serviceResult = outReply._claimedCount > 0 ? MailboxServiceResult{ MailboxResult::Ok } : firstFailure;
        outReply._result                         = serviceResult._result;
        return serviceResult;
    }

    MailboxServiceResult MailboxStoreLogic::deleteMail( IServiceStoreConnection& connection, uint64 accountID, string_view mailKey )
    {
        ServiceMailRecord          mail;
        const MailboxServiceResult read = MailboxStoreLogicInternal::readMail( connection, accountID, mailKey, mail );
        if ( read != MailboxResult::Ok )
            return read;
        if ( mail._state != ServiceMailState::Claimed && mail._message._listAttachment.empty() == false )
            return MailboxResult::HasAttachments;
        ServiceTransaction transaction;
        transaction.erase( ServiceMail::getMailTable(), mail._mailKey, mail._version );
        if ( mail._message._expiresMs > 0 ) // 받은 우편도 보관 기간 만료 인덱스가 있다
            transaction.erase( ServiceMail::getExpiryTable(), ServiceMail::makeExpiryKey( mail._message._expiresMs, mailKey ) );
        const ServiceStoreResult committed = connection.commit( transaction );
        if ( committed == ServiceStoreResult::Conflict )
            return MailboxServiceResult::fail( OnlineError::kBusy );
        return committed == ServiceStoreResult::Ok ? MailboxServiceResult{ MailboxResult::Ok } : MailboxServiceResult::fail( OnlineError::kUnavailable );
    }

    ServiceStoreResult MailboxStoreLogic::eraseAccount( IServiceStoreConnection& connection, AccountID accountID, int64 nowMs )
    {
        using Internal      = MailboxStoreLogicInternal;
        const string prefix = ServiceKeyUtil::makeHex64( accountID ) + "/";
        while ( true )
        {
            vector<ServiceRecord>    listMail;
            const ServiceStoreResult listed = connection.listRecords( ServiceMail::getMailTable(), prefix, "", Internal::kErasePageSize, false, listMail );
            if ( listed != ServiceStoreResult::Ok )
                return listed;
            if ( listMail.empty() )
                break;
            for ( const ServiceRecord& record : listMail )
            {
                ServiceTransaction transaction;
                if ( Internal::stageEraseMail( connection, record, nowMs, transaction ) == false )
                    return ServiceStoreResult::Conflict; // 맡김 이동을 짜지 못했다 — 다음 회차가 다시 읽는다
                const ServiceStoreResult committed = connection.commit( transaction );
                if ( committed != ServiceStoreResult::Ok )
                    return committed;
            }
        }
        if ( const ServiceStoreResult sent = ServiceStoreUtil::erasePrefix( connection, ServiceMail::getSentTable(), prefix ); sent != ServiceStoreResult::Ok )
            return sent;
        vector<ServiceMailCampaign> listCampaign;
        if ( const ServiceStoreResult read = ServiceMailCampaign::listCampaigns( connection, listCampaign ); read != ServiceStoreResult::Ok )
            return read;
        ServiceTransaction claims;
        for ( const ServiceMailCampaign& campaign : listCampaign )
        {
            claims.erase( ServiceMailCampaign::getClaimTable(), ServiceMailCampaign::makeClaimKey( campaign._campaignID, accountID ) );
            if ( claims.getWrites().size() < static_cast<size_t>( ServiceTransaction::kMaxWriteCount ) )
                continue;
            if ( const ServiceStoreResult committed = connection.commit( claims ); committed != ServiceStoreResult::Ok )
                return committed;
            claims = ServiceTransaction{};
        }
        return claims.getWrites().empty() ? ServiceStoreResult::Ok : connection.commit( claims );
    }

    void MailboxStoreLogic::sweepExpired( IServiceStoreConnection& connection, int64 nowMs, int32 maxCount, MailboxSweepStats& outStats )
    {
        outStats = MailboxSweepStats{};
        vector<ServiceRecord> listIndex;
        if ( connection.listRecords( ServiceMail::getExpiryTable(), "", "", maxCount, false, listIndex ) != ServiceStoreResult::Ok )
        {
            ++outStats._failedCount;
            return;
        }
        const size_t width = static_cast<size_t>( ServiceKeyUtil::kHexWidth );
        for ( const ServiceRecord& index : listIndex )
        {
            uint64 expiresMs = 0;
            if ( index._key.size() <= width + 1 || ServiceKeyUtil::parseHex64( string_view( index._key ).substr( 0, width ), expiresMs ) == false )
            {
                ++outStats._failedCount;
                continue;
            }
            if ( static_cast<int64>( expiresMs ) > nowMs )
                return; // 인덱스가 시각 순서이므로 나머지는 아직 정리할 때가 아니다
            const string_view            mailKey = string_view( index._key ).substr( width + 1 );
            ServiceTransaction           transaction;
            bool                         bReturned  = false;
            bool                         bDiscarded = false;
            vector<MailboxBalanceChange> listChange;
            if ( MailboxStoreLogicInternal::stageExpiry( connection, index, mailKey, nowMs, transaction, bReturned, bDiscarded, listChange ) == false ||
                 connection.commit( transaction ) != ServiceStoreResult::Ok )
            {
                ++outStats._failedCount;
                continue;
            }
            for ( MailboxBalanceChange& change : listChange )
            {
                outStats._listChange.push_back( std::move( change ) );
            }
            if ( bReturned )
                ++outStats._returnedCount;
            else if ( bDiscarded )
                ++outStats._discardedCount;
            else
                ++outStats._removedCount;
        }
        outStats._bMore = static_cast<int32>( listIndex.size() ) == maxCount ? SW_TRUE : SW_FALSE;
    }
} // namespace sw
