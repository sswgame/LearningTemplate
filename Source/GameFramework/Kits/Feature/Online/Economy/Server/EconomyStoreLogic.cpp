#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyStoreLogic.h"

#include "Core/Serialization/BitStream.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Catalog/CurrencyCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Catalog/OfferCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Protocol/ReceiptBuyerToken.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPTypes.h"
#include "GameFramework/Runtime/Online/Ledger/Server/Ledger.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

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

            /**
             * @brief 스토어 답의 구매자를 지급받을 계정과 대조합니다. 맞으면 Ok, 아니면 NotPurchaser, 연동을 읽지 못하면 Unavailable 입니다.
             * @details 구매자 대조는 여기 한 곳이다. 스토어는 답이 말한 값을 옮겨 적고 규칙은 제공자 설정이 정한다. 답에 구매자가 없으면 거절한다 — 없음을 통과시키면 그 길로 남의 영수증을 받는다.
             *          계정 토큰이 없으면 `BuyerTokenMissing` 으로 까닭을 가른다 — 남의 영수증이 아니라 클라이언트가 토큰을 빠뜨린 구매일 때가 많다.
             */
            static EconomyServiceResult compareBuyer( IServiceStoreConnection& connection, const EconomyRedeemInput& input )
            {
                const ReceiptValidationResult& receipt = input._receipt;
                switch ( receipt._buyerRule._binding )
                {
                    case ReceiptBuyerBinding::None:
                    {
                        return EconomyResult::Ok;
                    }
                    case ReceiptBuyerBinding::AccountToken:
                    {
                        if ( receipt._storeBuyerID.empty() )
                        {
                            SW_LOG_WARNING_ID( "EconomyStoreLogic.BuyerTokenMissing", "The %# answer for transaction '%#' has no account token - the client purchase did not carry ReceiptBuyerToken (obfuscatedAccountId / appAccountToken, see StorePurchaseFlow), the grant is refused", receipt._storeName.c_str(), receipt._transactionID.c_str() );
                            return EconomyResult::BuyerTokenMissing;
                        }
                        return HTTPUtil::isEqualIgnoreCase( receipt._storeBuyerID, ReceiptBuyerToken::make( input._accountID ) ) ? EconomyResult::Ok : EconomyResult::NotPurchaser; // UUID 는 대소문자를 가리지 않는다
                    }
                    case ReceiptBuyerBinding::LinkedLogin:
                    {
                        if ( receipt._storeBuyerID.empty() )
                            return EconomyResult::NotPurchaser;
                        if ( input._pExternalLinks == nullptr )
                            return EconomyServiceResult::fail( OnlineError::kUnavailable ); // 서비스가 기동 때 거절한다 — 바로 부른 쪽의 실수이므로 지급하지 않는다
                        bool                     bLinked = false;
                        const ServiceStoreResult read    = input._pExternalLinks->readIsLinked( connection, input._accountID, receipt._buyerRule._loginProvider, receipt._storeBuyerID, bLinked );
                        if ( read != ServiceStoreResult::Ok )
                            return EconomyServiceResult::fail( OnlineError::kUnavailable );
                        return bLinked ? EconomyResult::Ok : EconomyResult::NotPurchaser;
                    }
                    case ReceiptBuyerBinding::Count:
                    {
                        return EconomyResult::ReceiptInvalid;
                    }
                }
                return EconomyResult::ReceiptInvalid;
            }

            static constexpr uint64 kDeliveryRecordVersion = 1;

            static void writeText( BitWriter& writer, string_view text )
            {
                writer.writeBlob( reinterpret_cast<const uint8*>( text.data() ), static_cast<int32>( text.size() ) );
            }

            [[nodiscard]] static bool readText( BitReader& reader, int32 maxSize, string& outText )
            {
                vector<uint8> listByte;
                if ( reader.readBlob( listByte, maxSize ) == false )
                    return false;
                outText.assign( listByte.begin(), listByte.end() );
                return true;
            }

            static vector<uint8> encodeDelivery( const ReceiptDeliveryTask& task )
            {
                BitWriter writer;
                writer.writeVarUint( kDeliveryRecordVersion );
                writeText( writer, task._storeName );
                writeText( writer, task._deliveryKey );
                writer.writeVarInt( task._deadlineMs );
                return writer.releaseBytes();
            }

            [[nodiscard]] static bool decodeDelivery( const ServiceRecord& record, ReceiptDeliveryTask& outTask )
            {
                BitReader reader( record._bytes.data(), static_cast<int32>( record._bytes.size() ) );
                if ( reader.readVarUint() != kDeliveryRecordVersion )
                    return false;
                if ( readText( reader, EconomyProtocol::kMaxStoreNameSize, outTask._storeName ) == false || readText( reader, EconomyProtocol::kMaxReceiptPayloadSize, outTask._deliveryKey ) == false )
                    return false;
                outTask._deadlineMs = reader.readVarInt();
                outTask._recordKey  = record._key;
                return reader.hasOverflowed() == false;
            }

            /**
             * @brief 지급 분개를 커밋합니다. 스토어에 알려야 하는 영수증이면 알림 레코드를 같은 트랜잭션에 넣습니다.
             * @details 재생(이미 지급한 거래)도 쓴다 — 스토어가 아직 확인 전이라고 답했으니 지난번 알림이 끝나지 않았다. 그 쓰기가 실패하면 Unavailable 로 답해 클라이언트가 같은 영수증을 다시 내게 한다.
             */
            [[nodiscard]] static LedgerResult commitRedeem( IServiceStoreConnection& connection, const LedgerTransferRequest& request, const ReceiptValidationResult& receipt, LedgerTransferResult& outServiceResult )
            {
                ReceiptDeliveryTask task;
                if ( EconomyStoreLogic::makeDeliveryTask( receipt, task ) == false )
                    return Ledger::executeTransfer( connection, request, outServiceResult );
                const vector<uint8> recordBytes = encodeDelivery( task );
                for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
                {
                    ServiceTransaction transaction;
                    const LedgerResult staged = Ledger::stageTransfer( connection, request, transaction, outServiceResult );
                    if ( staged != LedgerResult::Ok )
                        return staged;
                    const bool bReplayed = outServiceResult._bReplayed == SW_TRUE;
                    transaction.put( EconomyStoreLogic::getDeliveryTable(), task._recordKey, recordBytes );
                    const ServiceStoreResult committed = connection.commit( transaction );
                    if ( committed == ServiceStoreResult::Ok )
                        return LedgerResult::Ok;
                    if ( committed == ServiceStoreResult::Invalid )
                        return LedgerResult::Invalid;
                    if ( bReplayed )
                        return LedgerResult::Unavailable;
                    const LedgerResult resolved = Ledger::resolveConflict( connection, request, outServiceResult );
                    if ( resolved != LedgerResult::Conflict )
                        return resolved;
                    if ( committed == ServiceStoreResult::Unavailable )
                        return LedgerResult::Unavailable; // 분개가 없어도 커밋이 늦게 닿을 수 있다 — 같은 거래로 나중에 가려진다
                }
                return LedgerResult::Conflict;
            }

            /** @brief 응답에 업무 결과를 적고 결과를 돌려줍니다. 결과 칸이 없는 응답(지갑 · 내역)은 결과만 돌려준다. */
            template <typename TReply>
            static EconomyServiceResult finish( TReply& outReply, EconomyServiceResult result )
            {
                if constexpr ( OnlineReplyHasResult<TReply>::value )
                    outReply._result = result._result;
                else
                    (void)outReply;
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
            static EconomyServiceResult appendPriceLegs( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const OfferPrice& price, int64 totalAmount,
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
                        return EconomyServiceResult::fail( OnlineError::kUnavailable );
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
             * @brief 같은 분개 키의 분개가 이미 있으면 그 결과를 돌려주고(재생이면 Ok 이고 @p outReply 에 잔액을 넣는다, 다른 구매의 키이면 `kInvalidRequest`) 없으면 빈 값입니다.
             * @details 지금 잔액으로 분개 항목을 다시 짤 수 없다. 재원이 비었을 수 있기 때문이다. 그래서 저장된 분개의 항목으로 재생한다.
             */
            template <typename TReply>
            [[nodiscard]] static optional<EconomyServiceResult> findReplay( IServiceStoreConnection& connection, const LedgerTransferRequest& request, uint64 accountID, TReply& outReply )
            {
                LedgerJournalEntry entry;
                if ( Ledger::findJournal( connection, request._journalKey, entry ) != ServiceStoreResult::Ok )
                    return nullopt;
                if ( entry._reason != request._reason || entry._memo != request._memo )
                    return EconomyServiceResult::fail( OnlineError::kInvalidRequest ); // 다른 구매에 쓴 멱등 키이다
                LedgerTransferRequest stored = request;
                stored._listPosting          = entry._listPosting;
                LedgerTransferResult replay;
                if ( Ledger::resolveConflict( connection, stored, replay ) != LedgerResult::Ok )
                    return EconomyServiceResult::fail( OnlineError::kInvalidRequest );
                EconomyStoreLogic::collectAccountBalances( replay, accountID, outReply._listBalance );
                outReply._bReplayed = SW_TRUE;
                return EconomyServiceResult{ EconomyResult::Ok };
            }

            /**
             * @brief 가격 항목과 지급 항목으로 구매 분개 요청을 짭니다.
             * @details 구매를 여기서 끝내야 하면 그 결과를 돌려주고, 이어 가도 되면 빈 값입니다. 가격이 모자라면 같은 키로 이미 산 것인지 먼저 확인합니다.
             *          첫 구매로 잔액이 줄어든 뒤의 재시도가 "부족" 을 받으면 안 되기 때문입니다.
             */
            [[nodiscard]] static optional<EconomyServiceResult> buildPurchaseRequest( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const OfferDef& offer,
                                                                                      const EconomyPurchaseInput& input, EconomyPurchaseReply& outReply, LedgerTransferRequest& outRequest )
            {
                const LedgerHolder account = LedgerHolder::makeAccount( input._accountID );
                const int64        count   = input._count;
                outRequest._journalKey     = input._journalKey;
                outRequest._reason         = "shop.buy";
                outRequest._memo           = offer._id;
                outRequest._pPolicy        = &currencies.getLedgerPolicy();
                outRequest._timeMs         = input._nowMs;
                outRequest._actorID        = input._accountID;
                outRequest._actorKind      = LedgerActorKind::Player;
                for ( const OfferPrice& price : offer._listPrice )
                {
                    if ( price._amount > LedgerConstant::kMaxAmount / count )
                        return EconomyServiceResult::fail( OnlineError::kInvalidRequest );
                    const EconomyServiceResult priced = appendPriceLegs( connection, currencies, price, price._amount * count, account, outRequest );
                    if ( priced == EconomyResult::Ok )
                        continue;
                    if ( const optional<EconomyServiceResult> replayed = findReplay( connection, outRequest, input._accountID, outReply ) )
                        return replayed;
                    return priced;
                }
                for ( const OfferGrant& grant : offer._listGrant )
                {
                    if ( grant._amount > LedgerConstant::kMaxAmount / count )
                        return EconomyServiceResult::fail( OnlineError::kInvalidRequest );
                    outRequest._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), account, grant._assetID, grant._amount * count } );
                }
                return nullopt;
            }

            /**
             * @brief 계정별 구매 한도가 있는 상품이면 구매 수를 늘려 @p inoutTransaction 에 붙입니다.
             * @details 구매를 여기서 끝내야 하면 그 결과(저장소 오류, 한도 초과)를 돌려주고, 이어 가도 되면 빈 값입니다.
             */
            [[nodiscard]] static optional<EconomyServiceResult> stageAccountLimit( IServiceStoreConnection& connection, const OfferDef& offer, const EconomyPurchaseInput& input,
                                                                                   ServiceTransaction& inoutTransaction )
            {
                if ( offer._limitPerAccount <= 0 )
                    return nullopt;
                const int64              count    = input._count;
                const string             countKey = makePurchaseCountKey( input._accountID, offer._id );
                ServiceRecord            countRecord;
                const ServiceStoreResult read = connection.readRecord( EconomyStoreLogic::getPurchaseCountTable(), countKey, countRecord );
                if ( read != ServiceStoreResult::Ok && read != ServiceStoreResult::NotFound )
                    return EconomyServiceResult::fail( OnlineError::kUnavailable );
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
                return nullopt;
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

    EconomyServiceResult EconomyStoreLogic::toEconomyResult( LedgerResult result )
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
                return EconomyServiceResult::fail( OnlineError::kInvalidRequest );
            case LedgerResult::Conflict:
                return EconomyServiceResult::fail( OnlineError::kBusy );
            case LedgerResult::Invalid:
                return EconomyServiceResult::fail( OnlineError::kInvalidRequest );
            case LedgerResult::Unavailable:
                return EconomyServiceResult::fail( OnlineError::kUnavailable );
        }
        return EconomyServiceResult::fail( OnlineError::kUnavailable );
    }

    void EconomyStoreLogic::collectAccountBalances( const LedgerTransferResult& serviceResult, uint64 accountID, vector<LedgerBalance>& outListBalance )
    {
        const LedgerHolder account = LedgerHolder::makeAccount( accountID );
        for ( const LedgerTransferResult::HolderBalance& holderBalance : serviceResult._listHolderBalance )
        {
            if ( holderBalance._holder == account )
                outListBalance.push_back( holderBalance._balance );
        }
    }

    EconomyServiceResult EconomyStoreLogic::purchase( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const OfferCatalog& offers,
                                                      const EconomyPurchaseInput& input, EconomyPurchaseReply& outReply )
    {
        outReply               = EconomyPurchaseReply{};
        const OfferDef* pOffer = offers.findOffer( input._offerID );
        if ( pOffer == nullptr )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::UnknownOffer );
        if ( pOffer->isRealMoney() )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::NotPurchasable );
        const bool bCountOk         = 1 <= input._count && input._count <= pOffer->_maxCountPerPurchase;
        const bool bInvalidPurchase = bCountOk == false || input._accountID == 0 || LedgerUtil::isValidJournalKey( input._journalKey ) == false;
        if ( bInvalidPurchase )
            return EconomyStoreLogicInternal::finish( outReply, EconomyServiceResult::fail( OnlineError::kInvalidRequest ) );
        if ( pOffer->isOnSale( input._nowMs ) == false )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::NotOnSale );

        // 재시도는 잔액과 한도 판정보다 먼저 지난 결과를 확인한다. 가상 화폐 분개 항목은 지금 잔액으로 짜므로 다시 짜면 처음과 다를 수 있다.
        if ( const optional<EconomyServiceResult> replayed = EconomyStoreLogicInternal::findReplay( connection, EconomyStoreLogicInternal::makePurchaseProbe( input, pOffer->_id ), input._accountID, outReply ) )
            return EconomyStoreLogicInternal::finish( outReply, *replayed );
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            LedgerTransferRequest request;
            if ( const optional<EconomyServiceResult> early = EconomyStoreLogicInternal::buildPurchaseRequest( connection, currencies, *pOffer, input, outReply, request ) )
                return EconomyStoreLogicInternal::finish( outReply, *early );

            ServiceTransaction   transaction;
            LedgerTransferResult serviceResult;
            const LedgerResult   staged = Ledger::stageTransfer( connection, request, transaction, serviceResult );
            if ( staged != LedgerResult::Ok )
            {
                if ( staged == LedgerResult::InsufficientFunds )
                {
                    if ( const optional<EconomyServiceResult> replayed = EconomyStoreLogicInternal::findReplay( connection, request, input._accountID, outReply ) )
                        return EconomyStoreLogicInternal::finish( outReply, *replayed );
                }
                return EconomyStoreLogicInternal::finish( outReply, toEconomyResult( staged ) );
            }
            if ( serviceResult._bReplayed == SW_TRUE )
            {
                collectAccountBalances( serviceResult, input._accountID, outReply._listBalance );
                outReply._bReplayed = SW_TRUE;
                return EconomyStoreLogicInternal::finish( outReply, EconomyResult::Ok );
            }
            if ( const optional<EconomyServiceResult> early = EconomyStoreLogicInternal::stageAccountLimit( connection, *pOffer, input, transaction ) )
                return EconomyStoreLogicInternal::finish( outReply, *early );
            const ServiceStoreResult committed = connection.commit( transaction );
            if ( committed == ServiceStoreResult::Ok )
            {
                collectAccountBalances( serviceResult, input._accountID, outReply._listBalance );
                return EconomyStoreLogicInternal::finish( outReply, EconomyResult::Ok );
            }
            if ( committed == ServiceStoreResult::Invalid )
                return EconomyStoreLogicInternal::finish( outReply, EconomyServiceResult::fail( OnlineError::kInvalidRequest ) );
            const LedgerResult resolved = Ledger::resolveConflict( connection, request, serviceResult );
            if ( resolved == LedgerResult::Ok )
            {
                collectAccountBalances( serviceResult, input._accountID, outReply._listBalance );
                outReply._bReplayed = serviceResult._bReplayed;
                return EconomyStoreLogicInternal::finish( outReply, EconomyResult::Ok );
            }
            if ( resolved != LedgerResult::Conflict )
                return EconomyStoreLogicInternal::finish( outReply, toEconomyResult( resolved ) );
            if ( committed == ServiceStoreResult::Unavailable )
                return EconomyStoreLogicInternal::finish( outReply, EconomyServiceResult::fail( OnlineError::kUnavailable ) ); // 적용됐는지 알 수 없다. 같은 키로 다시 하면 중복이 가려진다
            // Conflict 이다. 잔액이나 구매 수가 그사이 바뀌었으므로 다시 읽어 다시 짠다
        }
        return EconomyStoreLogicInternal::finish( outReply, EconomyServiceResult::fail( OnlineError::kBusy ) );
    }

    EconomyServiceResult EconomyStoreLogic::redeemReceipt( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const OfferCatalog& offers,
                                                           const EconomyRedeemInput& input, EconomyRedeemReply& outReply )
    {
        outReply                               = EconomyRedeemReply{};
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
            return EconomyStoreLogicInternal::finish( outReply, EconomyServiceResult::fail( OnlineError::kInvalidRequest ) );
        const EconomyServiceResult buyer = EconomyStoreLogicInternal::compareBuyer( connection, input );
        if ( buyer != EconomyResult::Ok )
            return EconomyStoreLogicInternal::finish( outReply, buyer );
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
            return EconomyStoreLogicInternal::finish( outReply, EconomyServiceResult::fail( OnlineError::kUnavailable ) );
        request._reason            = "iap.redeem";
        request._memo              = receipt._productID.substr( 0, static_cast<size_t>( LedgerConstant::kMaxMemoSize ) );
        request._pPolicy           = &currencies.getLedgerPolicy();
        request._timeMs            = input._nowMs;
        request._actorID           = input._accountID;
        request._actorKind         = LedgerActorKind::Player;
        const LedgerHolder account = LedgerHolder::makeAccount( input._accountID );
        for ( const OfferGrant& grant : pOffer->_listGrant )
        {
            request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), account, grant._assetID, grant._amount } );
        }
        LedgerTransferResult serviceResult;
        const LedgerResult   result = EconomyStoreLogicInternal::commitRedeem( connection, request, receipt, serviceResult );
        if ( result == LedgerResult::JournalKeyReused )
            return EconomyStoreLogicInternal::finish( outReply, EconomyResult::AlreadyRedeemed ); // 받는 계정이 달라서 분개 내용이 다르다
        if ( result != LedgerResult::Ok )
            return EconomyStoreLogicInternal::finish( outReply, toEconomyResult( result ) );
        collectAccountBalances( serviceResult, input._accountID, outReply._listBalance );
        outReply._bReplayed = serviceResult._bReplayed;
        return EconomyStoreLogicInternal::finish( outReply, EconomyResult::Ok );
    }

    const hashed_string& EconomyStoreLogic::getRefundTable()
    {
        static const hashed_string s_table{ "econ_refund" };
        return s_table;
    }

    const hashed_string& EconomyStoreLogic::getDeliveryTable()
    {
        static const hashed_string s_table{ "econ_delivery" };
        return s_table;
    }

    bool EconomyStoreLogic::makeDeliveryTask( const ReceiptValidationResult& receipt, ReceiptDeliveryTask& outTask )
    {
        if ( receipt._deliveryKey.empty() || EconomyStoreLogicInternal::makeRedeemKey( receipt._storeName, receipt._transactionID, outTask._recordKey ) == false )
            return false;
        outTask._storeName   = receipt._storeName;
        outTask._deliveryKey = receipt._deliveryKey;
        outTask._deadlineMs  = receipt._deliveryDeadlineMs;
        return true;
    }

    ServiceStoreResult EconomyStoreLogic::listDeliveries( IServiceStoreConnection& connection, int32 maxCount, vector<ReceiptDeliveryTask>& outListTask )
    {
        vector<ServiceRecord>    listRecord;
        const ServiceStoreResult read = connection.listRecords( getDeliveryTable(), "", "", maxCount, false, listRecord );
        if ( read != ServiceStoreResult::Ok )
            return read;
        for ( const ServiceRecord& record : listRecord )
        {
            ReceiptDeliveryTask task;
            if ( EconomyStoreLogicInternal::decodeDelivery( record, task ) )
                outListTask.push_back( std::move( task ) );
            else
                SW_LOG_ERROR_ID( "EconomyStoreLogic.DeliveryRecordUnreadable", "The delivery record '%#' is unreadable - the store refunds that purchase when its deadline passes", record._key.c_str() );
        }
        return ServiceStoreResult::Ok;
    }

    ServiceStoreResult EconomyStoreLogic::eraseDelivery( IServiceStoreConnection& connection, string_view recordKey )
    {
        ServiceTransaction transaction;
        transaction.erase( getDeliveryTable(), recordKey );
        return connection.commit( transaction );
    }

    const hashed_string& EconomyStoreLogic::getRefundCursorTable()
    {
        static const hashed_string s_table{ "econ_refund_cursor" };
        return s_table;
    }

    ServiceStoreResult EconomyStoreLogic::readRefundCursor( IServiceStoreConnection& connection, string_view storeName, int64& outLastReadMs )
    {
        outLastReadMs = 0;
        ServiceRecord            record;
        const ServiceStoreResult read = connection.readRecord( getRefundCursorTable(), storeName, record );
        if ( read == ServiceStoreResult::NotFound )
            return ServiceStoreResult::Ok;
        if ( read != ServiceStoreResult::Ok )
            return read;
        BitReader   reader( record._bytes.data(), static_cast<int32>( record._bytes.size() ) );
        const int64 lastReadMs = reader.readVarInt();
        if ( reader.hasOverflowed() || lastReadMs < 0 )
        {
            SW_LOG_ERROR_ID( "EconomyStoreLogic.RefundCursorUnreadable", "The refund cursor of store '%#' is unreadable - refund polling for that store waits until the record is fixed", storeName );
            return ServiceStoreResult::Invalid;
        }
        outLastReadMs = lastReadMs;
        return ServiceStoreResult::Ok;
    }

    ServiceStoreResult EconomyStoreLogic::writeRefundCursor( IServiceStoreConnection& connection, string_view storeName, int64 lastReadMs )
    {
        BitWriter writer;
        writer.writeVarInt( lastReadMs );
        ServiceTransaction transaction;
        transaction.put( getRefundCursorTable(), storeName, writer.getBytes() );
        return connection.commit( transaction );
    }

    EconomyServiceResult EconomyStoreLogic::revokeRefund( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const ReceiptRefundNotification& notice, int64 nowMs,
                                                          vector<EconomyBalanceChange>& outListChange )
    {
        using Internal = EconomyStoreLogicInternal;
        outListChange.clear();
        string redeemKey;
        if ( Internal::makeRedeemKey( notice._storeName, notice._transactionID, redeemKey ) == false )
            return EconomyServiceResult::fail( OnlineError::kInvalidRequest );
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            ServiceRecord            mark;
            const ServiceStoreResult markRead = connection.readRecord( getRefundTable(), redeemKey, mark );
            if ( markRead == ServiceStoreResult::Ok )
                return EconomyResult::AlreadyRefunded; // 이미 회수했거나 청약철회로 돌려받았다
            if ( markRead != ServiceStoreResult::NotFound )
                return EconomyServiceResult::fail( OnlineError::kUnavailable );
            LedgerJournalEntry       redeem;
            const ServiceStoreResult found = Ledger::findJournal( connection, redeemKey, redeem );
            if ( found != ServiceStoreResult::Ok && found != ServiceStoreResult::NotFound )
                return EconomyServiceResult::fail( OnlineError::kUnavailable );
            ServiceTransaction transaction;
            transaction.put( getRefundTable(), redeemKey, vector<uint8>{ Internal::kMarkRefunded }, ServiceRecord::kAbsentVersion );
            if ( found == ServiceStoreResult::NotFound )
            {
                // 지급 전에 환불이 왔다. 표시만 남겨 뒤에 온 지급을 막는다
                const ServiceStoreResult committed = connection.commit( transaction );
                if ( committed == ServiceStoreResult::Conflict )
                    continue;
                return committed == ServiceStoreResult::Ok ? EconomyResult::Ok : EconomyServiceResult::fail( OnlineError::kUnavailable );
            }
            LedgerTransferRequest request;
            if ( LedgerJournalKey::makeFromToken( string( Internal::kRefundScopePrefix ) + notice._storeName, notice._transactionID, request._journalKey ) == false )
                return EconomyServiceResult::fail( OnlineError::kInvalidRequest );
            request._reason     = "refund.revoke";
            request._memo       = redeem._memo;
            request._pPolicy    = &currencies.getLedgerPolicy();
            request._timeMs     = nowMs;
            request._actorKind  = LedgerActorKind::System;
            request._bAllowDebt = SW_TRUE; // 환불 회수만 빚을 허용한다
            for ( const LedgerPosting& posting : redeem._listPosting )
            {
                request._listPosting.push_back( LedgerPosting{ posting._to, LedgerHolder::makeSink(), posting._assetID, posting._amount } );
            }
            LedgerTransferResult serviceResult;
            const LedgerResult   staged = Ledger::stageTransfer( connection, request, transaction, serviceResult );
            if ( staged != LedgerResult::Ok )
                return toEconomyResult( staged );
            const ServiceStoreResult committed = connection.commit( transaction );
            if ( committed == ServiceStoreResult::Ok )
            {
                for ( const LedgerTransferResult::HolderBalance& holderBalance : serviceResult._listHolderBalance )
                {
                    if ( holderBalance._holder._kind == LedgerHolderKind::Account )
                        outListChange.push_back( EconomyBalanceChange{ holderBalance._holder._accountID, holderBalance._balance } );
                }
                return EconomyResult::Ok;
            }
            if ( committed != ServiceStoreResult::Conflict )
                return EconomyServiceResult::fail( OnlineError::kUnavailable );
        }
        return EconomyServiceResult::fail( OnlineError::kBusy );
    }

    EconomyServiceResult EconomyStoreLogic::withdrawPurchase( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const EconomyWithdrawInput& input,
                                                              EconomyWithdrawReply& outReply )
    {
        using Internal = EconomyStoreLogicInternal;
        outReply       = EconomyWithdrawReply{};
        string redeemKey;
        if ( input._accountID == 0 || Internal::makeRedeemKey( input._storeName, input._transactionID, redeemKey ) == false )
            return Internal::finish( outReply, EconomyServiceResult::fail( OnlineError::kInvalidRequest ) );
        const LedgerHolder account = LedgerHolder::makeAccount( input._accountID );
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            ServiceRecord            mark;
            const ServiceStoreResult markRead = connection.readRecord( getRefundTable(), redeemKey, mark );
            if ( markRead == ServiceStoreResult::Ok )
                return Internal::finish( outReply, EconomyResult::AlreadyRefunded );
            if ( markRead != ServiceStoreResult::NotFound )
                return Internal::finish( outReply, EconomyServiceResult::fail( OnlineError::kUnavailable ) );
            LedgerJournalEntry       redeem;
            const ServiceStoreResult found = Ledger::findJournal( connection, redeemKey, redeem );
            if ( found == ServiceStoreResult::NotFound )
                return Internal::finish( outReply, EconomyResult::NotPurchaser );
            if ( found != ServiceStoreResult::Ok )
                return Internal::finish( outReply, EconomyServiceResult::fail( OnlineError::kUnavailable ) );
            if ( input._nowMs - redeem._timeMs > input._windowMs )
                return Internal::finish( outReply, EconomyResult::WithdrawalExpired );
            LedgerTransferRequest request;
            if ( LedgerJournalKey::makeFromToken( string( Internal::kWithdrawScopePrefix ) + input._storeName, input._transactionID, request._journalKey ) == false )
                return Internal::finish( outReply, EconomyServiceResult::fail( OnlineError::kInvalidRequest ) );
            request._reason    = "iap.withdraw";
            request._memo      = redeem._memo;
            request._pPolicy   = &currencies.getLedgerPolicy();
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
                    return Internal::finish( outReply, EconomyServiceResult::fail( OnlineError::kUnavailable ) );
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
            LedgerTransferResult serviceResult;
            const LedgerResult   staged = Ledger::stageTransfer( connection, request, transaction, serviceResult );
            if ( staged != LedgerResult::Ok )
                return Internal::finish( outReply, toEconomyResult( staged ) );
            const ServiceStoreResult committed = connection.commit( transaction );
            if ( committed == ServiceStoreResult::Ok )
            {
                collectAccountBalances( serviceResult, input._accountID, outReply._listBalance );
                return Internal::finish( outReply, EconomyResult::Ok );
            }
            if ( committed != ServiceStoreResult::Conflict )
                return Internal::finish( outReply, EconomyServiceResult::fail( OnlineError::kUnavailable ) );
        }
        return Internal::finish( outReply, EconomyServiceResult::fail( OnlineError::kBusy ) );
    }

    EconomyServiceResult EconomyStoreLogic::readWallet( IServiceStoreConnection& connection, uint64 accountID, EconomyWalletReply& outReply )
    {
        outReply                      = EconomyWalletReply{};
        const ServiceStoreResult read = Ledger::listBalances( connection, LedgerHolder::makeAccount( accountID ), outReply._listBalance );
        return EconomyStoreLogicInternal::finish( outReply, read == ServiceStoreResult::Ok ? EconomyResult::Ok : EconomyServiceResult::fail( OnlineError::kUnavailable ) );
    }

    EconomyServiceResult EconomyStoreLogic::readHistory( IServiceStoreConnection& connection, uint64 accountID, const EconomyHistoryRequest& request, EconomyHistoryReply& outReply )
    {
        outReply                           = EconomyHistoryReply{};
        const LedgerHolder         account = LedgerHolder::makeAccount( accountID );
        vector<LedgerJournalEntry> listEntry;
        const ServiceStoreResult   read = Ledger::listHistory( connection, account, request._cursor, request._maxCount, listEntry, outReply._nextCursor );
        if ( read != ServiceStoreResult::Ok )
            return EconomyStoreLogicInternal::finish( outReply, read == ServiceStoreResult::Invalid ? EconomyServiceResult::fail( OnlineError::kInvalidRequest ) : EconomyServiceResult::fail( OnlineError::kUnavailable ) );
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
