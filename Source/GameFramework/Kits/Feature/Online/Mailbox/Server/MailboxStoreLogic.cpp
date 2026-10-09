#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Mailbox/Server/MailboxStoreLogic.h"

#include "Core/String/StringUtil.h"

#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Mail/ServiceMail.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    SW_LOG_CALLER( "MailboxStoreLogic" );

    namespace
    {
        struct MailboxStoreLogicInternal
        {

            static MailboxResult finish( MailboxReply& outReply, MailboxResult result )
            {
                outReply._result = result;
                return result;
            }

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

            static string makeAccountPrefix( uint64 accountId )
            {
                string prefix = ServiceKeyUtil::makeHex64( accountId );
                prefix.push_back( '/' );
                return prefix;
            }

            /** @brief 우편 하나를 읽어 해독합니다(받는 계정 확인 포함). */
            static MailboxResult readMail( IServiceStoreConnection& connection, uint64 accountId, string_view mailKey, ServiceMailRecord& outRecord )
            {
                uint64 recipient = 0;
                if ( ServiceMail::parseRecipient( mailKey, recipient ) == false || recipient != accountId )
                    return MailboxResult::NotFound;
                ServiceRecord            record;
                const ServiceStoreResult read = connection.readRecord( ServiceMail::getMailTable(), mailKey, record );
                if ( read == ServiceStoreResult::NotFound )
                    return MailboxResult::NotFound;
                if ( read != ServiceStoreResult::Ok || ServiceMail::decodeRecord( record._bytes, outRecord ) == false )
                    return MailboxResult::Unavailable;
                outRecord._mailKey = string( mailKey );
                outRecord._version = record._version;
                return MailboxResult::Ok;
            }

            static MailboxResult toMailboxResult( LedgerResult result )
            {
                switch ( result )
                {
                    case LedgerResult::Ok:
                        return MailboxResult::Ok;
                    case LedgerResult::CapExceeded:
                        return MailboxResult::CapExceeded;
                    case LedgerResult::Conflict:
                        return MailboxResult::Busy;
                    case LedgerResult::InsufficientFunds:
                    case LedgerResult::JournalKeyReused:
                    case LedgerResult::Invalid:
                    case LedgerResult::Unavailable:
                        return MailboxResult::Unavailable;
                }
                return MailboxResult::Unavailable;
            }

            static void collectAccountBalances( const LedgerTransferOutcome& outcome, uint64 accountId, vector<LedgerBalance>& inoutListBalance )
            {
                const LedgerHolder account = LedgerHolder::makeAccount( accountId );
                for ( const LedgerTransferOutcome::HolderBalance& holderBalance : outcome._listHolderBalance )
                {
                    if ( holderBalance._holder != account )
                        continue;
                    bool bReplaced = false;
                    for ( LedgerBalance& existing : inoutListBalance )
                    {
                        if ( existing._assetId == holderBalance._balance._assetId )
                        {
                            existing  = holderBalance._balance;
                            bReplaced = true;
                        }
                    }
                    if ( bReplaced == false )
                        inoutListBalance.push_back( holderBalance._balance );
                }
            }

            static MailboxResult claimCampaign( IServiceStoreConnection& connection, uint64 campaignId, const MailboxClaimInput& input, MailboxReply& outReply )
            {
                ServiceRecord            campaignRecord;
                const ServiceStoreResult read = connection.readRecord( ServiceMailCampaignTable::getCampaignTable(), ServiceKeyUtil::makeHex64( campaignId ), campaignRecord );
                if ( read == ServiceStoreResult::NotFound )
                    return MailboxResult::NotFound;
                ServiceMailCampaign campaign;
                if ( read != ServiceStoreResult::Ok || ServiceMailCampaignTable::decode( campaignRecord._bytes, campaign ) == false )
                    return MailboxResult::Unavailable;
                if ( campaign.isActive( input._nowMs ) == false )
                    return MailboxResult::Expired;
                const string          claimKey = ServiceMailCampaignTable::makeClaimKey( campaignId, input._accountId );
                LedgerTransferRequest request;
                if ( LedgerJournalKey::makeFromToken( "mail.campaign", claimKey, request._journalKey ) == false )
                    return MailboxResult::InvalidRequest;
                request._reason    = "mail.campaign";
                request._memo      = campaign._titleKey.substr( 0, static_cast<size_t>( LedgerConstant::kMaxMemoSize ) );
                request._pPolicy   = input._pPolicy;
                request._timeMs    = input._nowMs;
                request._actorId   = input._accountId;
                request._actorKind = LedgerActorKind::Player;
                for ( const ServiceMailAttachment& attachment : campaign._listAttachment )
                {
                    request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), LedgerHolder::makeAccount( input._accountId ), attachment._assetId, attachment._amount } );
                }
                for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
                {
                    ServiceTransaction    transaction;
                    LedgerTransferOutcome outcome;
                    const LedgerResult    staged = Ledger::stageTransfer( connection, request, transaction, outcome );
                    if ( staged != LedgerResult::Ok )
                        return toMailboxResult( staged );
                    if ( outcome._bReplayed == SW_TRUE )
                        return MailboxResult::AlreadyClaimed;
                    transaction.put( ServiceMailCampaignTable::getClaimTable(), claimKey, vector<uint8>{}, ServiceRecord::kAbsentVersion );
                    const ServiceStoreResult committed = connection.commit( transaction );
                    if ( committed == ServiceStoreResult::Ok )
                    {
                        collectAccountBalances( outcome, input._accountId, outReply._listBalance );
                        ++outReply._claimedCount;
                        return MailboxResult::Ok;
                    }
                    const LedgerResult resolved = Ledger::resolveConflict( connection, request, outcome );
                    if ( resolved == LedgerResult::Ok )
                        return MailboxResult::AlreadyClaimed;
                    if ( resolved != LedgerResult::Conflict || committed == ServiceStoreResult::Unavailable )
                        return MailboxResult::Unavailable;
                }
                return MailboxResult::Busy;
            }

            /** @brief 만료 색인 하나를 처리할 트랜잭션을 붙입니다. 실패면 false(그 색인은 다음 쓸기가 다시 본다). */
            static bool stageExpiry( IServiceStoreConnection& connection, const ServiceRecord& index, string_view mailKey, int64 nowMs, ServiceTransaction& inoutTransaction,
                                     bool& outbReturned, bool& outbDiscarded )
            {
                outbReturned  = false;
                outbDiscarded = false;
                inoutTransaction.erase( ServiceMail::getExpiryTable(), index._key, index._version );
                ServiceRecord            record;
                const ServiceStoreResult read = connection.readRecord( ServiceMail::getMailTable(), mailKey, record );
                if ( read != ServiceStoreResult::Ok && read != ServiceStoreResult::NotFound )
                    return false;
                ServiceMailRecord mail;
                const bool        bLive = read == ServiceStoreResult::Ok && ServiceMail::decodeRecord( record._bytes, mail ) && mail._state != ServiceMailState::Claimed;
                if ( bLive == false )
                    return true; // 없거나 받은 우편 — 색인만 지운다
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
                request._pPolicy   = nullptr; // 돌려주기는 상한을 보지 않는다 — 보낸 쪽이 가득 차도 재화가 사라지면 안 된다
                for ( const ServiceMailAttachment& attachment : mail._message._listAttachment )
                {
                    request._listPosting.push_back( LedgerPosting{ escrow, target, attachment._assetId, attachment._amount } );
                }
                LedgerTransferOutcome outcome;
                return Ledger::stageTransfer( connection, request, inoutTransaction, outcome ) == LedgerResult::Ok && outcome._bReplayed == SW_FALSE;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    MailboxResult MailboxStoreLogic::listMail( IServiceStoreConnection& connection, uint64 accountId, int64 nowMs, const vector<ServiceMailCampaign>& listActiveCampaign,
                                               const MailboxRequest& request, MailboxReply& outReply )
    {
        outReply = MailboxReply{};
        if ( request._cursor.empty() )
        {
            for ( const ServiceMailCampaign& campaign : listActiveCampaign )
            {
                if ( campaign.isActive( nowMs ) == false )
                    continue;
                ServiceRecord            claimRecord;
                const ServiceStoreResult claimed =
                    connection.readRecord( ServiceMailCampaignTable::getClaimTable(), ServiceMailCampaignTable::makeClaimKey( campaign._campaignId, accountId ), claimRecord );
                if ( claimed == ServiceStoreResult::Ok )
                    continue;
                if ( claimed != ServiceStoreResult::NotFound )
                    return MailboxStoreLogicInternal::finish( outReply, MailboxResult::Unavailable );
                MailView& view       = outReply._listMail.emplace_back();
                view._mailKey        = string( MailboxProtocol::kCampaignPrefix ) + ServiceKeyUtil::makeHex64( campaign._campaignId );
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
        const ServiceStoreResult listed = connection.listRecords( ServiceMail::getMailTable(), MailboxStoreLogicInternal::makeAccountPrefix( accountId ), request._cursor,
                                                                  request._maxCount, true, listRecord );
        if ( listed != ServiceStoreResult::Ok )
            return MailboxStoreLogicInternal::finish( outReply, MailboxResult::Unavailable );
        for ( const ServiceRecord& record : listRecord )
        {
            ServiceMailRecord mail;
            if ( ServiceMail::decodeRecord( record._bytes, mail ) == false )
            {
                SW_LOG_ERROR( "Mail '%#' is unreadable", record._key );
                continue;
            }
            if ( MailboxStoreLogicInternal::isExpired( mail._message, nowMs ) )
                continue; // 쓸기가 곧 치운다 — 목록에는 보이지 않는다
            mail._mailKey = record._key;
            outReply._listMail.push_back( MailboxStoreLogicInternal::makeView( mail ) );
        }
        if ( static_cast<int32>( listRecord.size() ) == request._maxCount )
            outReply._nextCursor = listRecord.back()._key;
        return MailboxStoreLogicInternal::finish( outReply, MailboxResult::Ok );
    }

    MailboxResult MailboxStoreLogic::markRead( IServiceStoreConnection& connection, uint64 accountId, string_view mailKey )
    {
        ServiceMailRecord   mail;
        const MailboxResult read = MailboxStoreLogicInternal::readMail( connection, accountId, mailKey, mail );
        if ( read != MailboxResult::Ok || mail._state != ServiceMailState::Unread )
            return read;
        mail._state = ServiceMailState::Read;
        ServiceTransaction transaction;
        transaction.put( ServiceMail::getMailTable(), mail._mailKey, ServiceMail::encodeRecord( mail ), mail._version );
        const ServiceStoreResult committed = connection.commit( transaction );
        if ( committed == ServiceStoreResult::Conflict )
            return MailboxResult::Ok; // 그새 받았거나 읽었다 — 읽음 표시는 덮지 않는다
        return committed == ServiceStoreResult::Ok ? MailboxResult::Ok : MailboxResult::Unavailable;
    }

    MailboxResult MailboxStoreLogic::claim( IServiceStoreConnection& connection, const MailboxClaimInput& input, MailboxReply& outReply )
    {
        const string_view mailKey = input._mailKey;
        if ( StringUtil::startsWith( mailKey, MailboxProtocol::kCampaignPrefix ) )
        {
            uint64 campaignId = 0;
            if ( ServiceKeyUtil::parseHex64( mailKey.substr( string_view( MailboxProtocol::kCampaignPrefix ).size() ), campaignId ) == false )
                return MailboxResult::NotFound;
            return MailboxStoreLogicInternal::claimCampaign( connection, campaignId, input, outReply );
        }
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            ServiceMailRecord   mail;
            const MailboxResult read = MailboxStoreLogicInternal::readMail( connection, input._accountId, mailKey, mail );
            if ( read != MailboxResult::Ok )
                return read;
            if ( mail._state == ServiceMailState::Claimed )
                return MailboxResult::AlreadyClaimed;
            if ( MailboxStoreLogicInternal::isExpired( mail._message, input._nowMs ) )
                return MailboxResult::Expired;

            ServiceTransaction    transaction;
            LedgerTransferRequest request;
            LedgerTransferOutcome outcome;
            const bool            bHasAttachment = mail._message._listAttachment.empty() == false;
            if ( bHasAttachment )
            {
                const LedgerHolder escrow  = ServiceMail::makeEscrowHolder( mailKey );
                const LedgerHolder funding = mail._message._fundingHolder._kind == LedgerHolderKind::Mint ? LedgerHolder::makeMint() : escrow;
                if ( LedgerJournalKey::makeFromToken( "mail.claim", escrow._escrowToken, request._journalKey ) == false )
                    return MailboxResult::InvalidRequest;
                request._reason    = "mail.claim";
                request._memo      = mail._message._titleKey.substr( 0, static_cast<size_t>( LedgerConstant::kMaxMemoSize ) );
                request._pPolicy   = input._pPolicy;
                request._timeMs    = input._nowMs;
                request._actorId   = input._accountId;
                request._actorKind = LedgerActorKind::Player;
                for ( const ServiceMailAttachment& attachment : mail._message._listAttachment )
                {
                    request._listPosting.push_back( LedgerPosting{ funding, LedgerHolder::makeAccount( input._accountId ), attachment._assetId, attachment._amount } );
                }
                const LedgerResult staged = Ledger::stageTransfer( connection, request, transaction, outcome );
                if ( staged != LedgerResult::Ok )
                {
                    if ( staged == LedgerResult::InsufficientFunds )
                        SW_LOG_ERROR( "Mail '%#' escrow holds less than its attachments", mailKey );
                    return MailboxStoreLogicInternal::toMailboxResult( staged );
                }
                if ( outcome._bReplayed == SW_TRUE )
                    return MailboxResult::AlreadyClaimed; // 분개와 상태는 한 트랜잭션이라 여기 오지 않는다 — 방어
            }
            mail._state = ServiceMailState::Claimed;
            transaction.put( ServiceMail::getMailTable(), mail._mailKey, ServiceMail::encodeRecord( mail ), mail._version );
            if ( mail._message._expiresMs > 0 )
                transaction.erase( ServiceMail::getExpiryTable(), ServiceMail::makeExpiryKey( mail._message._expiresMs, mailKey ) );
            const ServiceStoreResult committed = connection.commit( transaction );
            if ( committed == ServiceStoreResult::Ok )
            {
                MailboxStoreLogicInternal::collectAccountBalances( outcome, input._accountId, outReply._listBalance );
                ++outReply._claimedCount;
                return MailboxResult::Ok;
            }
            if ( committed == ServiceStoreResult::Invalid )
                return MailboxResult::InvalidRequest;
            if ( bHasAttachment )
            {
                const LedgerResult resolved = Ledger::resolveConflict( connection, request, outcome );
                if ( resolved == LedgerResult::Ok )
                {
                    // 응답만 잃었거나(이번 수령이 들어갔다) 다른 기기가 먼저 받았다 — 어느 쪽이든 원장은 한 번이다.
                    MailboxStoreLogicInternal::collectAccountBalances( outcome, input._accountId, outReply._listBalance );
                    return MailboxResult::Ok;
                }
                if ( resolved == LedgerResult::Unavailable || committed == ServiceStoreResult::Unavailable )
                    return MailboxResult::Unavailable;
            }
            else if ( committed == ServiceStoreResult::Unavailable )
            {
                return MailboxResult::Unavailable;
            }
            // Conflict — 그새 읽음 표시 등으로 판이 바뀌었다: 다시 읽어 다시
        }
        return MailboxResult::Busy;
    }

    MailboxResult MailboxStoreLogic::claimAll( IServiceStoreConnection& connection, uint64 accountId, int64 nowMs, const ILedgerPolicy* pPolicy, MailboxReply& outReply )
    {
        outReply = MailboxReply{};
        vector<ServiceRecord>    listRecord;
        const ServiceStoreResult listed = connection.listRecords( ServiceMail::getMailTable(), MailboxStoreLogicInternal::makeAccountPrefix( accountId ), "",
                                                                  MailboxProtocol::kMaxListCount, true, listRecord );
        if ( listed != ServiceStoreResult::Ok )
            return MailboxStoreLogicInternal::finish( outReply, MailboxResult::Unavailable );
        MailboxResult firstFailure = MailboxResult::Ok;
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
            input._mailKey             = record._key;
            input._accountId           = accountId;
            input._nowMs               = nowMs;
            input._pPolicy             = pPolicy;
            const MailboxResult result = claim( connection, input, outReply );
            if ( result != MailboxResult::Ok && result != MailboxResult::AlreadyClaimed && firstFailure == MailboxResult::Ok )
                firstFailure = result; // 하나가 상한에 걸려도 나머지는 받는다 — 첫 실패를 알린다
        }
        return MailboxStoreLogicInternal::finish( outReply, outReply._claimedCount > 0 ? MailboxResult::Ok : firstFailure );
    }

    MailboxResult MailboxStoreLogic::deleteMail( IServiceStoreConnection& connection, uint64 accountId, string_view mailKey )
    {
        ServiceMailRecord   mail;
        const MailboxResult read = MailboxStoreLogicInternal::readMail( connection, accountId, mailKey, mail );
        if ( read != MailboxResult::Ok )
            return read;
        if ( mail._state != ServiceMailState::Claimed && mail._message._listAttachment.empty() == false )
            return MailboxResult::HasAttachments;
        ServiceTransaction transaction;
        transaction.erase( ServiceMail::getMailTable(), mail._mailKey, mail._version );
        if ( mail._message._expiresMs > 0 && mail._state != ServiceMailState::Claimed )
            transaction.erase( ServiceMail::getExpiryTable(), ServiceMail::makeExpiryKey( mail._message._expiresMs, mailKey ) );
        const ServiceStoreResult committed = connection.commit( transaction );
        if ( committed == ServiceStoreResult::Conflict )
            return MailboxResult::Busy;
        return committed == ServiceStoreResult::Ok ? MailboxResult::Ok : MailboxResult::Unavailable;
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
                return; // 색인은 시각 순 — 나머지는 아직이다
            const string_view  mailKey = string_view( index._key ).substr( width + 1 );
            ServiceTransaction transaction;
            bool               bReturned  = false;
            bool               bDiscarded = false;
            if ( MailboxStoreLogicInternal::stageExpiry( connection, index, mailKey, nowMs, transaction, bReturned, bDiscarded ) == false ||
                 connection.commit( transaction ) != ServiceStoreResult::Ok )
            {
                ++outStats._failedCount;
                continue;
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
