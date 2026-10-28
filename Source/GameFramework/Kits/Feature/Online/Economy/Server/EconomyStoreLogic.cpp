#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyStoreLogic.h"

#include "Core/Serialization/BitStream.h"

#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Kits/Feature/Online/Economy/Shared/Catalog/CurrencyCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Shared/Catalog/OfferCatalog.h"

namespace sw
{
    namespace
    {
        struct EconomyStoreLogicInternal
        {
            static constexpr const utf8* kReceiptScopePrefix  = "rcpt.";
            static constexpr const utf8* kRefundScopePrefix   = "refund.";
            static constexpr const utf8* kWithdrawScopePrefix = "withdraw.";
            static constexpr uint8       kMarkRefunded        = 1;
            static constexpr uint8       kMarkWithdrawn       = 2;

            /** @brief 거래의 지급 분개 키입니다. 환불 표시의 키이기도 하다. */
            [[nodiscard]] static bool makeRedeemKey( string_view storeName, string_view transactionID, string& outKey )
            {
                return LedgerJournalKey::makeFromToken( string( kReceiptScopePrefix ) + string( storeName ), transactionID, outKey );
            }

            static EconomyResult finish( EconomyReply& outReply, EconomyResult result )
            {
                outReply._result = result;
                return result;
            }

            /** @brief 구매 분개를 찾는 요청입니다. 키, 사유, 상품만 담습니다. */
            static LedgerTransferRequest makePurchaseProbe( const EconomyPurchaseInput& input, string_view offerID )
            {
                LedgerTransferRequest probe;
                probe._journalKey = input._journalKey;
                probe._reason     = "shop.buy";
                probe._memo       = string( offerID );
                return probe;
            }

            static string makePurchaseCountKey( uint64 accountID, string_view offerID )
            {
                string key = ServiceKeyUtil::makeHex64( accountID );
                key.push_back( '/' );
                key += offerID;
                return key;
            }

            /** @brief 가격 항목을 붙입니다. 가상 화폐이면 재원 잔액을 순서대로 씁니다. 모자라거나 재원에 빚이 있으면 InsufficientFunds 입니다. */
            static EconomyResult appendPriceLegs( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const OfferPrice& price, int64 totalAmount,
                                                  const LedgerHolder& account, LedgerTransferRequest& inoutRequest )
            {
                const CurrencyDef* pCurrency = currencies.findCurrency( price._currencyID );
                if ( pCurrency == nullptr || pCurrency->isVirtual() == false )
                {
                    inoutRequest._listPosting.push_back( LedgerPosting{ account, LedgerHolder::makeSink(), price._currencyID, totalAmount } );
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
                        return EconomyResult::InsufficientFunds; // 빚이 있는 동안은 그 재원의 가상 화폐를 쓸 수 없다
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
             * @brief 같은 분개 키의 분개가 이미 있으면 그 결과를 @p outReply 에 넣고 true 를 반환합니다. `_result` 는 Ok 이며 다른 구매의 키이면 InvalidRequest 입니다.
             * @details 지금 잔액으로 분개 항목을 다시 짤 수 없다. 재원이 비었을 수 있기 때문이다. 그래서 저장된 분개의 항목으로 재생한다.
             */
            [[nodiscard]] static bool findReplay( IServiceStoreConnection& connection, const LedgerTransferRequest& request, uint64 accountID, EconomyReply& outReply )
            {
                LedgerJournalEntry entry;
                if ( Ledger::findJournal( connection, request._journalKey, entry ) != ServiceStoreResult::Ok )
                    return false;
                if ( entry._reason != request._reason || entry._memo != request._memo )
                {
                    outReply._result = EconomyResult::InvalidRequest; // 다른 구매에 쓴 멱등 키이다
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
                EconomyStoreLogic::collectAccountBalances( replay, accountID, outReply._listBalance );
                outReply._bReplayed = SW_TRUE;
                outReply._result    = EconomyResult::Ok;
                return true;
            }

            /**
             * @brief 가격 항목과 지급 항목으로 구매 분개 요청을 짭니다.
             * @details 구매를 여기서 끝내야 하면 그 결과를 돌려주고, 이어 가도 되면 빈 값입니다. 가격이 모자라면 같은 키로 이미 산 것인지 먼저 확인합니다.
             *          첫 구매로 잔액이 줄어든 뒤의 재시도가 "부족" 을 받으면 안 되기 때문입니다.
             */
            [[nodiscard]] static std::optional<EconomyResult> buildPurchaseRequest( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const OfferDef& offer,
                                                                                    const EconomyPurchaseInput& input, EconomyReply& outReply, LedgerTransferRequest& outRequest )
            {
                const LedgerHolder account = LedgerHolder::makeAccount( input._accountID );
                const int64        count   = input._count;
                outRequest._journalKey     = input._journalKey;
                outRequest._reason         = "shop.buy";
                outRequest._memo           = offer._id;
                outRequest._pPolicy        = &currencies;
                outRequest._timeMs         = input._nowMs;
                outRequest._actorID        = input._accountID;
                outRequest._actorKind      = LedgerActorKind::Player;
                for ( const OfferPrice& price : offer._listPrice )
                {
                    if ( price._amount > LedgerConstant::kMaxAmount / count )
                        return EconomyResult::InvalidRequest;
                    const EconomyResult priced = appendPriceLegs( connection, currencies, price, price._amount * count, account, outRequest );
                    if ( priced == EconomyResult::Ok )
                        continue;
                    if ( findReplay( connection, outRequest, input._accountID, outReply ) )
                        return outReply._result;
                    return priced;
                }
                for ( const OfferGrant& grant : offer._listGrant )
                {
                    if ( grant._amount > LedgerConstant::kMaxAmount / count )
                        return EconomyResult::InvalidRequest;
                    outRequest._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), account, grant._assetID, grant._amount * count } );
                }
                return std::nullopt;
            }

