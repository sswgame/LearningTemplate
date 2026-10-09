#include "pch.h"

#include "GameFramework/Base/Online/Ledger/LedgerAudit.h"

#include "Core/Container/map.h"

#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"

namespace sw
{
    namespace
    {
        struct LedgerAuditInternal
        {
            static constexpr int32 kPageSize = 256;

            static LedgerAssetAudit& findOrAdd( map<string, LedgerAssetAudit>& inoutMapAsset, const string& assetId )
            {
                LedgerAssetAudit& audit = inoutMapAsset[assetId];
                audit._assetId          = assetId;
                return audit;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool LedgerAuditReport::isBalanced() const
    {
        if ( _negativeEscrowCount != 0 || _unreadableRecordCount != 0 )
            return false;
        for ( const LedgerAssetAudit& asset : _listAsset )
        {
            if ( asset.isBalanced() == false )
                return false;
        }
        return true;
    }

    const LedgerAssetAudit* LedgerAuditReport::findAsset( string_view assetId ) const
    {
        for ( const LedgerAssetAudit& asset : _listAsset )
        {
            if ( asset._assetId == assetId )
                return &asset;
        }
        return nullptr;
    }

    ServiceStoreResult LedgerAudit::computeReport( IServiceStoreConnection& connection, LedgerAuditReport& outReport )
    {
        outReport = LedgerAuditReport{};
        map<string, LedgerAssetAudit> mapAsset;

        string cursor;
        while ( true )
        {
            vector<ServiceRecord>    listRecord;
            const ServiceStoreResult listed = connection.listRecords( Ledger::getJournalTable(), "", cursor, LedgerAuditInternal::kPageSize, false, listRecord );
            if ( listed != ServiceStoreResult::Ok )
                return listed;
            for ( const ServiceRecord& record : listRecord )
            {
                LedgerJournalEntry       entry;
                const ServiceStoreResult found = Ledger::findJournal( connection, record._key, entry ); // 해독을 한 곳에 둔다(읽기 한 번 더 — 검사 작업이라 괜찮다)
                if ( found != ServiceStoreResult::Ok )
                {
                    ++outReport._unreadableRecordCount;
                    continue;
                }
                ++outReport._journalCount;
                for ( const LedgerPosting& posting : entry._listPosting )
                {
                    if ( posting._from._kind == LedgerHolderKind::Mint )
                        LedgerAuditInternal::findOrAdd( mapAsset, posting._assetId )._issued += posting._amount;
                    if ( posting._to._kind == LedgerHolderKind::Sink )
                        LedgerAuditInternal::findOrAdd( mapAsset, posting._assetId )._burned += posting._amount;
                }
            }
            if ( static_cast<int32>( listRecord.size() ) < LedgerAuditInternal::kPageSize )
                break;
            cursor = listRecord.back()._key;
        }

        cursor.clear();
        while ( true )
        {
            vector<ServiceRecord>    listRecord;
            const ServiceStoreResult listed = connection.listRecords( Ledger::getBalanceTable(), "", cursor, LedgerAuditInternal::kPageSize, false, listRecord );
            if ( listed != ServiceStoreResult::Ok )
                return listed;
            for ( const ServiceRecord& record : listRecord )
            {
                const size_t slash  = record._key.rfind( '/' );
                int64        amount = 0;
                if ( slash == string::npos || Ledger::decodeBalanceRecord( record._bytes, amount ) == false )
                {
                    ++outReport._unreadableRecordCount;
                    continue;
                }
                if ( amount < 0 )
                {
                    const bool bAccount = record._key.rfind( "acct/", 0 ) == 0;
                    if ( bAccount )
                        ++outReport._debtBalanceCount;
                    else
                        ++outReport._negativeEscrowCount;
                }
                LedgerAuditInternal::findOrAdd( mapAsset, record._key.substr( slash + 1 ) )._held += amount;
            }
            if ( static_cast<int32>( listRecord.size() ) < LedgerAuditInternal::kPageSize )
                break;
            cursor = listRecord.back()._key;
        }

        outReport._listAsset.reserve( mapAsset.size() );
        for ( auto& [assetId, audit] : mapAsset )
        {
            outReport._listAsset.push_back( std::move( audit ) );
        }
        return ServiceStoreResult::Ok;
    }
} // namespace sw
