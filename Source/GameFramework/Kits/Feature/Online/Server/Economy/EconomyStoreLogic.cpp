#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Server/Economy/EconomyStoreLogic.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Kits/Feature/Online/Economy/Catalog/CurrencyCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Catalog/OfferCatalog.h"

namespace sw
{
    namespace
    {
        struct EconomyStoreLogicInternal
        {
            static constexpr const utf8* kReceiptScopePrefix = "rcpt.";

            static EconomyResult finish( EconomyReply& outReply, EconomyResult result )
            {
                outReply._result = result;
                return result;
            }

            /** @brief 구매 분개를 찾는 요청(키 · 사유 · 상품만)입니다. */
            static LedgerTransferRequest makePurchaseProbe( const EconomyPurchaseInput& input, string_view offerId )
            {
                LedgerTransferRequest probe;
                probe._journalKey = input._journalKey;
                probe._reason     = "shop.buy";
                probe._memo       = string( offerId );
                return probe;
            }

            static string makePurchaseCountKey( uint64 accountId, string_view offerId )
            {
                string key = ServiceKeyUtil::makeHex64( accountId );
                key.push_back( '/' );
                key += offerId;
                return key;
            }

            /** @brief 가격 다리를 붙입니다. 가상 화폐면 재원 잔액을 순서대로 쓴다 — 모자라거나 재원에 빚이 있으면 InsufficientFunds. */
            static EconomyResult appendPriceLegs( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const OfferPrice& price, int64 totalAmount,
                                                  const LedgerHolder& account, LedgerTransferRequest& inoutRequest )
            {
                const CurrencyDef* pCurrency = currencies.findCurrency( price._currencyId );
                if ( pCurrency == nullptr || pCurrency->isVirtual() == false )
                {
                    inoutRequest._listPosting.push_back( LedgerPosting{ account, LedgerHolder::makeSink(), price._currencyId, totalAmount } );
                    return EconomyResult::Ok;
                }
                int64 remaining = totalAmount;
                for ( const string& fundingAsset : pCurrency->_listFundingAsset )
                {
                    LedgerBalance            balance;
                    const ServiceStoreResult read = Ledger::readBalance( connection, account, fundingAsset, balance );
                    if ( read != ServiceStoreResult::Ok )
                        return EconomyResult::Unavailable;
                    if ( balance.isDebt() )
                        return EconomyResult::InsufficientFunds; // 빚이 있는 동안 그 재원의 가상 화폐는 쓰지 못한다
                    const int64 take = balance._amount < remaining ? balance._amount : remaining;
                    if ( take > 0 )
                    {
                        inoutRequest._listPosting.push_back( LedgerPosting{ account, LedgerHolder::makeSink(), fundingAsset, take } );
                        remaining -= take;
                    }
                }
                return remaining == 0 ? EconomyResult::Ok : EconomyResult::InsufficientFunds;
            }

