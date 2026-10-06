#include "pch.h"

#include "GameFramework/Base/Online/Ledger/Ledger.h"

#include "Core/Network/BitStream.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "Ledger" );

    namespace
    {
        struct LedgerInternal
        {
            static constexpr uint64 kJournalFormat     = 1;
            static constexpr int32  kMaxStoredTextSize = 512;
            static constexpr int32  kListPageSize      = 64;
            static constexpr int32  kTimeHexWidth      = 16;

            /** @brief 한 (보유자, 자산)의 순변화입니다. */
            struct Delta
            {
                LedgerHolder _holder{};
                string       _holderKey{};
                string       _assetId{};
                int64        _delta{ 0 };
                int32        _firstDebitIndex{ -1 };  ///< 이 보유자가 보낸 첫 다리(모자람 보고)
                int32        _firstCreditIndex{ -1 }; ///< 이 보유자가 받은 첫 다리(상한 보고)
            };

            static bool isDeltaLess( const Delta& left, const Delta& right )
            {
                if ( left._holderKey != right._holderKey )
                    return left._holderKey < right._holderKey;
                return left._assetId < right._assetId;
            }

            static LedgerResult finish( LedgerTransferOutcome& outOutcome, LedgerResult result )
            {
                outOutcome._result = result;
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
                    LedgerUtil::writePosting( writer, posting );
                const vector<uint8>& bytes = writer.getBytes();
                return StringUtil::computeHash64( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size(), false );
            }

            static vector<uint8> encodeJournal( const LedgerTransferRequest& request, uint64 contentHash, const vector<LedgerTransferOutcome::HolderBalance>& listHolderBalance )
            {
                BitWriter writer;
                writer.writeVarUint( kJournalFormat );
                writer.writeVarInt( request._timeMs );
                writer.writeVarUint( static_cast<uint64>( request._actorKind ) );
                writer.writeVarUint( request._actorId );
                writer.writeUint32( static_cast<uint32>( contentHash >> 32 ) );
                writer.writeUint32( static_cast<uint32>( contentHash ) );
                ServiceKeyUtil::writeString( writer, request._reason );
                ServiceKeyUtil::writeString( writer, request._memo );
                writer.writeVarUint( request._listPosting.size() );
                for ( const LedgerPosting& posting : request._listPosting )
                    LedgerUtil::writePosting( writer, posting );
                writer.writeVarUint( listHolderBalance.size() );
                for ( const LedgerTransferOutcome::HolderBalance& holderBalance : listHolderBalance )
                {
                    LedgerUtil::writeHolder( writer, holderBalance._holder );
                    ServiceKeyUtil::writeString( writer, holderBalance._balance._assetId );
                    writer.writeVarInt( holderBalance._balance._amount );
                }
                return writer.releaseBytes();
            }

            [[nodiscard]] static bool decodeJournal( const vector<uint8>& bytes, LedgerJournalEntry& outEntry, vector<LedgerTransferOutcome::HolderBalance>* pOutListHolderBalance )
            {
                BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                if ( reader.readVarUint() != kJournalFormat )
                    return false;
                outEntry._timeMs       = reader.readVarInt();
                const uint64 actorKind = reader.readVarUint();
                outEntry._actorId      = reader.readVarUint();
                const uint64 hashHigh  = reader.readUint32();
                const uint64 hashLow   = reader.readUint32();
                outEntry._contentHash  = ( hashHigh << 32 ) | hashLow;
                if ( actorKind >= static_cast<uint64>( LedgerActorKind::Count ) )
                    return false;
                outEntry._actorKind = static_cast<LedgerActorKind>( actorKind );
                if ( ServiceKeyUtil::readString( reader, LedgerConstant::kMaxReasonSize, outEntry._reason ) == false )
                    return false;
                if ( ServiceKeyUtil::readString( reader, kMaxStoredTextSize, outEntry._memo ) == false )
                    return false;
                const uint64 postingCount = reader.readVarUint();
                if ( postingCount > static_cast<uint64>( LedgerConstant::kMaxPostingCount ) )
                    return false;
                outEntry._listPosting.resize( static_cast<size_t>( postingCount ) );
                for ( LedgerPosting& posting : outEntry._listPosting )
                {
                    if ( LedgerUtil::readPosting( reader, posting ) == false )
                        return false;
                }
                const uint64 balanceCount = reader.readVarUint();
                if ( balanceCount > static_cast<uint64>( LedgerConstant::kMaxPostingCount * 2 ) )
                    return false;
                if ( pOutListHolderBalance != nullptr )
                    pOutListHolderBalance->clear();
                for ( uint64 balanceIndex = 0; balanceIndex < balanceCount; ++balanceIndex )
                {
                    LedgerTransferOutcome::HolderBalance holderBalance;
                    if ( LedgerUtil::readHolder( reader, holderBalance._holder ) == false )
                        return false;
                    if ( ServiceKeyUtil::readString( reader, LedgerConstant::kMaxAssetIdSize, holderBalance._balance._assetId ) == false )
                        return false;
                    holderBalance._balance._amount = reader.readVarInt();
                    if ( pOutListHolderBalance != nullptr )
                        pOutListHolderBalance->push_back( std::move( holderBalance ) );
                }
                return reader.hasOverflowed() == false;
            }

            static LedgerResult validate( const LedgerTransferRequest& request, int32& outFailedPostingIndex )
            {
                outFailedPostingIndex    = -1;
                const int32 postingCount = static_cast<int32>( request._listPosting.size() );
                const bool  bCountOk     = 1 <= postingCount && postingCount <= LedgerConstant::kMaxPostingCount;
                const bool  bKeyOk       = LedgerUtil::isValidJournalKey( request._journalKey );
                const bool  bReasonOk    = LedgerUtil::isValidReasonCode( request._reason );
                const bool  bMemoOk      = request._memo.size() <= static_cast<size_t>( LedgerConstant::kMaxMemoSize );
                if ( bCountOk == false || bKeyOk == false || bReasonOk == false || bMemoOk == false )
                    return LedgerResult::Invalid;
                for ( int32 postingIndex = 0; postingIndex < postingCount; ++postingIndex )
                {
                    const LedgerPosting& posting     = request._listPosting[static_cast<size_t>( postingIndex )];
                    const bool           bHolderOk   = posting._from.isValid() && posting._to.isValid() && posting._from != posting._to;
                    const bool           bSystemOnly = posting._from.isSystem() && posting._to.isSystem();
                    const bool           bAmountOk   = 1 <= posting._amount && posting._amount <= LedgerConstant::kMaxAmount;
                    if ( bHolderOk == false || bSystemOnly || bAmountOk == false || LedgerUtil::isValidAssetId( posting._assetId ) == false )
                    {
                        outFailedPostingIndex = postingIndex;
                        return LedgerResult::Invalid;
                    }
                }
                return LedgerResult::Ok;
            }

            static Delta& findOrAddDelta( vector<Delta>& inoutListDelta, const LedgerHolder& holder, const string& assetId )
            {
                const string holderKey = holder.makeKey();
                for ( Delta& delta : inoutListDelta )
                {
                    if ( delta._holderKey == holderKey && delta._assetId == assetId )
                        return delta;
                }
                Delta& delta     = inoutListDelta.emplace_back();
                delta._holder    = holder;
                delta._holderKey = holderKey;
                delta._assetId   = assetId;
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
                        Delta& debit = findOrAddDelta( outListDelta, posting._from, posting._assetId );
                        debit._delta -= posting._amount;
                        if ( debit._firstDebitIndex < 0 )
                            debit._firstDebitIndex = static_cast<int32>( postingIndex );
                    }
                    if ( posting._to.isSystem() == false )
                    {
                        Delta& credit = findOrAddDelta( outListDelta, posting._to, posting._assetId );
                        credit._delta += posting._amount;
                        if ( credit._firstCreditIndex < 0 )
                            credit._firstCreditIndex = static_cast<int32>( postingIndex );
                    }
                }
                std::sort( outListDelta.begin(), outListDelta.end(), &LedgerInternal::isDeltaLess );
            }

            static int64 computeCap( const LedgerTransferRequest& request, const LedgerHolder& holder, string_view assetId )
            {
                if ( holder._kind != LedgerHolderKind::Account || request._pPolicy == nullptr )
                    return LedgerConstant::kMaxBalance;
                const int64 policyCap = request._pPolicy->getBalanceCap( assetId );
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

            static string makeBalanceKey( const string& holderKey, string_view assetId )
            {
                string key{ holderKey };
                key.push_back( '/' );
                key += assetId;
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
            static LedgerResult replayJournal( const vector<uint8>& bytes, const LedgerTransferRequest& request, LedgerTransferOutcome& outOutcome )
            {
                LedgerJournalEntry entry;
                if ( decodeJournal( bytes, entry, &outOutcome._listHolderBalance ) == false )
                {
                    SW_LOG_ERROR( "Ledger journal '%#' is unreadable", request._journalKey.c_str() );
                    return finish( outOutcome, LedgerResult::Unavailable );
                }
                if ( entry._contentHash != computeContentHash( request ) )
                {
                    outOutcome._listHolderBalance.clear();
                    return finish( outOutcome, LedgerResult::JournalKeyReused );
                }
                outOutcome._timeMs    = entry._timeMs;
                outOutcome._bReplayed = SW_TRUE;
                return finish( outOutcome, LedgerResult::Ok );
            }

            static void applyCommitVersion( LedgerTransferOutcome& inoutOutcome, uint64 commitVersion )
            {
                for ( LedgerTransferOutcome::HolderBalance& holderBalance : inoutOutcome._listHolderBalance )
                    holderBalance._balance._version = holderBalance._balance._amount == 0 ? 0 : commitVersion;
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

    const hashed_string& Ledger::getHistoryTable()
    {
        static const hashed_string s_table{ "ledger_history" };
        return s_table;
    }

    LedgerResult Ledger::stageTransfer( IServiceStoreConnection& connection, const LedgerTransferRequest& request, ServiceTransaction& inoutTransaction,
                                        LedgerTransferOutcome& outOutcome )
    {
        outOutcome                   = LedgerTransferOutcome{};
        outOutcome._journalKey       = request._journalKey;
        outOutcome._timeMs           = request._timeMs;
        const LedgerResult validated = LedgerInternal::validate( request, outOutcome._failedPostingIndex );
        if ( validated != LedgerResult::Ok )
            return LedgerInternal::finish( outOutcome, validated );

        ServiceRecord            journalRecord;
        const ServiceStoreResult journalRead = connection.readRecord( getJournalTable(), request._journalKey, journalRecord );
        if ( journalRead == ServiceStoreResult::Ok )
            return LedgerInternal::replayJournal( journalRecord._bytes, request, outOutcome );
        if ( journalRead != ServiceStoreResult::NotFound )
            return LedgerInternal::finish( outOutcome, LedgerResult::Unavailable );

        vector<LedgerInternal::Delta> listDelta;
        LedgerInternal::collectDeltas( request, listDelta );

        ServiceTransaction pending;
        for ( const LedgerInternal::Delta& delta : listDelta )
        {
            const string             balanceKey = LedgerInternal::makeBalanceKey( delta._holderKey, delta._assetId );
            ServiceRecord            balanceRecord;
            const ServiceStoreResult balanceRead = connection.readRecord( getBalanceTable(), balanceKey, balanceRecord );
            if ( balanceRead != ServiceStoreResult::Ok && balanceRead != ServiceStoreResult::NotFound )
                return LedgerInternal::finish( outOutcome, LedgerResult::Unavailable );
            int64 currentAmount = 0;
            if ( balanceRead == ServiceStoreResult::Ok && decodeBalanceRecord( balanceRecord._bytes, currentAmount ) == false )
            {
                SW_LOG_ERROR( "Ledger balance '%#' is unreadable", balanceKey.c_str() );
                return LedgerInternal::finish( outOutcome, LedgerResult::Unavailable );
            }
            const int64 nextAmount = currentAmount + delta._delta;
            if ( LedgerInternal::isAmountAllowed( request, delta, nextAmount ) == false )
            {
                outOutcome._failedPostingIndex = delta._firstDebitIndex;
                return LedgerInternal::finish( outOutcome, LedgerResult::InsufficientFunds );
            }
            const bool bIncrease = delta._delta > 0;
            if ( bIncrease && nextAmount > LedgerInternal::computeCap( request, delta._holder, delta._assetId ) )
            {
                outOutcome._failedPostingIndex = delta._firstCreditIndex;
                return LedgerInternal::finish( outOutcome, LedgerResult::CapExceeded );
            }
            if ( delta._delta != 0 )
            {
                if ( nextAmount == 0 )
                    pending.erase( getBalanceTable(), balanceKey, balanceRecord._version );
                else
                    pending.put( getBalanceTable(), balanceKey, LedgerInternal::encodeBalance( nextAmount ), balanceRecord._version );
            }
            LedgerTransferOutcome::HolderBalance& holderBalance = outOutcome._listHolderBalance.emplace_back();
            holderBalance._holder                               = delta._holder;
            holderBalance._balance._assetId                     = delta._assetId;
            holderBalance._balance._amount                      = nextAmount;
            holderBalance._balance._version                     = balanceRecord._version;
        }

        const uint64 contentHash = LedgerInternal::computeContentHash( request );
        pending.put( getJournalTable(), request._journalKey, LedgerInternal::encodeJournal( request, contentHash, outOutcome._listHolderBalance ),
                     ServiceRecord::kAbsentVersion );
        const string* pLastHolderKey = nullptr;
        for ( const LedgerInternal::Delta& delta : listDelta )
        {
            const bool bSameHolder = pLastHolderKey != nullptr && *pLastHolderKey == delta._holderKey;
            if ( bSameHolder )
                continue;
            pending.put( getHistoryTable(), LedgerInternal::makeHistoryKey( delta._holderKey, request._timeMs, request._journalKey ), vector<uint8>{},
                         ServiceRecord::kAbsentVersion );
            pLastHolderKey = &delta._holderKey;
        }

        const size_t totalWriteCount = inoutTransaction.getWrites().size() + pending.getWrites().size();
        if ( totalWriteCount > static_cast<size_t>( ServiceTransaction::kMaxWriteCount ) )
        {
            outOutcome._listHolderBalance.clear();
            return LedgerInternal::finish( outOutcome, LedgerResult::Invalid );
        }
        appendWrites( pending, inoutTransaction );
        return LedgerInternal::finish( outOutcome, LedgerResult::Ok );
    }

    LedgerResult Ledger::resolveConflict( IServiceStoreConnection& connection, const LedgerTransferRequest& request, LedgerTransferOutcome& outOutcome )
    {
        ServiceRecord            journalRecord;
        const ServiceStoreResult journalRead = connection.readRecord( getJournalTable(), request._journalKey, journalRecord );
        if ( journalRead == ServiceStoreResult::Ok )
        {
            outOutcome._listHolderBalance.clear();
            return LedgerInternal::replayJournal( journalRecord._bytes, request, outOutcome );
        }
        if ( journalRead == ServiceStoreResult::NotFound )
            return LedgerInternal::finish( outOutcome, LedgerResult::Conflict );
        return LedgerInternal::finish( outOutcome, LedgerResult::Unavailable );
    }

    LedgerResult Ledger::executeTransfer( IServiceStoreConnection& connection, const LedgerTransferRequest& request, LedgerTransferOutcome& outOutcome )
    {
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            ServiceTransaction transaction;
            const LedgerResult staged = stageTransfer( connection, request, transaction, outOutcome );
            if ( staged != LedgerResult::Ok || outOutcome._bReplayed == SW_TRUE )
                return staged;
            ServiceCommitInfo        info;
            const ServiceStoreResult committed = connection.commit( transaction, &info );
            if ( committed == ServiceStoreResult::Ok )
            {
                LedgerInternal::applyCommitVersion( outOutcome, info._commitVersion );
                return LedgerResult::Ok;
            }
            if ( committed == ServiceStoreResult::Invalid )
                return LedgerInternal::finish( outOutcome, LedgerResult::Invalid );
            const LedgerResult resolved = resolveConflict( connection, request, outOutcome );
            if ( resolved != LedgerResult::Conflict )
                return resolved;
            if ( committed == ServiceStoreResult::Unavailable )
                return LedgerInternal::finish( outOutcome, LedgerResult::Unavailable ); // 분개가 없어도 커밋이 늦게 닿을 수 있다 — 같은 키로 나중에
        }
        return LedgerInternal::finish( outOutcome, LedgerResult::Conflict );
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
                balance._assetId       = record._key.substr( prefix.size() );
                balance._version       = record._version;
                if ( decodeBalanceRecord( record._bytes, balance._amount ) == false )
                {
                    SW_LOG_ERROR( "Ledger balance '%#' is unreadable", record._key.c_str() );
                    return ServiceStoreResult::Unavailable;
                }
            }
            if ( static_cast<int32>( listRecord.size() ) < LedgerInternal::kListPageSize )
                return ServiceStoreResult::Ok;
            cursor = listRecord.back()._key;
        }
    }

    ServiceStoreResult Ledger::readBalance( IServiceStoreConnection& connection, const LedgerHolder& holder, string_view assetId, LedgerBalance& outBalance )
    {
        outBalance          = LedgerBalance{};
        outBalance._assetId = string( assetId );
        if ( holder.isValid() == false || holder.isSystem() || LedgerUtil::isValidAssetId( assetId ) == false )
            return ServiceStoreResult::Invalid;
        ServiceRecord            record;
        const ServiceStoreResult read = connection.readRecord( getBalanceTable(), LedgerInternal::makeBalanceKey( holder.makeKey(), assetId ), record );
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
        if ( holder.isValid() == false || holder.isSystem() || maxCount <= 0 )
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
                SW_LOG_WARNING( "Ledger history '%#' points at a missing journal", record._key.c_str() );
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