            /**
             * @brief 계정별 구매 한도가 있는 상품이면 구매 수를 늘려 @p inoutTransaction 에 붙입니다.
             * @details 구매를 여기서 끝내야 하면 그 결과(저장소 오류, 한도 초과)를 돌려주고, 이어 가도 되면 빈 값입니다.
             */
            [[nodiscard]] static std::optional<EconomyResult> stageAccountLimit( IServiceStoreConnection& connection, const OfferDef& offer, const EconomyPurchaseInput& input,
                                                                                 ServiceTransaction& inoutTransaction )
            {
                if ( offer._limitPerAccount <= 0 )
                    return std::nullopt;
                const int64              count    = input._count;
                const string             countKey = makePurchaseCountKey( input._accountID, offer._id );
                ServiceRecord            countRecord;
                const ServiceStoreResult read = connection.readRecord( EconomyStoreLogic::getPurchaseCountTable(), countKey, countRecord );
                if ( read != ServiceStoreResult::Ok && read != ServiceStoreResult::NotFound )
                    return EconomyResult::Unavailable;
                int64 bought = 0;
                if ( read == ServiceStoreResult::Ok )
                {
                    BitReader reader( countRecord._bytes.data(), static_cast<int32>( countRecord._bytes.size() ) );
                    bought = reader.readVarInt();
                }
                if ( bought + count > offer._limitPerAccount )
                    return EconomyResult::LimitReached;
                BitWriter writer;
                writer.writeVarInt( bought + count );
                inoutTransaction.put( EconomyStoreLogic::getPurchaseCountTable(), countKey, writer.getBytes(), countRecord._version );
                return std::nullopt;
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

    void EconomyStoreLogic::collectAccountBalances( const LedgerTransferOutcome& outcome, uint64 accountID, vector<LedgerBalance>& outListBalance )
    {
        const LedgerHolder account = LedgerHolder::makeAccount( accountID );
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
        const OfferDef* pOffer = offers.findOffer( input._offerID );
        if ( pOffer == nullptr )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::UnknownOffer );
        if ( pOffer->isRealMoney() )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::NotPurchasable );
        const bool bCountOk         = 1 <= input._count && input._count <= pOffer->_maxCountPerPurchase;
        const bool bInvalidPurchase = bCountOk == false || input._accountID == 0 || LedgerUtil::isValidJournalKey( input._journalKey ) == false;
        if ( bInvalidPurchase )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::InvalidRequest );
        if ( pOffer->isOnSale( input._nowMs ) == false )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::NotOnSale );

        // 재시도는 잔액과 한도 판정보다 먼저 지난 결과를 확인한다. 가상 화폐 분개 항목은 지금 잔액으로 짜므로 다시 짜면 처음과 다를 수 있다.
        if ( EconomyStoreLogicInternal::findReplay( connection, EconomyStoreLogicInternal::makePurchaseProbe( input, pOffer->_id ), input._accountID, outReply ) )
            return EconomyStoreLogicInternal::finish( outReply, outReply._result );
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            LedgerTransferRequest request;
            if ( const std::optional<EconomyResult> early = EconomyStoreLogicInternal::buildPurchaseRequest( connection, currencies, *pOffer, input, outReply, request ) )
                return EconomyStoreLogicInternal::finish( outReply, *early );