            /**
             * @brief 같은 분개 키의 분개가 이미 있으면 그 결과를 @p outReply 에 넣고 true 입니다(`_result` 는 Ok, 다른 구매의 키면 InvalidRequest).
             * @details 다리는 지금 잔액으로 다시 짤 수 없으므로(재원이 비었을 수 있다) 저장된 분개의 다리로 재생한다.
             */
            static bool findReplay( IServiceStoreConnection& connection, const LedgerTransferRequest& request, uint64 accountId, EconomyReply& outReply )
            {
                LedgerJournalEntry entry;
                if ( Ledger::findJournal( connection, request._journalKey, entry ) != ServiceStoreResult::Ok )
                    return false;
                if ( entry._reason != request._reason || entry._memo != request._memo )
                {
                    outReply._result = EconomyResult::InvalidRequest; // 다른 구매에 쓴 멱등 키
                    return true;
                }
                LedgerTransferRequest stored = request;
                stored._listPosting          = entry._listPosting;
                LedgerTransferOutcome replay;
                if ( Ledger::resolveConflict( connection, stored, replay ) != LedgerResult::Ok )
                {
                    outReply._result = EconomyResult::InvalidRequest;
                    return true;
                }
                EconomyStoreLogic::collectAccountBalances( replay, accountId, outReply._listBalance );
                outReply._bReplayed = SW_TRUE;
                outReply._result    = EconomyResult::Ok;
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const hashed_string& EconomyStoreLogic::getPurchaseCountTable()
    {
        static const hashed_string s_table{ "econ_purchase" };
        return s_table;
    }

    EconomyResult EconomyStoreLogic::toEconomyResult( LedgerResult result )
    {
        switch ( result )
        {
            case LedgerResult::Ok:
                return EconomyResult::Ok;
            case LedgerResult::InsufficientFunds:
                return EconomyResult::InsufficientFunds;
            case LedgerResult::CapExceeded:
                return EconomyResult::CapExceeded;
            case LedgerResult::JournalKeyReused:
                return EconomyResult::InvalidRequest;
            case LedgerResult::Conflict:
                return EconomyResult::Busy;
            case LedgerResult::Invalid:
                return EconomyResult::InvalidRequest;
            case LedgerResult::Unavailable:
                return EconomyResult::Unavailable;
        }
        return EconomyResult::Unavailable;
    }

    void EconomyStoreLogic::collectAccountBalances( const LedgerTransferOutcome& outcome, uint64 accountId, vector<LedgerBalance>& outListBalance )
    {
        const LedgerHolder account = LedgerHolder::makeAccount( accountId );
        for ( const LedgerTransferOutcome::HolderBalance& holderBalance : outcome._listHolderBalance )
        {
            if ( holderBalance._holder == account )
                outListBalance.push_back( holderBalance._balance );
        }
    }

    EconomyResult EconomyStoreLogic::purchase( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const OfferCatalog& offers,
                                               const EconomyPurchaseInput& input, EconomyReply& outReply )
    {
        outReply               = EconomyReply{};
        const OfferDef* pOffer = offers.findOffer( input._offerId );
        if ( pOffer == nullptr )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::UnknownOffer );
        if ( pOffer->isRealMoney() )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::NotPurchasable );
        const bool bCountOk = 1 <= input._count && input._count <= pOffer->_maxCountPerPurchase;
        if ( bCountOk == false || input._accountId == 0 || LedgerUtil::isValidJournalKey( input._journalKey ) == false )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::InvalidRequest );
        if ( pOffer->isOnSale( input._nowMs ) == false )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::NotOnSale );

