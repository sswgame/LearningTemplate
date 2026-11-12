#include "pch.h"

#include "GameFramework/Runtime/Online/Ledger/Server/Ledger.h"

#include "Core/Algorithm/CompareUtil.h"
#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Runtime/Online/Ledger/Server/LedgerItemInstance.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct LedgerInternal
        {
            static constexpr uint64      kJournalFormat       = 1;
            static constexpr uint64      kCarryFormat         = 1;
            static constexpr uint64      kTombstoneFormat     = 1;
            static constexpr int32       kListPageSize        = 64;
            static constexpr int32       kTimeHexWidth        = 16;
            static constexpr string_view kLegacyMintHolderKey = "sys/mint"; ///< 예전 판이 발행 다리에 넣던 내역 줄의 보유자 키
            static constexpr string_view kLegacySinkHolderKey = "sys/sink"; ///< 예전 판이 소각 다리에 넣던 내역 줄의 보유자 키

            /** @brief 한 (보유자, 자산)의 순변화입니다. */
            struct Delta
            {
                LedgerHolder    _holder{};
                string          _holderKey{};
                string          _assetID{};
                int64           _delta{ 0 };
                optional<int32> _firstDebitIndex;  ///< 이 보유자가 보낸 첫 다리(모자람 보고)
                optional<int32> _firstCreditIndex; ///< 이 보유자가 받은 첫 다리(상한 보고)
            };

            static LedgerResult finish( LedgerTransferResult& outResult, LedgerResult result )
            {
                outResult._result = result;
                return result;
            }

            static vector<uint8> encodeBalance( int64 amount )
            {
                BitWriter writer;
                writer.writeVarInt( amount );
                return writer.releaseBytes();
            }

            static uint64 computeContentHash( const LedgerTransferRequest& request )
            {
                BitWriter writer;
                ServiceKeyUtil::writeString( writer, request._reason );
                writer.writeBool( request._bAllowDebt == SW_TRUE );
                writer.writeVarUint( request._listPosting.size() );
                for ( const LedgerPosting& posting : request._listPosting )
                {
                    LedgerPosting::write( writer, posting );
                }
                const vector<uint8>& bytes = writer.getBytes();
                return HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size(), false );
            }

            static vector<uint8> encodeJournal( const LedgerTransferRequest& request, uint64 contentHash, const vector<LedgerTransferResult::HolderBalance>& listHolderBalance )
            {
                BitWriter writer;
                writer.writeVarUint( kJournalFormat );
                writer.writeVarInt( request._timeMs );
                writer.writeVarUint( static_cast<uint64>( request._actorKind ) );
                writer.writeVarUint( request._actorID );
                writer.writeUint32( static_cast<uint32>( contentHash >> 32 ) );
                writer.writeUint32( static_cast<uint32>( contentHash ) );
                ServiceKeyUtil::writeString( writer, request._reason );
                ServiceKeyUtil::writeString( writer, request._memo );
                writer.writeVarUint( request._listPosting.size() );
                for ( const LedgerPosting& posting : request._listPosting )
                {
                    LedgerPosting::write( writer, posting );
                }
                writer.writeVarUint( listHolderBalance.size() );
                for ( const LedgerTransferResult::HolderBalance& holderBalance : listHolderBalance )
                {
                    LedgerHolder::write( writer, holderBalance._holder );
                    ServiceKeyUtil::writeString( writer, holderBalance._balance._assetID );
                    writer.writeVarInt( holderBalance._balance._amount );
                }
                return writer.releaseBytes();
            }

            [[nodiscard]] static bool decodeJournal( const vector<uint8>& bytes, LedgerJournalEntry& outEntry, vector<LedgerTransferResult::HolderBalance>* pOutListHolderBalance )
            {
                BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                if ( reader.readVarUint() != kJournalFormat )
                    return false;
                outEntry._timeMs       = reader.readVarInt();
                const uint64 actorKind = reader.readVarUint();
                outEntry._actorID      = reader.readVarUint();
                const uint64 hashHigh  = reader.readUint32();
                const uint64 hashLow   = reader.readUint32();
                outEntry._contentHash  = ( hashHigh << 32 ) | hashLow;
                if ( actorKind >= static_cast<uint64>( LedgerActorKind::Count ) )
                    return false;
                outEntry._actorKind = static_cast<LedgerActorKind>( actorKind );
                if ( ServiceKeyUtil::readString( reader, LedgerConstant::kMaxReasonSize, outEntry._reason ) == false )
                    return false;
                if ( ServiceKeyUtil::readString( reader, LedgerConstant::kMaxStoredTextSize, outEntry._memo ) == false )
                    return false;
                const uint64 postingCount = reader.readVarUint();
                if ( postingCount > static_cast<uint64>( LedgerConstant::kMaxPostingCount ) )
                    return false;
                outEntry._listPosting.resize( static_cast<size_t>( postingCount ) );
                for ( LedgerPosting& posting : outEntry._listPosting )
                {
                    if ( LedgerPosting::read( reader, posting ) == false )
                        return false;
                }
                const uint64 balanceCount = reader.readVarUint();
                if ( balanceCount > static_cast<uint64>( LedgerConstant::kMaxPostingCount * 2 ) )
                    return false;
                if ( pOutListHolderBalance != nullptr )
                    pOutListHolderBalance->clear();
                for ( uint64 balanceIndex = 0; balanceIndex < balanceCount; ++balanceIndex )
                {
                    LedgerTransferResult::HolderBalance holderBalance;
                    if ( LedgerHolder::read( reader, holderBalance._holder ) == false )
                        return false;
                    if ( ServiceKeyUtil::readString( reader, LedgerConstant::kMaxAssetIDSize, holderBalance._balance._assetID ) == false )
                        return false;
                    holderBalance._balance._amount = reader.readVarInt();
                    if ( pOutListHolderBalance != nullptr )
                        pOutListHolderBalance->push_back( std::move( holderBalance ) );
                }
                return reader.hasOverflowed() == false;
            }

            /** @brief 분개 레코드에서 이동 시각을 읽습니다(분개 쓸기 규칙 용). 분개 전체를 읽지 못하면 false 라 지우지 않는다 — 이월에 넣지 못하는 분개다. */
            static bool readJournalTimeMs( const ServiceRecord& record, int64& outTimeMs )
            {
                LedgerJournalEntry entry;
                if ( decodeJournal( record._bytes, entry, nullptr ) == false )
                    return false;
                outTimeMs = entry._timeMs;
                return true;
            }

            static vector<uint8> encodeCarry( const LedgerSupplyStats& supply )
            {
                BitWriter writer;
                writer.writeVarUint( kCarryFormat );
                writer.writeVarInt( supply._issued );
                writer.writeVarInt( supply._burned );
                return writer.releaseBytes();
            }

            [[nodiscard]] static bool decodeCarry( const vector<uint8>& bytes, LedgerSupplyStats& inoutSupply )
            {
                BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                if ( reader.readVarUint() != kCarryFormat )
                    return false;
                const int64 issued = reader.readVarInt();
                const int64 burned = reader.readVarInt();
                if ( reader.hasOverflowed() )
                    return false;
                inoutSupply._issued = issued;
                inoutSupply._burned = burned;
                return true;
            }

            static const hashed_string& getJournalExpiryTable()
            {
                static const hashed_string s_table{ "ledger_journal_expiry" };
                return s_table;
            }

            /** @brief 지우는 분개 @p journalKey 의 내역 색인 줄(보유자마다 하나)을 지우는 쓰기를 붙입니다. 없는 줄을 지워도 된다(조건 없음). */
            static void stageHistoryErase( const LedgerJournalEntry& entry, string_view journalKey, ServiceTransaction& inoutTransaction )
            {
                LedgerTransferRequest request;
                request._listPosting = entry._listPosting;
                vector<Delta> listDelta;
                collectDeltas( request, listDelta ); // 넣을 때(`stageTransfer`)와 같은 보유자 키 · 같은 순서다
                const string* pLastHolderKey = nullptr;
                for ( const Delta& delta : listDelta )
                {
                    if ( pLastHolderKey != nullptr && *pLastHolderKey == delta._holderKey )
                        continue;
                    inoutTransaction.erase( Ledger::getHistoryTable(), makeHistoryKey( delta._holderKey, entry._timeMs, journalKey ) );
                    pLastHolderKey = &delta._holderKey;
                }
                // 예전 판은 발행 · 소각에도 내역 줄(`sys/mint/…` · `sys/sink/…`)을 넣었다 — 그 줄도 분개와 함께 지운다(없으면 조건 없는 지우기라 그냥 지나간다)
                bool bIssued = false;
                bool bBurned = false;
                for ( const LedgerPosting& posting : entry._listPosting )
                {
                    bIssued = bIssued || posting._from._kind == LedgerHolderKind::Mint;
                    bBurned = bBurned || posting._to._kind == LedgerHolderKind::Sink;
                }
                if ( bIssued )
                    inoutTransaction.erase( Ledger::getHistoryTable(), makeHistoryKey( string( kLegacyMintHolderKey ), entry._timeMs, journalKey ) );
                if ( bBurned )
                    inoutTransaction.erase( Ledger::getHistoryTable(), makeHistoryKey( string( kLegacySinkHolderKey ), entry._timeMs, journalKey ) );
            }

            /**
             * @brief 지우는 분개들의 공급 합을 자산별 이월 레코드에 더하고 내역 줄을 지우는 쓰기를 붙입니다(`ServiceTTLStageFn`). 이월 레코드는 읽은 버전을 조건으로 쓴다.
             * @details 분개 하나의 쓰기는 분개 · 색인 줄 2 + 이월(자산 ≤ 16) + 내역(보유자 ≤ 32)이라 쓰기 상한(64) 안이다.
             */
            static ServiceStoreResult stageJournalErase( IServiceStoreConnection& connection, const vector<const ServiceRecord*>& listErased, ServiceTransaction& inoutTransaction )
            {
                vector<LedgerSupplyStats> listSupply;
                for ( const ServiceRecord* pRecord : listErased )
                {
                    LedgerJournalEntry entry;
                    if ( decodeJournal( pRecord->_bytes, entry, nullptr ) == false )
                        return ServiceStoreResult::Invalid; // 시각을 읽은 분개라 여기 오지 않는다
                    Ledger::addSupply( entry, listSupply );
                    stageHistoryErase( entry, pRecord->_key, inoutTransaction );
                }
                for ( const LedgerSupplyStats& supply : listSupply )
                {
                    ServiceRecord            carryRecord;
                    const ServiceStoreResult read = connection.readRecord( Ledger::getCarryTable(), supply._assetID, carryRecord );
                    if ( read != ServiceStoreResult::Ok && read != ServiceStoreResult::NotFound )
                        return read;
                    LedgerSupplyStats total{ supply._assetID };
                    if ( read == ServiceStoreResult::Ok && decodeCarry( carryRecord._bytes, total ) == false )
                    {
                        SW_LOG_ERROR_ID( "Ledger.LedgerCarryUnreadable", "Ledger carry '%#' is unreadable", supply._assetID.c_str() );
                        return ServiceStoreResult::Invalid; // 깨진 이월에 더하면 합이 틀린다 - 지우지 않고 멈춘다
                    }
                    total._issued += supply._issued;
                    total._burned += supply._burned;
                    inoutTransaction.put( Ledger::getCarryTable(), supply._assetID, encodeCarry( total ), carryRecord._version );
                }
                return ServiceStoreResult::Ok;
            }

            static LedgerResult validate( const LedgerTransferRequest& request, optional<int32>& outFailedPostingIndex )
            {
                outFailedPostingIndex        = nullopt;
                const int32 postingCount     = static_cast<int32>( request._listPosting.size() );
                const bool  bCountOk         = 1 <= postingCount && postingCount <= LedgerConstant::kMaxPostingCount;
                const bool  bKeyOk           = LedgerUtil::isValidJournalKey( request._journalKey );
                const bool  bReasonOk        = LedgerUtil::isValidReasonCode( request._reason );
                const bool  bMemoOk          = request._memo.size() <= static_cast<size_t>( LedgerConstant::kMaxMemoSize );
                const bool  bInvalidTransfer = bCountOk == false || bKeyOk == false || bReasonOk == false || bMemoOk == false;
                if ( bInvalidTransfer )
                    return LedgerResult::Invalid;
                for ( int32 postingIndex = 0; postingIndex < postingCount; ++postingIndex )
                {
                    const LedgerPosting& posting         = request._listPosting[static_cast<size_t>( postingIndex )];
                    const bool           bHolderOk       = posting._from.isValid() && posting._to.isValid() && posting._from != posting._to;
                    const bool           bSystemOnly     = posting._from.isSystem() && posting._to.isSystem();
                    const bool           bInstance       = LedgerItemInstance::isInstanceAssetID( posting._assetID );
                    const bool           bAmountOk       = bInstance ? posting._amount == 1 : 1 <= posting._amount && posting._amount <= LedgerConstant::kMaxAmount; // 인스턴스 아이템은 하나씩 옮긴다
                    const bool           bInvalidPosting = bHolderOk == false || bSystemOnly || bAmountOk == false || LedgerUtil::isValidAssetID( posting._assetID ) == false;
                    if ( bInvalidPosting )
                    {
                        outFailedPostingIndex = postingIndex;
                        return LedgerResult::Invalid;
                    }
                }
                return LedgerResult::Ok;
            }

            static Delta& getOrCreateDelta( vector<Delta>& inoutListDelta, const LedgerHolder& holder, const string& assetID )
            {
                const string holderKey = holder.makeKey();
                for ( Delta& delta : inoutListDelta )
                {
                    if ( delta._holderKey == holderKey && delta._assetID == assetID )
                        return delta;
                }
                Delta& delta     = inoutListDelta.emplace_back();
                delta._holder    = holder;
                delta._holderKey = holderKey;
                delta._assetID   = assetID;
                return delta;
            }

            /** @brief 계정 · 맡김의 순변화를 (보유자 키, 자산) 순으로 모읍니다 — 읽기 · 쓰기 순서가 결정적이다. */
            static void collectDeltas( const LedgerTransferRequest& request, vector<Delta>& outListDelta )
            {
                outListDelta.clear();
                for ( size_t postingIndex = 0; postingIndex < request._listPosting.size(); ++postingIndex )
                {
                    const LedgerPosting& posting = request._listPosting[postingIndex];
                    if ( posting._from.isSystem() == false )
                    {
                        Delta& debit = getOrCreateDelta( outListDelta, posting._from, posting._assetID );
                        debit._delta -= posting._amount;
                        if ( debit._firstDebitIndex.has_value() == false )
                            debit._firstDebitIndex = static_cast<int32>( postingIndex );
                    }
                    if ( posting._to.isSystem() == false )
                    {
                        Delta& credit = getOrCreateDelta( outListDelta, posting._to, posting._assetID );
                        credit._delta += posting._amount;
                        if ( credit._firstCreditIndex.has_value() == false )
                            credit._firstCreditIndex = static_cast<int32>( postingIndex );
                    }
                }
                VectorUtil::sort( outListDelta, Ascending<&LedgerInternal::Delta::_holderKey, &LedgerInternal::Delta::_assetID>{} );
            }

            static int64 computeCap( const LedgerTransferRequest& request, const LedgerHolder& holder, string_view assetID )
            {
                if ( LedgerItemInstance::isInstanceAssetID( assetID ) )
                    return 1; // 인스턴스 아이템은 세상에 하나다. 어느 보유자도 둘을 가질 수 없다(같은 id 로 두 번 만들 수 없다)
                if ( holder._kind != LedgerHolderKind::Account || request._pPolicy == nullptr )
                    return LedgerConstant::kMaxBalance;
                const int64 policyCap = request._pPolicy->getBalanceCap( assetID );
                return 0 < policyCap && policyCap < LedgerConstant::kMaxBalance ? policyCap : LedgerConstant::kMaxBalance;
            }

            /** @brief 이동 뒤 잔액 @p nextAmount 가 허락되는가입니다 — 음수는 빚을 갚는 쪽(늘어남)이거나 빚 허용 요청의 계정만, 빚도 `kMaxBalance` 아래. */
            static bool isAmountAllowed( const LedgerTransferRequest& request, const Delta& delta, int64 nextAmount )
            {
                if ( nextAmount >= 0 || delta._delta > 0 )
                    return true;
                const bool bDebtAllowed = request._bAllowDebt == SW_TRUE && delta._holder._kind == LedgerHolderKind::Account && delta._delta < 0;
                return bDebtAllowed && -LedgerConstant::kMaxBalance <= nextAmount;
            }

            static string makeBalanceKey( const string& holderKey, string_view assetID )
            {
                string key{ holderKey };
                key.push_back( '/' );
                key += assetID;
                return key;
            }

            static string makeHistoryKey( const string& holderKey, int64 timeMs, string_view journalKey )
            {
                string key{ holderKey };
                key.push_back( '/' );
                ServiceKeyUtil::appendHex64( key, static_cast<uint64>( timeMs < 0 ? 0 : timeMs ) );
                key.push_back( '/' );
                key += journalKey;
                return key;
            }

            /** @brief 저장된 분개로 결과를 채웁니다(같은 내용이면 Ok + 다시 돌려줌, 다르면 JournalKeyReused). */
            static LedgerResult replayJournal( const vector<uint8>& bytes, const LedgerTransferRequest& request, LedgerTransferResult& outResult )
            {
                LedgerJournalEntry entry;
                if ( decodeJournal( bytes, entry, &outResult._listHolderBalance ) == false )
                {
                    SW_LOG_ERROR_ID( "Ledger.LedgerJournalUnreadable", "Ledger journal '%#' is unreadable", request._journalKey.c_str() );
                    return finish( outResult, LedgerResult::Unavailable );
                }
                if ( entry._contentHash != computeContentHash( request ) )
                {
                    outResult._listHolderBalance.clear();
                    return finish( outResult, LedgerResult::JournalKeyReused );
                }
                outResult._timeMs    = entry._timeMs;
                outResult._bReplayed = SW_TRUE;
                return finish( outResult, LedgerResult::Ok );
            }

            /** @brief 묘비를 읽을지입니다. 지운 계정의 잔액 소각만 건너뛴다. */
            enum class TombstoneRule : uint8
            {
                Refuse = 0, ///< 묘비가 선 계정의 다리는 `AccountDeleted`
                Skip        ///< 지운 계정의 소각(`burnDeletedAccounts`)
            };

            /** @brief 계정 보유자마다 묘비가 없는지 읽고, 없어야 함을 @p inoutTransaction 의 조건으로 겁니다. 묘비가 있으면 `AccountDeleted` 입니다. */
            [[nodiscard]] static LedgerResult stageAliveAccounts( IServiceStoreConnection& connection, const vector<Delta>& listDelta, ServiceTransaction& inoutTransaction )
            {
                const string* pLastHolderKey = nullptr;
                for ( const Delta& delta : listDelta )
                {
                    const bool bSkip = delta._holder._kind != LedgerHolderKind::Account || ( pLastHolderKey != nullptr && *pLastHolderKey == delta._holderKey );
                    if ( bSkip )
                        continue;
                    pLastHolderKey = &delta._holderKey;
                    ServiceRecord            tombstone;
                    const ServiceStoreResult read = connection.readRecord( Ledger::getTombstoneTable(), delta._holderKey, tombstone );
                    if ( read == ServiceStoreResult::Ok )
                        return LedgerResult::AccountDeleted;
                    if ( read != ServiceStoreResult::NotFound )
                        return LedgerResult::Unavailable;
                    inoutTransaction.requireVersion( Ledger::getTombstoneTable(), delta._holderKey, ServiceRecord::kAbsentVersion ); // 읽은 뒤 선 묘비에 잔액을 만들지 않는다
                }
                return LedgerResult::Ok;
            }

            /**
             * @brief 이동을 붙입니다(`Ledger::stageTransfer` 의 몸통). 계정 다리마다 묘비를 읽어 있으면 `AccountDeleted` 이고, 없어야 함을 커밋 조건으로 건다.
             * @details 지운 계정의 잔액을 소각하는 이동(`burnDeletedAccounts`)만 묘비를 건너뛴다 — 공개 함수로는 그 길을 열 수 없다.
             */
            [[nodiscard]] static LedgerResult stageTransfer( IServiceStoreConnection& connection, const LedgerTransferRequest& request, TombstoneRule tombstoneRule,
                                                             ServiceTransaction& inoutTransaction, LedgerTransferResult& outResult )
            {
                outResult                    = LedgerTransferResult{};
                outResult._journalKey        = request._journalKey;
                outResult._timeMs            = request._timeMs;
                const LedgerResult validated = validate( request, outResult._failedPostingIndex );
                if ( validated != LedgerResult::Ok )
                    return finish( outResult, validated );

                ServiceRecord            journalRecord;
                const ServiceStoreResult journalRead = connection.readRecord( Ledger::getJournalTable(), request._journalKey, journalRecord );
                if ( journalRead == ServiceStoreResult::Ok )
                    return replayJournal( journalRecord._bytes, request, outResult );
                if ( journalRead != ServiceStoreResult::NotFound )
                    return finish( outResult, LedgerResult::Unavailable );

                vector<Delta> listDelta;
                collectDeltas( request, listDelta );

                ServiceTransaction pending;
                if ( tombstoneRule == TombstoneRule::Refuse )
                {
                    const LedgerResult alive = stageAliveAccounts( connection, listDelta, pending );
                    if ( alive != LedgerResult::Ok )
                        return finish( outResult, alive );
                }
                for ( const Delta& delta : listDelta )
                {
                    const string             balanceKey = makeBalanceKey( delta._holderKey, delta._assetID );
                    ServiceRecord            balanceRecord;
                    const ServiceStoreResult balanceRead = connection.readRecord( Ledger::getBalanceTable(), balanceKey, balanceRecord );
                    if ( balanceRead != ServiceStoreResult::Ok && balanceRead != ServiceStoreResult::NotFound )
                        return finish( outResult, LedgerResult::Unavailable );
                    int64 currentAmount = 0;
                    if ( balanceRead == ServiceStoreResult::Ok && Ledger::decodeBalanceRecord( balanceRecord._bytes, currentAmount ) == false )
                    {
                        SW_LOG_ERROR_ID( "Ledger.LedgerBalanceUnreadable", "Ledger balance '%#' is unreadable", balanceKey.c_str() );
                        return finish( outResult, LedgerResult::Unavailable );
                    }
                    const int64 nextAmount = currentAmount + delta._delta;
                    if ( isAmountAllowed( request, delta, nextAmount ) == false )
                    {
                        outResult._failedPostingIndex = delta._firstDebitIndex;
                        return finish( outResult, LedgerResult::InsufficientFunds );
                    }
                    const bool bIncrease = delta._delta > 0;
                    if ( bIncrease && nextAmount > computeCap( request, delta._holder, delta._assetID ) )
                    {
                        outResult._failedPostingIndex = delta._firstCreditIndex;
                        return finish( outResult, LedgerResult::CapExceeded );
                    }
                    if ( delta._delta != 0 )
                    {
                        if ( nextAmount == 0 )
                            pending.erase( Ledger::getBalanceTable(), balanceKey, balanceRecord._version );
                        else
                            pending.put( Ledger::getBalanceTable(), balanceKey, encodeBalance( nextAmount ), balanceRecord._version );
                    }
                    LedgerTransferResult::HolderBalance& holderBalance = outResult._listHolderBalance.emplace_back();
                    holderBalance._holder                              = delta._holder;
                    holderBalance._balance._assetID                    = delta._assetID;
                    holderBalance._balance._amount                     = nextAmount;
                    holderBalance._balance._version                    = balanceRecord._version;
                }

                const uint64 contentHash = computeContentHash( request );
                pending.put( Ledger::getJournalTable(), request._journalKey, encodeJournal( request, contentHash, outResult._listHolderBalance ),
                             ServiceRecord::kAbsentVersion );
                // 소각 분개(`burnAccount`)도 이 길을 지나 같은 TTL 색인 줄을 넣는다 - 기간 뒤 분개 쓸기가 이월 · 내역과 함께 지운다
                pending.put( getJournalExpiryTable(), ServiceTTLRule::makeTTLIndexKey( request._timeMs, request._journalKey ), vector<uint8>{}, ServiceRecord::kAnyVersion );
                const string* pLastHolderKey = nullptr;
                for ( const Delta& delta : listDelta )
                {
                    const bool bSameHolder = pLastHolderKey != nullptr && *pLastHolderKey == delta._holderKey;
                    if ( bSameHolder )
                        continue;
                    pending.put( Ledger::getHistoryTable(), makeHistoryKey( delta._holderKey, request._timeMs, request._journalKey ), vector<uint8>{},
                                 ServiceRecord::kAbsentVersion );
                    pLastHolderKey = &delta._holderKey;
                }

                const size_t totalWriteCount = inoutTransaction.getWrites().size() + pending.getWrites().size();
                if ( totalWriteCount > static_cast<size_t>( ServiceTransaction::kMaxWriteCount ) )
                {
                    outResult._listHolderBalance.clear();
                    return finish( outResult, LedgerResult::Invalid );
                }
                Ledger::appendWrites( pending, inoutTransaction );
                return finish( outResult, LedgerResult::Ok );
            }

            /** @brief 묘비 · 소각 대기 키 `acct/<16 진>` 에서 계정 id 를 읽습니다. */
            [[nodiscard]] static bool parseAccountKey( string_view key, uint64& outAccountID )
            {
                constexpr string_view kPrefix = "acct/";
                return StringUtil::startsWith( key, kPrefix ) && ServiceKeyUtil::parseHex64( key.substr( kPrefix.size() ), outAccountID ) && outAccountID != 0;
            }

            /**
             * @brief 지운 계정 하나의 잔액을 모두 소각하고 소각 대기에서 뺍니다. 잔액은 Sink 로, 빚은 Mint 에서 메워 0 으로 만든다(대손 상각).
             * @return 다 소각했으면 Ok, 충돌이 이어지면 Conflict(다음 회차), 분개 키가 이미 다른 소각에 쓰였으면 Invalid, 저장소 문제면 그 결과입니다.
             */
            [[nodiscard]] static ServiceStoreResult burnAccount( IServiceStoreConnection& connection, uint64 accountID, int64 timeMs )
            {
                const LedgerHolder account       = LedgerHolder::makeAccount( accountID );
                int32              conflictCount = 0;
                while ( true )
                {
                    vector<LedgerBalance>    listBalance;
                    const ServiceStoreResult listed = Ledger::listBalances( connection, account, listBalance );
                    if ( listed != ServiceStoreResult::Ok )
                        return listed;
                    if ( listBalance.empty() )
                    {
                        ServiceTransaction done;
                        done.erase( Ledger::getDeletionQueueTable(), account.makeKey() );
                        return connection.commit( done );
                    }
                    LedgerTransferRequest request;
                    request._journalKey = LedgerJournalKey::makeAccountScope( accountID );
                    request._journalKey += "/delete.";
                    request._journalKey += listBalance.front()._assetID;
                    request._reason    = "account.delete";
                    request._timeMs    = timeMs;
                    request._actorKind = LedgerActorKind::System;
                    ServiceTransaction transaction;
                    const size_t       burnCount = std::min( listBalance.size(), static_cast<size_t>( LedgerConstant::kMaxPostingCount ) );
                    for ( size_t balanceIndex = 0; balanceIndex < burnCount; ++balanceIndex )
                    {
                        const LedgerBalance& balance = listBalance[balanceIndex];
                        if ( balance._amount > 0 )
                            request._listPosting.push_back( LedgerPosting{ account, LedgerHolder::makeSink(), balance._assetID, balance._amount } );
                        else
                            request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), account, balance._assetID, -balance._amount } ); // 빚은 메워 없앤다 — 거둘 사람이 없다
                        if ( LedgerItemInstance::isInstanceAssetID( balance._assetID ) )
                            transaction.erase( LedgerItemInstance::getStateTable(), balance._assetID ); // 소각한 아이템의 상태도 함께 지운다(`stageDestroy` 와 같다)
                    }
                    LedgerTransferResult result;
                    const LedgerResult   staged = stageTransfer( connection, request, TombstoneRule::Skip, transaction, result );
                    if ( staged == LedgerResult::Unavailable )
                        return ServiceStoreResult::Unavailable;
                    if ( staged != LedgerResult::Ok || result._bReplayed == SW_TRUE )
                    {
                        SW_LOG_ERROR_ID( "Ledger.AccountBurnRefused", "Burning the balances of deleted account %# was refused (journal '%#') - the account stays in the deletion queue", accountID,
                                         request._journalKey.c_str() );
                        return ServiceStoreResult::Invalid;
                    }
                    const ServiceStoreResult committed = connection.commit( transaction );
                    if ( committed == ServiceStoreResult::Ok )
                    {
                        conflictCount = 0;
                        continue;
                    }
                    if ( committed != ServiceStoreResult::Conflict )
                        return committed; // 늦게 닿은 커밋이면 다음 회차가 다시 읽어 남은 것만 소각한다
                    if ( ++conflictCount >= LedgerConstant::kMaxRetryCount )
                        return ServiceStoreResult::Conflict;
                }
            }

            static void applyCommitVersion( LedgerTransferResult& inoutResult, uint64 commitVersion )
            {
                for ( LedgerTransferResult::HolderBalance& holderBalance : inoutResult._listHolderBalance )
                {
                    holderBalance._balance._version = holderBalance._balance._amount == 0 ? 0 : commitVersion;
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const hashed_string& Ledger::getBalanceTable()
    {
        static const hashed_string s_table{ "ledger_balance" };
        return s_table;
    }

    const hashed_string& Ledger::getJournalTable()
    {
        static const hashed_string s_table{ "ledger_journal" };
        return s_table;
    }

    const hashed_string& Ledger::getCarryTable()
    {
        static const hashed_string s_table{ "ledger_carry" };
        return s_table;
    }

    const hashed_string& Ledger::getTombstoneTable()
    {
        static const hashed_string s_table{ "ledger_tombstone" };
        return s_table;
    }

    const hashed_string& Ledger::getDeletionQueueTable()
    {
        static const hashed_string s_table{ "ledger_deletion_queue" };
        return s_table;
    }

    void Ledger::stageAccountDeletion( uint64 accountID, int64 timeMs, ServiceTransaction& inoutTransaction )
    {
        const string key = LedgerHolder::makeAccount( accountID ).makeKey();
        BitWriter    writer;
        writer.writeVarUint( LedgerInternal::kTombstoneFormat );
        writer.writeVarInt( timeMs );
        inoutTransaction.put( getTombstoneTable(), key, writer.releaseBytes() );
        inoutTransaction.put( getDeletionQueueTable(), key, vector<uint8>{} );
    }

    ServiceStoreResult Ledger::burnDeletedAccounts( IServiceStoreConnection& connection, int64 timeMs, int32 maxCount, int32& outBurnedCount )
    {
        outBurnedCount = 0;
        if ( maxCount <= 0 )
            return ServiceStoreResult::Ok;
        vector<ServiceRecord>    listQueued;
        const ServiceStoreResult listed = connection.listRecords( getDeletionQueueTable(), "", "", maxCount, false, listQueued );
        if ( listed != ServiceStoreResult::Ok )
            return listed;
        for ( const ServiceRecord& queued : listQueued )
        {
            uint64 accountID = 0;
            if ( LedgerInternal::parseAccountKey( queued._key, accountID ) == false )
            {
                SW_LOG_WARNING_ID( "Ledger.DeletionQueueKeyMalformed", "Ledger deletion queue key '%#' is malformed", queued._key.c_str() );
                continue;
            }
            const ServiceStoreResult burned = LedgerInternal::burnAccount( connection, accountID, timeMs );
            if ( burned == ServiceStoreResult::Unavailable )
                return burned;
            outBurnedCount += burned == ServiceStoreResult::Ok ? 1 : 0; // 충돌 · 거절은 대기에 남아 다음 회차가 다시 본다
        }
        return ServiceStoreResult::Ok;
    }

    ServiceTTLRule Ledger::makeJournalTTLRule( int64 retentionMs )
    {
        ServiceTTLRule rule;
        rule._table           = getJournalTable();
        rule._ttlIndexTable   = LedgerInternal::getJournalExpiryTable();
        rule._expireAfterMs   = retentionMs;
        rule._pfnReadTime     = &LedgerInternal::readJournalTimeMs;
        rule._pfnStageErase   = &LedgerInternal::stageJournalErase;
        rule._pBackfillSource = ServiceTTLRule::makeRecordBackfillSource<&LedgerInternal::readJournalTimeMs>();
        rule._backfillVersion = 1;
        return rule;
    }

    void Ledger::addSupply( const LedgerJournalEntry& entry, vector<LedgerSupplyStats>& inoutListSupply )
    {
        for ( const LedgerPosting& posting : entry._listPosting )
        {
            const bool bIssued = posting._from._kind == LedgerHolderKind::Mint;
            const bool bBurned = posting._to._kind == LedgerHolderKind::Sink;
            if ( bIssued == false && bBurned == false )
                continue;
            LedgerSupplyStats* pSupply = VectorUtil::findBy<&LedgerSupplyStats::_assetID>( inoutListSupply, posting._assetID );
            if ( pSupply == nullptr )
                pSupply = &inoutListSupply.emplace_back( LedgerSupplyStats{ posting._assetID } );
            if ( bIssued )
                pSupply->_issued += posting._amount;
            if ( bBurned )
                pSupply->_burned += posting._amount;
        }
    }

    ServiceStoreResult Ledger::addCarriedSupply( IServiceStoreConnection& connection, vector<LedgerSupplyStats>& inoutListSupply, int32& outUnreadableCount )
    {
        outUnreadableCount = 0;
        string cursor;
        while ( true )
        {
            vector<ServiceRecord>    listRecord;
            const ServiceStoreResult listed = connection.listRecords( getCarryTable(), "", cursor, LedgerInternal::kListPageSize, false, listRecord );
            if ( listed != ServiceStoreResult::Ok )
                return listed;
            for ( const ServiceRecord& record : listRecord )
            {
                LedgerSupplyStats carried{ record._key };
                if ( LedgerInternal::decodeCarry( record._bytes, carried ) == false )
                {
                    ++outUnreadableCount;
                    continue;
                }
                LedgerSupplyStats* pSupply = VectorUtil::findBy<&LedgerSupplyStats::_assetID>( inoutListSupply, carried._assetID );
                if ( pSupply == nullptr )
                {
                    inoutListSupply.push_back( std::move( carried ) );
                    continue;
                }
                pSupply->_issued += carried._issued;
                pSupply->_burned += carried._burned;
            }
            if ( static_cast<int32>( listRecord.size() ) < LedgerInternal::kListPageSize )
                return ServiceStoreResult::Ok;
            cursor = listRecord.back()._key;
        }
    }

    const hashed_string& Ledger::getHistoryTable()
    {
        static const hashed_string s_table{ "ledger_history" };
        return s_table;
    }

    LedgerResult Ledger::stageTransfer( IServiceStoreConnection& connection, const LedgerTransferRequest& request, ServiceTransaction& inoutTransaction,
                                        LedgerTransferResult& outResult )
    {
        return LedgerInternal::stageTransfer( connection, request, LedgerInternal::TombstoneRule::Refuse, inoutTransaction, outResult );
    }

    LedgerResult Ledger::resolveConflict( IServiceStoreConnection& connection, const LedgerTransferRequest& request, LedgerTransferResult& outResult )
    {
        ServiceRecord            journalRecord;
        const ServiceStoreResult journalRead = connection.readRecord( getJournalTable(), request._journalKey, journalRecord );
        if ( journalRead == ServiceStoreResult::Ok )
        {
            outResult._listHolderBalance.clear();
            return LedgerInternal::replayJournal( journalRecord._bytes, request, outResult );
        }
        if ( journalRead == ServiceStoreResult::NotFound )
            return LedgerInternal::finish( outResult, LedgerResult::Conflict );
        return LedgerInternal::finish( outResult, LedgerResult::Unavailable );
    }

    LedgerResult Ledger::executeTransfer( IServiceStoreConnection& connection, const LedgerTransferRequest& request, LedgerTransferResult& outResult )
    {
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            ServiceTransaction transaction;
            const LedgerResult staged = stageTransfer( connection, request, transaction, outResult );
            if ( staged != LedgerResult::Ok || outResult._bReplayed == SW_TRUE )
                return staged;
            ServiceCommitInfo        info;
            const ServiceStoreResult committed = connection.commit( transaction, &info );
            if ( committed == ServiceStoreResult::Ok )
            {
                LedgerInternal::applyCommitVersion( outResult, info._commitVersion );
                return LedgerResult::Ok;
            }
            if ( committed == ServiceStoreResult::Invalid )
                return LedgerInternal::finish( outResult, LedgerResult::Invalid );
            const LedgerResult resolved = resolveConflict( connection, request, outResult );
            if ( resolved != LedgerResult::Conflict )
                return resolved;
            if ( committed == ServiceStoreResult::Unavailable )
                return LedgerInternal::finish( outResult, LedgerResult::Unavailable ); // 분개가 없어도 커밋이 늦게 닿을 수 있다 — 같은 키로 나중에
        }
        return LedgerInternal::finish( outResult, LedgerResult::Conflict );
    }

    ServiceStoreResult Ledger::listBalances( IServiceStoreConnection& connection, const LedgerHolder& holder, vector<LedgerBalance>& outListBalance )
    {
        if ( holder.isValid() == false || holder.isSystem() )
            return ServiceStoreResult::Invalid;
        string prefix = holder.makeKey();
        prefix.push_back( '/' );
        string cursor;
        while ( true )
        {
            vector<ServiceRecord>    listRecord;
            const ServiceStoreResult listed = connection.listRecords( getBalanceTable(), prefix, cursor, LedgerInternal::kListPageSize, false, listRecord );
            if ( listed != ServiceStoreResult::Ok )
                return listed;
            for ( ServiceRecord& record : listRecord )
            {
                LedgerBalance& balance = outListBalance.emplace_back();
                balance._assetID       = record._key.substr( prefix.size() );
                balance._version       = record._version;
                if ( decodeBalanceRecord( record._bytes, balance._amount ) == false )
                {
                    SW_LOG_ERROR_ID( "Ledger.LedgerBalanceUnreadable", "Ledger balance '%#' is unreadable", record._key.c_str() );
                    return ServiceStoreResult::Unavailable;
                }
            }
            if ( static_cast<int32>( listRecord.size() ) < LedgerInternal::kListPageSize )
                return ServiceStoreResult::Ok;
            cursor = listRecord.back()._key;
        }
    }

    ServiceStoreResult Ledger::readBalance( IServiceStoreConnection& connection, const LedgerHolder& holder, string_view assetID, LedgerBalance& outBalance )
    {
        outBalance                      = LedgerBalance{};
        outBalance._assetID             = string( assetID );
        const bool bInvalidBalanceQuery = holder.isValid() == false || holder.isSystem() || LedgerUtil::isValidAssetID( assetID ) == false;
        if ( bInvalidBalanceQuery )
            return ServiceStoreResult::Invalid;
        ServiceRecord            record;
        const ServiceStoreResult read = connection.readRecord( getBalanceTable(), LedgerInternal::makeBalanceKey( holder.makeKey(), assetID ), record );
        if ( read == ServiceStoreResult::NotFound )
            return ServiceStoreResult::Ok;
        if ( read != ServiceStoreResult::Ok )
            return read;
        outBalance._version = record._version;
        return decodeBalanceRecord( record._bytes, outBalance._amount ) ? ServiceStoreResult::Ok : ServiceStoreResult::Unavailable;
    }

    ServiceStoreResult Ledger::listHistory( IServiceStoreConnection& connection, const LedgerHolder& holder, string_view cursor, int32 maxCount,
                                            vector<LedgerJournalEntry>& outListEntry, string& outNextCursor )
    {
        outNextCursor.clear();
        const bool bInvalidHistoryQuery = holder.isValid() == false || holder.isSystem() || maxCount <= 0;
        if ( bInvalidHistoryQuery )
            return ServiceStoreResult::Invalid;
        string prefix = holder.makeKey();
        prefix.push_back( '/' );
        vector<ServiceRecord>    listRecord;
        const ServiceStoreResult listed = connection.listRecords( getHistoryTable(), prefix, cursor, maxCount, true, listRecord );
        if ( listed != ServiceStoreResult::Ok )
            return listed;
        const size_t journalOffset = prefix.size() + static_cast<size_t>( LedgerInternal::kTimeHexWidth ) + 1;
        for ( const ServiceRecord& record : listRecord )
        {
            if ( record._key.size() <= journalOffset )
                continue;
            LedgerJournalEntry&      entry = outListEntry.emplace_back();
            const ServiceStoreResult found = findJournal( connection, string_view( record._key ).substr( journalOffset ), entry );
            if ( found != ServiceStoreResult::Ok )
            {
                outListEntry.pop_back();
                if ( found != ServiceStoreResult::NotFound )
                    return found;
                SW_LOG_WARNING_ID( "Ledger.LedgerHistoryPointsMissing", "Ledger history '%#' points at a missing journal", record._key.c_str() );
            }
        }
        if ( static_cast<int32>( listRecord.size() ) == maxCount )
            outNextCursor = listRecord.back()._key;
        return ServiceStoreResult::Ok;
    }

    ServiceStoreResult Ledger::findJournal( IServiceStoreConnection& connection, string_view journalKey, LedgerJournalEntry& outEntry )
    {
        outEntry = LedgerJournalEntry{};
        if ( LedgerUtil::isValidJournalKey( journalKey ) == false )
            return ServiceStoreResult::Invalid;
        ServiceRecord            record;
        const ServiceStoreResult read = connection.readRecord( getJournalTable(), journalKey, record );
        if ( read != ServiceStoreResult::Ok )
            return read;
        outEntry._journalKey = string( journalKey );
        return LedgerInternal::decodeJournal( record._bytes, outEntry, nullptr ) ? ServiceStoreResult::Ok : ServiceStoreResult::Unavailable;
    }

    void Ledger::appendWrites( const ServiceTransaction& source, ServiceTransaction& inoutTarget )
    {
        for ( const ServiceWrite& write : source.getWrites() )
        {
            switch ( write._kind )
            {
                case ServiceWrite::Kind::Put:
                {
                    inoutTarget.put( write._table, write._key, write._bytes, write._expectedVersion );
                    break;
                }
                case ServiceWrite::Kind::Erase:
                {
                    inoutTarget.erase( write._table, write._key, write._expectedVersion );
                    break;
                }
                case ServiceWrite::Kind::Require:
                {
                    inoutTarget.requireVersion( write._table, write._key, write._expectedVersion );
                    break;
                }
            }
        }
    }

    bool Ledger::decodeBalanceRecord( const vector<uint8>& bytes, int64& outAmount )
    {
        BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        outAmount = reader.readVarInt();
        return reader.hasOverflowed() == false;
    }
} // namespace sw