            ServiceTransaction    transaction;
            LedgerTransferOutcome outcome;
            const LedgerResult    staged = Ledger::stageTransfer( connection, request, transaction, outcome );
            if ( staged != LedgerResult::Ok )
            {
                if ( staged == LedgerResult::InsufficientFunds && EconomyStoreLogicInternal::findReplay( connection, request, input._accountID, outReply ) )
                    return EconomyStoreLogicInternal::finish( outReply, outReply._result );
                return EconomyStoreLogicInternal::finish( outReply, toEconomyResult( staged ) );
            }
            if ( outcome._bReplayed == SW_TRUE )
            {
                collectAccountBalances( outcome, input._accountID, outReply._listBalance );
                outReply._bReplayed = SW_TRUE;
                return EconomyStoreLogicInternal::finish( outReply, EconomyResult::Ok );
            }
            if ( const std::optional<EconomyResult> early = EconomyStoreLogicInternal::stageAccountLimit( connection, *pOffer, input, transaction ) )
                return EconomyStoreLogicInternal::finish( outReply, *early );
            const ServiceStoreResult committed = connection.commit( transaction );
            if ( committed == ServiceStoreResult::Ok )
            {
                collectAccountBalances( outcome, input._accountID, outReply._listBalance );
                return EconomyStoreLogicInternal::finish( outReply, EconomyResult::Ok );
            }
            if ( committed == ServiceStoreResult::Invalid )
                return EconomyStoreLogicInternal::finish( outReply, EconomyResult::InvalidRequest );
            const LedgerResult resolved = Ledger::resolveConflict( connection, request, outcome );
            if ( resolved == LedgerResult::Ok )
            {
                collectAccountBalances( outcome, input._accountID, outReply._listBalance );
                outReply._bReplayed = outcome._bReplayed;
                return EconomyStoreLogicInternal::finish( outReply, EconomyResult::Ok );
            }
            if ( resolved != LedgerResult::Conflict )
                return EconomyStoreLogicInternal::finish( outReply, toEconomyResult( resolved ) );
            if ( committed == ServiceStoreResult::Unavailable )
                return EconomyStoreLogicInternal::finish( outReply, EconomyResult::Unavailable ); // 적용됐는지 알 수 없다. 같은 키로 다시 하면 중복이 가려진다
            // Conflict 이다. 잔액이나 구매 수가 그사이 바뀌었으므로 다시 읽어 다시 짠다
        }
        return EconomyStoreLogicInternal::finish( outReply, EconomyResult::Busy );
    }

    EconomyResult EconomyStoreLogic::redeemReceipt( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const OfferCatalog& offers,
                                                    const EconomyRedeemInput& input, EconomyReply& outReply )
    {
        outReply                               = EconomyReply{};
        const ReceiptValidationResult& receipt = input._receipt;
        outReply._productID                    = receipt._productID;
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
        if ( input._accountID == 0 )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::InvalidRequest );
        const OfferDef* pOffer = offers.findOfferByProduct( receipt._storeName, receipt._productID );
        if ( pOffer == nullptr )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::UnknownProduct );

        LedgerTransferRequest request;
        if ( EconomyStoreLogicInternal::makeRedeemKey( receipt._storeName, receipt._transactionID, request._journalKey ) == false )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::ReceiptInvalid );
        ServiceRecord            refundMark;
        const ServiceStoreResult markRead = connection.readRecord( getRefundTable(), request._journalKey, refundMark );
        if ( markRead == ServiceStoreResult::Ok )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::ReceiptRefunded ); // 지급 전에 환불 알림이 왔거나 이미 돌려받은 거래이다
        if ( markRead != ServiceStoreResult::NotFound )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::Unavailable );
        request._reason            = "iap.redeem";
        request._memo              = receipt._productID.substr( 0, static_cast<size_t>( LedgerConstant::kMaxMemoSize ) );
        request._pPolicy           = &currencies;
        request._timeMs            = input._nowMs;
        request._actorID           = input._accountID;
        request._actorKind         = LedgerActorKind::Player;
        const LedgerHolder account = LedgerHolder::makeAccount( input._accountID );
        for ( const OfferGrant& grant : pOffer->_listGrant )
        {
            request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), account, grant._assetID, grant._amount } );
        }
        LedgerTransferOutcome outcome;
        const LedgerResult    result = Ledger::executeTransfer( connection, request, outcome );
        if ( result == LedgerResult::JournalKeyReused )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::AlreadyRedeemed ); // 받는 계정이 달라서 분개 내용이 다르다
        if ( result != LedgerResult::Ok )
            return EconomyStoreLogicInternal::finish( outReply, toEconomyResult( result ) );
        collectAccountBalances( outcome, input._accountID, outReply._listBalance );
        outReply._bReplayed = outcome._bReplayed;
        return EconomyStoreLogicInternal::finish( outReply, EconomyResult::Ok );
    }

    const hashed_string& EconomyStoreLogic::getRefundTable()
    {
        static const hashed_string s_table{ "econ_refund" };
        return s_table;
    }

    EconomyResult EconomyStoreLogic::revokeRefund( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const ReceiptRefundNotice& notice, int64 nowMs,
                                                   vector<EconomyBalanceChange>& outListChange )
    {
        using Internal = EconomyStoreLogicInternal;
        outListChange.clear();
        string redeemKey;
        if ( Internal::makeRedeemKey( notice._storeName, notice._transactionID, redeemKey ) == false )
            return EconomyResult::InvalidRequest;
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            ServiceRecord            mark;
            const ServiceStoreResult markRead = connection.readRecord( getRefundTable(), redeemKey, mark );
            if ( markRead == ServiceStoreResult::Ok )
                return EconomyResult::AlreadyRefunded; // 이미 회수했거나 청약철회로 돌려받았다
            if ( markRead != ServiceStoreResult::NotFound )
                return EconomyResult::Unavailable;
            LedgerJournalEntry       redeem;
            const ServiceStoreResult found = Ledger::findJournal( connection, redeemKey, redeem );
            if ( found != ServiceStoreResult::Ok && found != ServiceStoreResult::NotFound )
                return EconomyResult::Unavailable;
            ServiceTransaction transaction;
            transaction.put( getRefundTable(), redeemKey, vector<uint8>{ Internal::kMarkRefunded }, ServiceRecord::kAbsentVersion );
            if ( found == ServiceStoreResult::NotFound )
            {
                // 지급 전에 환불이 왔다. 표시만 남겨 뒤에 온 지급을 막는다
                const ServiceStoreResult committed = connection.commit( transaction );
                if ( committed == ServiceStoreResult::Conflict )
                    continue;
                return committed == ServiceStoreResult::Ok ? EconomyResult::Ok : EconomyResult::Unavailable;
            }
            LedgerTransferRequest request;
            if ( LedgerJournalKey::makeFromToken( string( Internal::kRefundScopePrefix ) + notice._storeName, notice._transactionID, request._journalKey ) == false )
                return EconomyResult::InvalidRequest;
            request._reason     = "refund.revoke";
            request._memo       = redeem._memo;
            request._pPolicy    = &currencies;
            request._timeMs     = nowMs;
            request._actorKind  = LedgerActorKind::System;
            request._bAllowDebt = SW_TRUE; // 환불 회수만 빚을 허용한다
            for ( const LedgerPosting& posting : redeem._listPosting )
            {
                request._listPosting.push_back( LedgerPosting{ posting._to, LedgerHolder::makeSink(), posting._assetID, posting._amount } );
            }
            LedgerTransferOutcome outcome;
            const LedgerResult    staged = Ledger::stageTransfer( connection, request, transaction, outcome );
            if ( staged != LedgerResult::Ok )
                return toEconomyResult( staged );
            const ServiceStoreResult committed = connection.commit( transaction );
            if ( committed == ServiceStoreResult::Ok )
            {
                for ( const LedgerTransferOutcome::HolderBalance& holderBalance : outcome._listHolderBalance )
                {
                    if ( holderBalance._holder._kind == LedgerHolderKind::Account )
                        outListChange.push_back( EconomyBalanceChange{ holderBalance._holder._accountID, holderBalance._balance } );
                }
                return EconomyResult::Ok;
            }
            if ( committed != ServiceStoreResult::Conflict )
                return EconomyResult::Unavailable;
        }
        return EconomyResult::Busy;
    }

    EconomyResult EconomyStoreLogic::withdrawPurchase( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const EconomyWithdrawInput& input,
                                                       EconomyReply& outReply )
    {
        using Internal = EconomyStoreLogicInternal;
        outReply       = EconomyReply{};
        string redeemKey;
        if ( input._accountID == 0 || Internal::makeRedeemKey( input._storeName, input._transactionID, redeemKey ) == false )
            return Internal::finish( outReply, EconomyResult::InvalidRequest );
        const LedgerHolder account = LedgerHolder::makeAccount( input._accountID );
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            ServiceRecord            mark;
            const ServiceStoreResult markRead = connection.readRecord( getRefundTable(), redeemKey, mark );
            if ( markRead == ServiceStoreResult::Ok )
                return Internal::finish( outReply, EconomyResult::AlreadyRefunded );
            if ( markRead != ServiceStoreResult::NotFound )
                return Internal::finish( outReply, EconomyResult::Unavailable );
            LedgerJournalEntry       redeem;
            const ServiceStoreResult found = Ledger::findJournal( connection, redeemKey, redeem );
            if ( found == ServiceStoreResult::NotFound )
                return Internal::finish( outReply, EconomyResult::NotPurchaser );
            if ( found != ServiceStoreResult::Ok )
                return Internal::finish( outReply, EconomyResult::Unavailable );
            if ( input._nowMs - redeem._timeMs > input._windowMs )
                return Internal::finish( outReply, EconomyResult::WithdrawalExpired );
            LedgerTransferRequest request;
            if ( LedgerJournalKey::makeFromToken( string( Internal::kWithdrawScopePrefix ) + input._storeName, input._transactionID, request._journalKey ) == false )
                return Internal::finish( outReply, EconomyResult::InvalidRequest );
            request._reason    = "iap.withdraw";
            request._memo      = redeem._memo;
            request._pPolicy   = &currencies;
            request._timeMs    = input._nowMs;
            request._actorID   = input._accountID;
            request._actorKind = LedgerActorKind::Player;
            outReply._listWithdrawn.clear();
            for ( const LedgerPosting& posting : redeem._listPosting )
            {
                if ( posting._to != account )
                    return Internal::finish( outReply, EconomyResult::NotPurchaser );
                LedgerBalance balance;
                if ( Ledger::readBalance( connection, account, posting._assetID, balance ) != ServiceStoreResult::Ok )
                    return Internal::finish( outReply, EconomyResult::Unavailable );
                const int64 unused = balance._amount < posting._amount ? balance._amount : posting._amount; // 쓰지 않은 만큼만 돌려받는다
                if ( unused <= 0 )
                    continue;
                request._listPosting.push_back( LedgerPosting{ account, LedgerHolder::makeSink(), posting._assetID, unused } );
                outReply._listWithdrawn.push_back( LedgerBalance{ posting._assetID, unused } );
            }
            if ( request._listPosting.empty() )
                return Internal::finish( outReply, EconomyResult::NothingToWithdraw );
            ServiceTransaction transaction;
            transaction.put( getRefundTable(), redeemKey, vector<uint8>{ Internal::kMarkWithdrawn }, ServiceRecord::kAbsentVersion );
            LedgerTransferOutcome outcome;
            const LedgerResult    staged = Ledger::stageTransfer( connection, request, transaction, outcome );
            if ( staged != LedgerResult::Ok )
                return Internal::finish( outReply, toEconomyResult( staged ) );
            const ServiceStoreResult committed = connection.commit( transaction );
            if ( committed == ServiceStoreResult::Ok )
            {
                collectAccountBalances( outcome, input._accountID, outReply._listBalance );
                return Internal::finish( outReply, EconomyResult::Ok );
            }
            if ( committed != ServiceStoreResult::Conflict )
                return Internal::finish( outReply, EconomyResult::Unavailable );
        }
        return Internal::finish( outReply, EconomyResult::Busy );
    }

    EconomyResult EconomyStoreLogic::readWallet( IServiceStoreConnection& connection, uint64 accountID, EconomyReply& outReply )
    {
        outReply                      = EconomyReply{};
        const ServiceStoreResult read = Ledger::listBalances( connection, LedgerHolder::makeAccount( accountID ), outReply._listBalance );
        return EconomyStoreLogicInternal::finish( outReply, read == ServiceStoreResult::Ok ? EconomyResult::Ok : EconomyResult::Unavailable );
    }

    EconomyResult EconomyStoreLogic::readHistory( IServiceStoreConnection& connection, uint64 accountID, const EconomyHistoryRequest& request, EconomyReply& outReply )
    {
        outReply                           = EconomyReply{};
        const LedgerHolder         account = LedgerHolder::makeAccount( accountID );
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
                    if ( change._assetID == posting._assetID )
                        pChange = &change;
                }
                if ( pChange == nullptr )
                {
                    pChange           = &historyEntry._listChange.emplace_back();
                    pChange->_assetID = posting._assetID;
                }
                pChange->_amount += sign * posting._amount;
            }
        }
        return EconomyStoreLogicInternal::finish( outReply, EconomyResult::Ok );
    }
} // namespace sw
