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

            /** @brief 거래 하나의 상태입니다(영수증 상태 레코드의 값 — 저장 형식이라 번호를 바꾸지 않는다). */
            enum class ReceiptState : uint8
            {
                None = 0,      ///< 레코드가 없다 — 지급도 환불도 오지 않았다
                Redeemed,      ///< 지급했다(계정 = 받은 계정)
                RefundedFirst, ///< 지급 전에 환불이 왔다 — 뒤에 온 지급을 막는다
                Revoked,       ///< 지급 뒤 환불로 회수했다
                Withdrawn,     ///< 청약철회로 돌려받았다
                Count
            };

            /** @brief 읽은 영수증 상태와 그 버전입니다. 쓰기는 이 버전을 커밋 조건으로 건다. */
            struct ReceiptRecord
            {
                uint64       _accountID{ 0 };
                uint64       _version{ ServiceRecord::kAbsentVersion };
                ReceiptState _state{ ReceiptState::None };
            };

            static constexpr uint64 kReceiptRecordVersion = 1;

            [[nodiscard]] static bool isRefunded( ReceiptState state )
            {
                return state == ReceiptState::RefundedFirst || state == ReceiptState::Revoked || state == ReceiptState::Withdrawn;
            }

            static vector<uint8> encodeReceipt( ReceiptState state, uint64 accountID )
            {
                BitWriter writer;
                writer.writeVarUint( kReceiptRecordVersion );
                writer.writeVarUint( static_cast<uint64>( state ) );
                writer.writeVarUint( accountID ); // 청약철회는 이 계정만 한다. 운영 조회용이기도 하다
                return writer.releaseBytes();
            }

            /** @brief 영수증 상태를 읽습니다. 레코드가 없으면 `Ok` 와 `None` 입니다. 값을 읽을 수 없으면 오류 로그와 `Invalid` 입니다. */
            [[nodiscard]] static ServiceStoreResult readReceipt( IServiceStoreConnection& connection, string_view redeemKey, ReceiptRecord& outRecord )
            {
                outRecord = ReceiptRecord{};
                ServiceRecord            record;
                const ServiceStoreResult read = connection.readRecord( EconomyStoreLogic::getReceiptTable(), redeemKey, record );
                if ( read == ServiceStoreResult::NotFound )
                    return ServiceStoreResult::Ok;
                if ( read != ServiceStoreResult::Ok )
                    return read;
                BitReader    reader( record._bytes.data(), static_cast<int32>( record._bytes.size() ) );
                const uint64 format    = reader.readVarUint();
                const uint64 state     = reader.readVarUint();
                const uint64 accountID = reader.readVarUint();
                if ( reader.hasOverflowed() || format != kReceiptRecordVersion || state == 0 || state >= static_cast<uint64>( ReceiptState::Count ) )
                {
                    SW_LOG_ERROR_ID( "EconomyStoreLogic.ReceiptRecordUnreadable", "The receipt record '%#' is unreadable - grants, refunds and withdrawals of that transaction wait until the record is fixed", redeemKey );
                    return ServiceStoreResult::Invalid;
                }
                outRecord._accountID = accountID;
                outRecord._version   = record._version;
                outRecord._state     = static_cast<ReceiptState>( state );
                return ServiceStoreResult::Ok;
            }

            /** @brief 지급의 원장 결과를 업무 결과로 바꿉니다. 분개 키를 다른 내용(다른 계정)으로 쓰려 했으면 이미 받은 거래다. */
            static EconomyServiceResult toRedeemResult( LedgerResult result )
            {
                if ( result == LedgerResult::JournalKeyReused )
                    return EconomyResult::AlreadyRedeemed;
                return EconomyStoreLogic::toEconomyResult( result );
            }

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
             * @brief 영수증 지급을 커밋합니다. 스토어에 알려야 하는 영수증이면 알림 레코드를 같은 트랜잭션에 넣습니다.
             * @details 영수증 상태 레코드(`econ_receipt`)를 읽고, 새 지급이면 "없어야 함" 조건으로 `Redeemed` 를 넣는다. 환불도 같은 레코드를 읽은 버전 조건으로 쓰므로
             *          읽기와 커밋 사이에 환불이 표시하면 이 커밋이 Conflict 로 지고 다시 읽어 ReceiptRefunded 가 된다(감사 O-1 — PlayFab · Nakama 가 구매 검증과 환불을 같은 행 제약 아래 두는 것과 같다).
             *          분개가 남은 다시 보내기는 같은 결과(`_bReplayed`)이고 알림 레코드도 다시 쓴다(스토어가 아직 확인 전이라고 답했다) — 그 쓰기도 상태 버전을 건다.
             *          분개가 보존 정리로 지워진 뒤의 다시 보내기는 AlreadyRedeemed 다(감사 P0-7).
             */
            [[nodiscard]] static EconomyServiceResult commitRedeem( IServiceStoreConnection& connection, const LedgerTransferRequest& request, const ReceiptValidationResult& receipt,
                                                                    uint64 accountID, LedgerTransferResult& outServiceResult )
            {
                const hashed_string& receiptTable = EconomyStoreLogic::getReceiptTable();
                const vector<uint8>  redeemed     = encodeReceipt( ReceiptState::Redeemed, accountID );
                ReceiptDeliveryTask  task;
                const bool           bDelivery   = EconomyStoreLogic::makeDeliveryTask( receipt, task );
                const vector<uint8>  recordBytes = bDelivery ? encodeDelivery( task ) : vector<uint8>{};
                for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
                {
                    ReceiptRecord mark;
                    if ( readReceipt( connection, request._journalKey, mark ) != ServiceStoreResult::Ok )
                        return EconomyServiceResult::fail( OnlineError::kUnavailable );
                    if ( isRefunded( mark._state ) )
                        return EconomyResult::ReceiptRefunded; // 지급 전에 환불이 왔거나 이미 돌려받은 거래이다
                    ServiceTransaction transaction;
                    const LedgerResult staged = Ledger::stageTransfer( connection, request, transaction, outServiceResult );
                    if ( staged != LedgerResult::Ok )
                        return toRedeemResult( staged );
                    const bool bReplayed = outServiceResult._bReplayed == SW_TRUE;
                    if ( bReplayed == false )
                    {
                        if ( mark._state == ReceiptState::Redeemed )
                            return EconomyResult::AlreadyRedeemed; // 분개는 보존 정리로 지워졌지만 이 거래는 이미 지급했다
                        transaction.put( receiptTable, request._journalKey, redeemed, ServiceRecord::kAbsentVersion );
                    }
                    else if ( bDelivery )
                    {
                        transaction.requireVersion( receiptTable, request._journalKey, mark._version ); // 그사이 환불된 거래의 확인은 보내지 않는다
                    }
                    if ( bReplayed && bDelivery == false )
                        return EconomyResult::Ok; // 분개가 남은 다시 보내기 — 쓸 것이 없다
                    if ( bDelivery )
                        transaction.put( EconomyStoreLogic::getDeliveryTable(), task._recordKey, recordBytes );
                    const ServiceStoreResult committed = connection.commit( transaction );
                    if ( committed == ServiceStoreResult::Ok )
                        return EconomyResult::Ok;
                    if ( committed == ServiceStoreResult::Invalid )
                        return toRedeemResult( LedgerResult::Invalid );
                    if ( bReplayed )
                    {
                        if ( committed == ServiceStoreResult::Conflict )
                            continue; // 상태가 바뀌었다 — 다시 읽어 환불이면 거절한다
                        return EconomyServiceResult::fail( OnlineError::kUnavailable );
                    }
                    const LedgerResult resolved = Ledger::resolveConflict( connection, request, outServiceResult );
                    if ( resolved != LedgerResult::Conflict )
                        return toRedeemResult( resolved );
                    if ( committed == ServiceStoreResult::Unavailable )
                        return EconomyServiceResult::fail( OnlineError::kUnavailable ); // 분개가 없어도 커밋이 늦게 닿을 수 있다 — 같은 거래로 나중에 가려진다
                    // Conflict 이다. 상태(환불) · 잔액이 그사이 바뀌었으므로 다시 읽는다
                }
                return EconomyServiceResult::fail( OnlineError::kBusy );
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
            case LedgerResult::AccountDeleted:
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
        LedgerTransferResult       serviceResult;
        const EconomyServiceResult result = EconomyStoreLogicInternal::commitRedeem( connection, request, receipt, input._accountID, serviceResult );
        if ( result != EconomyResult::Ok )
            return EconomyStoreLogicInternal::finish( outReply, result );
        collectAccountBalances( serviceResult, input._accountID, outReply._listBalance );
        outReply._bReplayed = serviceResult._bReplayed;
        return EconomyStoreLogicInternal::finish( outReply, EconomyResult::Ok );
    }

    const hashed_string& EconomyStoreLogic::getReceiptTable()
    {
        static const hashed_string s_table{ "econ_receipt" };
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

    EconomyServiceResult EconomyStoreLogic::revokeRefund( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const ReceiptRefundNotification& notice,
                                                          EconomyRefundRecoveryMode recovery, int64 nowMs, vector<EconomyBalanceChange>& outListChange )
    {
        using Internal = EconomyStoreLogicInternal;
        outListChange.clear();
        if ( recovery >= EconomyRefundRecoveryMode::Count )
            return EconomyServiceResult::fail( OnlineError::kInvalidRequest ); // 서비스가 기동 때 거절한다 — 바로 부른 쪽의 실수이므로 회수하지 않는다
        string redeemKey;
        if ( Internal::makeRedeemKey( notice._storeName, notice._transactionID, redeemKey ) == false )
            return EconomyServiceResult::fail( OnlineError::kInvalidRequest );
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            Internal::ReceiptRecord mark;
            if ( Internal::readReceipt( connection, redeemKey, mark ) != ServiceStoreResult::Ok )
                return EconomyServiceResult::fail( OnlineError::kUnavailable );
            if ( Internal::isRefunded( mark._state ) )
                return EconomyResult::AlreadyRefunded; // 이미 회수했거나 청약철회로 돌려받았다
            ServiceTransaction transaction;
            if ( mark._state == Internal::ReceiptState::None )
            {
                // 지급 전에 환불이 왔다. 상태만 남겨 뒤에 온 지급을 막는다 — 지급도 이 레코드를 "없어야 함" 으로 쓰므로 먼저 커밋한 쪽이 이기고 다른 쪽은 다시 읽는다
                transaction.put( getReceiptTable(), redeemKey, Internal::encodeReceipt( Internal::ReceiptState::RefundedFirst, 0 ), ServiceRecord::kAbsentVersion );
                const ServiceStoreResult committed = connection.commit( transaction );
                if ( committed == ServiceStoreResult::Conflict )
                    continue;
                return committed == ServiceStoreResult::Ok ? EconomyResult::Ok : EconomyServiceResult::fail( OnlineError::kUnavailable );
            }
            transaction.put( getReceiptTable(), redeemKey, Internal::encodeReceipt( Internal::ReceiptState::Revoked, mark._accountID ), mark._version );
            LedgerJournalEntry       redeem;
            const ServiceStoreResult found = Ledger::findJournal( connection, redeemKey, redeem );
            if ( found == ServiceStoreResult::NotFound )
            {
                SW_LOG_ERROR_ID( "EconomyStoreLogic.RefundAfterJournalSwept", "The refund of '%#' came after its grant journal was swept - nothing is revoked from account %#, only the receipt is marked refunded", redeemKey, mark._accountID );
                const ServiceStoreResult committed = connection.commit( transaction );
                if ( committed == ServiceStoreResult::Conflict )
                    continue;
                return committed == ServiceStoreResult::Ok ? EconomyResult::Ok : EconomyServiceResult::fail( OnlineError::kUnavailable );
            }
            if ( found != ServiceStoreResult::Ok )
                return EconomyServiceResult::fail( OnlineError::kUnavailable );
            LedgerTransferRequest request;
            if ( LedgerJournalKey::makeFromToken( string( Internal::kRefundScopePrefix ) + notice._storeName, notice._transactionID, request._journalKey ) == false )
                return EconomyServiceResult::fail( OnlineError::kInvalidRequest );
            request._reason     = "refund.revoke";
            request._memo       = redeem._memo;
            request._pPolicy    = &currencies.getLedgerPolicy();
            request._timeMs     = nowMs;
            request._actorKind  = LedgerActorKind::System;
            request._bAllowDebt = recovery == EconomyRefundRecoveryMode::Debt ? SW_TRUE : SW_FALSE; // 빚은 환불 회수의 Debt 정책만 허용한다
            for ( const LedgerPosting& posting : redeem._listPosting )
            {
                int64 amount = posting._amount;
                if ( recovery == EconomyRefundRecoveryMode::Unspent )
                {
                    LedgerBalance balance;
                    if ( Ledger::readBalance( connection, posting._to, posting._assetID, balance ) != ServiceStoreResult::Ok )
                        return EconomyServiceResult::fail( OnlineError::kUnavailable );
                    amount = balance._amount < amount ? balance._amount : amount; // 남은 만큼만 — 이미 쓴 몫은 빚으로 만들지 않는다
                }
                if ( amount > 0 )
                    request._listPosting.push_back( LedgerPosting{ posting._to, LedgerHolder::makeSink(), posting._assetID, amount } );
            }
            LedgerTransferResult serviceResult;
            if ( request._listPosting.empty() == false )
            {
                const LedgerResult staged = Ledger::stageTransfer( connection, request, transaction, serviceResult );
                if ( staged == LedgerResult::InsufficientFunds && recovery == EconomyRefundRecoveryMode::Unspent )
                    continue; // 잔액을 읽은 뒤 그사이 쓰였다 — 다시 읽어 남은 만큼으로 짠다
                if ( staged != LedgerResult::Ok )
                    return toEconomyResult( staged );
            }
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
            Internal::ReceiptRecord mark;
            if ( Internal::readReceipt( connection, redeemKey, mark ) != ServiceStoreResult::Ok )
                return Internal::finish( outReply, EconomyServiceResult::fail( OnlineError::kUnavailable ) );
            if ( Internal::isRefunded( mark._state ) )
                return Internal::finish( outReply, EconomyResult::AlreadyRefunded );
            if ( mark._state != Internal::ReceiptState::Redeemed || mark._accountID != input._accountID )
                return Internal::finish( outReply, EconomyResult::NotPurchaser );
            LedgerJournalEntry       redeem;
            const ServiceStoreResult found = Ledger::findJournal( connection, redeemKey, redeem );
            if ( found == ServiceStoreResult::NotFound )
                return Internal::finish( outReply, EconomyResult::WithdrawalExpired ); // 분개 보존 기간은 철회 기한보다 훨씬 길다
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
            transaction.put( getReceiptTable(), redeemKey, Internal::encodeReceipt( Internal::ReceiptState::Withdrawn, input._accountID ), mark._version ); // 그사이 온 환불과 같은 레코드를 다툰다
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
