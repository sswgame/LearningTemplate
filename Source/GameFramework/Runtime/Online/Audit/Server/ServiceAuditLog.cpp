#include "pch.h"

#include "GameFramework/Runtime/Online/Audit/Server/ServiceAuditLog.h"

#include "Core/Serialization/BitStream.h"

#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct ServiceAuditLogInternal
        {
            static constexpr uint64 kVersion        = 1;
            static constexpr int32  kMaxActorSize   = 128;
            static constexpr int32  kMaxActionSize  = 64;
            static constexpr int32  kMaxSubjectSize = 128;

            static bool isActionText( string_view text )
            {
                if ( text.empty() )
                    return false;
                for ( const utf8 ch : text )
                {
                    const bool bAllowed = ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ) || ch == '_' || ch == '.';
                    if ( bAllowed == false )
                        return false;
                }
                return true;
            }

            static vector<uint8> encode( const ServiceAuditEntry& entry )
            {
                BitWriter writer;
                writer.writeVarUint( kVersion );
                ServiceKeyUtil::writeString( writer, entry._actor );
                ServiceKeyUtil::writeString( writer, entry._action );
                ServiceKeyUtil::writeString( writer, entry._subject );
                ServiceKeyUtil::writeString( writer, entry._before );
                ServiceKeyUtil::writeString( writer, entry._after );
                ServiceKeyUtil::writeString( writer, entry._memo );
                writer.writeVarInt( entry._timeMs );
                return writer.releaseBytes();
            }

            [[nodiscard]] static bool decode( const vector<uint8>& bytes, ServiceAuditEntry& outEntry )
            {
                BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                if ( reader.readVarUint() != kVersion )
                    return false;
                bool bRead = ServiceKeyUtil::readString( reader, kMaxActorSize, outEntry._actor );
                bRead      = bRead && ServiceKeyUtil::readString( reader, kMaxActionSize, outEntry._action );
                bRead      = bRead && ServiceKeyUtil::readString( reader, kMaxSubjectSize, outEntry._subject );
                bRead      = bRead && ServiceKeyUtil::readString( reader, ServiceAuditEntry::kMaxStateSize, outEntry._before );
                bRead      = bRead && ServiceKeyUtil::readString( reader, ServiceAuditEntry::kMaxStateSize, outEntry._after );
                bRead      = bRead && ServiceKeyUtil::readString( reader, ServiceAuditEntry::kMaxMemoSize, outEntry._memo );
                if ( bRead == false )
                    return false;
                outEntry._timeMs = reader.readVarInt();
                return reader.hasOverflowed() == false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const hashed_string& ServiceAuditLog::getTable()
    {
        static const hashed_string s_table{ "service_audit" };
        return s_table;
    }

    bool ServiceAuditLog::readTimeMs( const ServiceRecord& record, int64& outTimeMs )
    {
        constexpr size_t kUniqueWidth = 32;
        constexpr size_t kTailWidth   = 1 + static_cast<size_t>( ServiceKeyUtil::kHexWidth ) + 1 + kUniqueWidth; // "/<시각>/<고유>"
        const string&    key          = record._key;
        const bool       bMalformed   = key.size() <= kTailWidth || key[key.size() - kTailWidth] != '/' || key[key.size() - 1 - kUniqueWidth] != '/';
        if ( bMalformed )
            return false;
        uint64 timeMs = 0;
        if ( ServiceKeyUtil::parseHex64( string_view( key ).substr( key.size() - kTailWidth + 1, static_cast<size_t>( ServiceKeyUtil::kHexWidth ) ), timeMs ) == false )
            return false;
        outTimeMs = static_cast<int64>( timeMs );
        return true;
    }

    const hashed_string& ServiceAuditLog::getExpiryTable()
    {
        static const hashed_string s_table{ "service_audit_expiry" };
        return s_table;
    }

    ServiceTTLRule ServiceAuditLog::makeTTLRule( int64 expireAfterMs )
    {
        ServiceTTLRule rule;
        rule._table           = getTable();
        rule._ttlIndexTable   = getExpiryTable();
        rule._expireAfterMs   = expireAfterMs;
        rule._pfnReadTime     = &readTimeMs;
        rule._pBackfillSource = ServiceTTLRule::makeRecordBackfillSource<&readTimeMs>();
        rule._backfillVersion = 1;
        return rule;
    }

    string ServiceAuditLog::makeAccountActor( uint64 accountID )
    {
        return "acct." + ServiceKeyUtil::makeHex64( accountID );
    }

    string ServiceAuditLog::makeOperatorActor( uint64 operatorID )
    {
        return operatorID == 0 ? string( kSystemActor ) : "gm." + ServiceKeyUtil::makeHex64( operatorID );
    }

    string ServiceAuditLog::makeKey( const ServiceAuditEntry& entry, uint64 uniqueHigh, uint64 uniqueLow )
    {
        string key{ entry._subject };
        key.push_back( '/' );
        ServiceKeyUtil::appendHex64( key, static_cast<uint64>( entry._timeMs ) );
        key.push_back( '/' );
        ServiceKeyUtil::appendHex64( key, uniqueHigh );
        ServiceKeyUtil::appendHex64( key, uniqueLow );
        return key;
    }

    bool ServiceAuditLog::isValidEntry( const ServiceAuditEntry& entry )
    {
        const bool bSizes = entry._actor.empty() == false && entry._actor.size() <= static_cast<size_t>( ServiceAuditLogInternal::kMaxActorSize ) &&
                            entry._action.size() <= static_cast<size_t>( ServiceAuditLogInternal::kMaxActionSize ) &&
                            entry._subject.size() <= static_cast<size_t>( ServiceAuditLogInternal::kMaxSubjectSize ) &&
                            entry._before.size() <= static_cast<size_t>( ServiceAuditEntry::kMaxStateSize ) &&
                            entry._after.size() <= static_cast<size_t>( ServiceAuditEntry::kMaxStateSize ) &&
                            entry._memo.size() <= static_cast<size_t>( ServiceAuditEntry::kMaxMemoSize );
        if ( bSizes == false || entry._timeMs < 0 )
            return false;
        if ( ServiceAuditLogInternal::isActionText( entry._action ) == false )
            return false;
        return ServiceTransaction::isValidKey( entry._subject ); // subject 는 키 앞부분이다
    }

    void ServiceAuditLog::stageEntry( ServiceTransaction& inoutTransaction, const ServiceAuditEntry& entry, uint64 uniqueHigh, uint64 uniqueLow )
    {
        if ( isValidEntry( entry ) == false )
        {
            inoutTransaction.put( getTable(), string_view{}, vector<uint8>{}, ServiceRecord::kAbsentVersion ); // 빈 키 — 트랜잭션 전체가 Invalid
            return;
        }
        const string key      = makeKey( entry, uniqueHigh, uniqueLow );
        const string indexKey = ServiceTTLRule::makeTTLIndexKey( entry._timeMs, key );
        if ( ServiceTransaction::isValidKey( indexKey ) == false )
        {
            inoutTransaction.put( getTable(), string_view{}, vector<uint8>{}, ServiceRecord::kAbsentVersion ); // 색인에 들지 않는 긴 대상이다 — 쓸기가 못 지우는 줄을 남기지 않는다
            return;
        }
        inoutTransaction.put( getTable(), key, ServiceAuditLogInternal::encode( entry ), ServiceRecord::kAbsentVersion );
        inoutTransaction.put( getExpiryTable(), indexKey, vector<uint8>{}, ServiceRecord::kAnyVersion );
    }

    ServiceStoreResult ServiceAuditLog::listEntries( IServiceStoreConnection& connection, string_view subjectPrefix, string_view cursor, int32 maxCount,
                                                     vector<ServiceAuditEntry>& outListEntry, string& outNextCursor )
    {
        outNextCursor.clear();
        vector<ServiceRecord>    listRecord;
        const ServiceStoreResult result = connection.listRecords( getTable(), subjectPrefix, cursor, maxCount, true, listRecord );
        if ( result != ServiceStoreResult::Ok )
            return result;
        for ( const ServiceRecord& record : listRecord )
        {
            ServiceAuditEntry& entry = outListEntry.emplace_back();
            if ( ServiceAuditLogInternal::decode( record._bytes, entry ) == false )
                return ServiceStoreResult::Invalid;
        }
        if ( listRecord.empty() == false && static_cast<int32>( listRecord.size() ) == maxCount )
            outNextCursor = listRecord.back()._key;
        return ServiceStoreResult::Ok;
    }
} // namespace sw

namespace sw
{
    ServiceStoreResult ServiceAuditedWrite::commit( IServiceStoreConnection& connection ) const
    {
        ServiceRecord            current;
        const ServiceStoreResult readResult = connection.readRecord( _table, _key, current );
        if ( readResult == ServiceStoreResult::Unavailable || readResult == ServiceStoreResult::Invalid )
            return ServiceStoreResult::Unavailable;
        if ( _bytes.empty() && readResult == ServiceStoreResult::NotFound )
            return ServiceStoreResult::NotFound;
        ServiceTransaction transaction;
        if ( _bytes.empty() )
            transaction.erase( _table, _key, current._version );
        else
            transaction.put( _table, _key, _bytes, current._version );
        ServiceAuditLog::stageEntry( transaction, _auditEntry, _auditUnique._high, _auditUnique._low );
        return connection.commit( transaction ) == ServiceStoreResult::Ok ? ServiceStoreResult::Ok : ServiceStoreResult::Unavailable;
    }
} // namespace sw