        const LedgerHolder account = LedgerHolder::makeAccount( input._accountId );
        const int64        count   = input._count;
        // 재시도는 잔액 · 한도 판정보다 먼저 지난 결과를 본다 — 가상 화폐 다리는 지금 잔액으로 짜므로 다시 짜면 처음과 다를 수 있다.
        if ( EconomyStoreLogicInternal::findReplay( connection, EconomyStoreLogicInternal::makePurchaseProbe( input, pOffer->_id ), input._accountId, outReply ) )
            return EconomyStoreLogicInternal::finish( outReply, outReply._result );
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            LedgerTransferRequest request;
            request._journalKey = input._journalKey;
            request._reason     = "shop.buy";
            request._memo       = pOffer->_id;
            request._pPolicy    = &currencies;
            request._timeMs     = input._nowMs;
            request._actorId    = input._accountId;
            request._actorKind  = LedgerActorKind::Player;
            for ( const OfferPrice& price : pOffer->_listPrice )
            {
                if ( price._amount > LedgerConstant::kMaxAmount / count )
                    return EconomyStoreLogicInternal::finish( outReply, EconomyResult::InvalidRequest );
                const EconomyResult priced = EconomyStoreLogicInternal::appendPriceLegs( connection, currencies, price, price._amount * count, account, request );
                if ( priced == EconomyResult::Ok )
                    continue;
                // 모자람 판정 전에 같은 키로 이미 산 것인지 본다 — 첫 구매로 잔액이 준 뒤의 재시도가 "모자람" 을 받으면 안 된다.
                if ( EconomyStoreLogicInternal::findReplay( connection, request, input._accountId, outReply ) )
                    return EconomyStoreLogicInternal::finish( outReply, outReply._result );
                return EconomyStoreLogicInternal::finish( outReply, priced );
            }
            for ( const OfferGrant& grant : pOffer->_listGrant )
            {
                if ( grant._amount > LedgerConstant::kMaxAmount / count )
                    return EconomyStoreLogicInternal::finish( outReply, EconomyResult::InvalidRequest );
                request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), account, grant._assetId, grant._amount * count } );
            }

            ServiceTransaction    transaction;
            LedgerTransferOutcome outcome;
            const LedgerResult    staged = Ledger::stageTransfer( connection, request, transaction, outcome );
            if ( staged != LedgerResult::Ok )
            {
                if ( staged == LedgerResult::InsufficientFunds && EconomyStoreLogicInternal::findReplay( connection, request, input._accountId, outReply ) )
                    return EconomyStoreLogicInternal::finish( outReply, outReply._result );
                return EconomyStoreLogicInternal::finish( outReply, toEconomyResult( staged ) );
            }
            if ( outcome._bReplayed == SW_TRUE )
            {
                collectAccountBalances( outcome, input._accountId, outReply._listBalance );
                outReply._bReplayed = SW_TRUE;
                return EconomyStoreLogicInternal::finish( outReply, EconomyResult::Ok );
            }
            if ( pOffer->_limitPerAccount > 0 )
            {
                const string             countKey = EconomyStoreLogicInternal::makePurchaseCountKey( input._accountId, pOffer->_id );
                ServiceRecord            countRecord;
                const ServiceStoreResult read = connection.readRecord( getPurchaseCountTable(), countKey, countRecord );
                if ( read != ServiceStoreResult::Ok && read != ServiceStoreResult::NotFound )
                    return EconomyStoreLogicInternal::finish( outReply, EconomyResult::Unavailable );
                int64 bought = 0;
                if ( read == ServiceStoreResult::Ok )
                {
                    BitReader reader( countRecord._bytes.data(), static_cast<int32>( countRecord._bytes.size() ) );
                    bought = reader.readVarInt();
                }
                if ( bought + count > pOffer->_limitPerAccount )
                    return EconomyStoreLogicInternal::finish( outReply, EconomyResult::LimitReached );
                BitWriter writer;
                writer.writeVarInt( bought + count );
                transaction.put( getPurchaseCountTable(), countKey, writer.getBytes(), countRecord._version );
            }
            const ServiceStoreResult committed = connection.commit( transaction );
            if ( committed == ServiceStoreResult::Ok )
            {
                collectAccountBalances( outcome, input._accountId, outReply._listBalance );
                return EconomyStoreLogicInternal::finish( outReply, EconomyResult::Ok );
            }
            if ( committed == ServiceStoreResult::Invalid )
                return EconomyStoreLogicInternal::finish( outReply, EconomyResult::InvalidRequest );
            const LedgerResult resolved = Ledger::resolveConflict( connection, request, outcome );
            if ( resolved == LedgerResult::Ok )
            {
                collectAccountBalances( outcome, input._accountId, outReply._listBalance );
                outReply._bReplayed = outcome._bReplayed;
                return EconomyStoreLogicInternal::finish( outReply, EconomyResult::Ok );
            }
            if ( resolved != LedgerResult::Conflict )
                return EconomyStoreLogicInternal::finish( outReply, toEconomyResult( resolved ) );
            if ( committed == ServiceStoreResult::Unavailable )
                return EconomyStoreLogicInternal::finish( outReply, EconomyResult::Unavailable ); // 적용됐는지 모른다 — 같은 키로 다시 하면 가려진다
            // Conflict — 잔액 · 구매 수가 그새 바뀌었다: 다시 읽어 다시 짠다
        }
        return EconomyStoreLogicInternal::finish( outReply, EconomyResult::Busy );
    }

    EconomyResult EconomyStoreLogic::redeemReceipt( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const OfferCatalog& offers,
                                                    const EconomyRedeemInput& input, EconomyReply& outReply )
    {
        outReply                               = EconomyReply{};
        const ReceiptValidationResult& receipt = input._receipt;
        outReply._productId                    = receipt._productId;
        switch ( receipt._status )
        {
            case ReceiptStatus::Valid:
                break;
            case ReceiptStatus::Invalid:
                return EconomyStoreLogicInternal::finish( outReply, EconomyResult::ReceiptInvalid );
            case ReceiptStatus::Refunded:
                return EconomyStoreLogicInternal::finish( outReply, EconomyResult::ReceiptRefunded );
            case ReceiptStatus::Retry:
                return EconomyStoreLogicInternal::finish( outReply, EconomyResult::ReceiptPending );
            case ReceiptStatus::Count:
                return EconomyStoreLogicInternal::finish( outReply, EconomyResult::ReceiptInvalid );
        }
        if ( receipt._bSandbox == SW_TRUE && input._bAcceptSandbox == SW_FALSE )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::ReceiptInvalid );
        if ( input._accountId == 0 )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::InvalidRequest );
        const OfferDef* pOffer = offers.findOfferByProduct( receipt._storeName, receipt._productId );
        if ( pOffer == nullptr )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::UnknownProduct );

        LedgerTransferRequest request;
        const string          scope = string( EconomyStoreLogicInternal::kReceiptScopePrefix ) + receipt._storeName;
        if ( LedgerJournalKey::makeFromToken( scope, receipt._transactionId, request._journalKey ) == false )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::ReceiptInvalid );
        request._reason            = "iap.redeem";
        request._memo              = receipt._productId.substr( 0, static_cast<size_t>( LedgerConstant::kMaxMemoSize ) );
        request._pPolicy           = &currencies;
        request._timeMs            = input._nowMs;
        request._actorId           = input._accountId;
        request._actorKind         = LedgerActorKind::Player;
        const LedgerHolder account = LedgerHolder::makeAccount( input._accountId );
        for ( const OfferGrant& grant : pOffer->_listGrant )
        {
            request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), account, grant._assetId, grant._amount } );
        }
        LedgerTransferOutcome outcome;
        const LedgerResult    result = Ledger::executeTransfer( connection, request, outcome );
        if ( result == LedgerResult::JournalKeyReused )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::AlreadyRedeemed ); // 받는 계정이 달라 분개 내용이 다르다
        if ( result != LedgerResult::Ok )
            return EconomyStoreLogicInternal::finish( outReply, toEconomyResult( result ) );
        collectAccountBalances( outcome, input._accountId, outReply._listBalance );
        outReply._bReplayed = outcome._bReplayed;
        return EconomyStoreLogicInternal::finish( outReply, EconomyResult::Ok );
    }

    EconomyResult EconomyStoreLogic::readWallet( IServiceStoreConnection& connection, uint64 accountId, EconomyReply& outReply )
    {
        outReply                      = EconomyReply{};
        const ServiceStoreResult read = Ledger::listBalances( connection, LedgerHolder::makeAccount( accountId ), outReply._listBalance );
        return EconomyStoreLogicInternal::finish( outReply, read == ServiceStoreResult::Ok ? EconomyResult::Ok : EconomyResult::Unavailable );
    }

    EconomyResult EconomyStoreLogic::readHistory( IServiceStoreConnection& connection, uint64 accountId, const EconomyHistoryRequest& request, EconomyReply& outReply )
    {
        outReply                           = EconomyReply{};
        const LedgerHolder         account = LedgerHolder::makeAccount( accountId );
        vector<LedgerJournalEntry> listEntry;
        const ServiceStoreResult   read = Ledger::listHistory( connection, account, request._cursor, request._maxCount, listEntry, outReply._nextCursor );
        if ( read != ServiceStoreResult::Ok )
            return EconomyStoreLogicInternal::finish( outReply, read == ServiceStoreResult::Invalid ? EconomyResult::InvalidRequest : EconomyResult::Unavailable );
        for ( const LedgerJournalEntry& entry : listEntry )
        {
            EconomyHistoryEntry& historyEntry = outReply._listHistory.emplace_back();
            historyEntry._reason              = entry._reason;
            historyEntry._memo                = entry._memo;
            historyEntry._timeMs              = entry._timeMs;
            for ( const LedgerPosting& posting : entry._listPosting )
            {
                const int64 sign = posting._to == account ? 1 : ( posting._from == account ? -1 : 0 );
                if ( sign == 0 )
                    continue;
                LedgerBalance* pChange = nullptr;
                for ( LedgerBalance& change : historyEntry._listChange )
                {
                    if ( change._assetId == posting._assetId )
                        pChange = &change;
                }
                if ( pChange == nullptr )
                {
                    pChange           = &historyEntry._listChange.emplace_back();
                    pChange->_assetId = posting._assetId;
                }
                pChange->_amount += sign * posting._amount;
            }
        }
        return EconomyStoreLogicInternal::finish( outReply, EconomyResult::Ok );
    }
} // namespace sw
