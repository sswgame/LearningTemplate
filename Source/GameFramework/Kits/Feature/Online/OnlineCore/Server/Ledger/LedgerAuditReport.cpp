#include "pch.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Ledger/LedgerAuditReport.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/map.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Runtime/Online/Ledger/Server/Ledger.h"

namespace sw
{
    namespace
    {
        struct LedgerAuditInternal
        {
            static constexpr int32 kPageSize = 256;

            static LedgerAssetAudit& getOrCreate( map<string, LedgerAssetAudit>& inoutMapAsset, const string& assetID )
            {
                LedgerAssetAudit& audit = inoutMapAsset[assetID];
                audit._assetID          = assetID;
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

    const LedgerAssetAudit* LedgerAuditReport::findAsset( string_view assetID ) const
    {
        return VectorUtil::findBy<&LedgerAssetAudit::_assetID>( _listAsset, assetID );
    }

    ServiceStoreResult LedgerAuditReport::computeReport( IServiceStoreConnection& connection, LedgerAuditReport& outReport )
    {
        outReport = LedgerAuditReport{};
        map<string, LedgerAssetAudit> mapAsset;

        // 공급 = 이월(보존 쓸기가 지운 분개의 합) + 남은 분개
        vector<LedgerSupplyStats> listSupply;
        const ServiceStoreResult  carried = Ledger::addCarriedSupply( connection, listSupply, outReport._unreadableRecordCount );
        if ( carried != ServiceStoreResult::Ok )
            return carried;

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
                Ledger::addSupply( entry, listSupply );
            }
            if ( static_cast<int32>( listRecord.size() ) < LedgerAuditInternal::kPageSize )
                break;
            cursor = listRecord.back()._key;
        }

        for ( const LedgerSupplyStats& supply : listSupply )
        {
            LedgerAssetAudit& audit = LedgerAuditInternal::getOrCreate( mapAsset, supply._assetID );
            audit._issued += supply._issued;
            audit._burned += supply._burned;
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
                    const bool bAccount = StringUtil::startsWith( record._key, "acct/" );
                    if ( bAccount )
                        ++outReport._debtBalanceCount;
                    else
                        ++outReport._negativeEscrowCount;
                }
                LedgerAuditInternal::getOrCreate( mapAsset, record._key.substr( slash + 1 ) )._held += amount;
            }
            if ( static_cast<int32>( listRecord.size() ) < LedgerAuditInternal::kPageSize )
                break;
            cursor = listRecord.back()._key;
        }

        outReport._listAsset.reserve( mapAsset.size() );
        for ( auto& [assetID, audit] : mapAsset )
        {
            outReport._listAsset.push_back( std::move( audit ) );
        }
        return ServiceStoreResult::Ok;
    }
} // namespace sw
