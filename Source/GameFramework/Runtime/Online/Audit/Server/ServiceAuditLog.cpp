#include "pch.h"

#include "GameFramework/Runtime/Online/Audit/Server/ServiceAuditLog.h"

#include "Core/Serialization/BitStream.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceCommitRunner.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordReader.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordTable.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    template <>
    struct ServiceConflictAnswer<ServiceAuditedWriteResult>
    {
        [[nodiscard]] static ServiceAuditedWriteResult make() { return ServiceAuditedWriteResult::Busy; }
    };
} // namespace sw

namespace sw
{
    namespace
    {
        struct ServiceAuditLogInternal
        {
            static constexpr int32 kMaxActorSize   = 128;
            static constexpr int32 kMaxActionSize  = 64;
            static constexpr int32 kMaxSubjectSize = 128;
        };
    } // namespace
} // namespace sw

namespace sw
{
    const ServiceRecordTable<ServiceAuditEntry>& ServiceAuditLog::getTable()
    {
        static const ServiceRecordTable<ServiceAuditEntry> s_table{ "service_audit" };
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

    const ServiceIndexTable& ServiceAuditLog::getExpiryTable()
    {
        static const ServiceIndexTable s_table{ "service_audit_expiry" };
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
        rule._backfillVersion = 0;
        return rule;
    }

    string ServiceAuditLog::makeAccountActor( uint64 accountID )
    {
        StringBuilder<constant::buffer::kMax64> actor;
        actor.appendFormat( "acct.%#", ServiceKeyUtil::makeHex64( accountID ) );
        return string( actor.view() );
    }

    string ServiceAuditLog::makeOperatorActor( uint64 operatorID )
    {
        if ( operatorID == 0 )
            return string( kSystemActor );
        StringBuilder<constant::buffer::kMax64> actor;
        actor.appendFormat( "gm.%#", ServiceKeyUtil::makeHex64( operatorID ) );
        return string( actor.view() );
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
        if ( ServiceKeyUtil::isTokenText( entry._action, ServiceAuditLogInternal::kMaxActionSize, "_." ) == false ) // 길이 상한은 위 bSizes 와 같은 값이다 — 여기서 더 걸리는 것은 빈 글 · 글자뿐
            return false;
        return ServiceTransaction::isValidKey( entry._subject ); // subject 는 키 앞부분이다
    }

    void ServiceAuditLog::stageEntry( ServiceTransaction& inoutTransaction, const ServiceAuditEntry& entry, uint64 uniqueHigh, uint64 uniqueLow )
    {
        if ( isValidEntry( entry ) == false )
        {
            inoutTransaction.markMalformed(); // 트랜잭션 전체가 Invalid
            return;
        }
        const string key      = makeKey( entry, uniqueHigh, uniqueLow );
        const string indexKey = ServiceTTLRule::makeTTLIndexKey( entry._timeMs, key );
        if ( ServiceTransaction::isValidKey( indexKey ) == false )
        {
            inoutTransaction.markMalformed(); // 색인에 들지 않는 긴 대상이다 — 쓸기가 못 지우는 줄을 남기지 않는다
            return;
        }
        inoutTransaction.put( getTable(), key, entry, ServiceRecord::kAbsentVersion );
        inoutTransaction.put( getExpiryTable(), indexKey, ServiceRecord::kAnyVersion );
    }

    void ServiceAuditLog::commitRejection( IServiceStoreConnection& connection, const ServiceAuditEntry& requestedEntry )
    {
        ServiceAuditEntry rejected = requestedEntry;
        rejected._action += ".denied";
        const NetIdempotencyKey unique = NetIdempotencyKey::makeRandom();
        ServiceTransaction      transaction;
        stageEntry( transaction, rejected, unique._high, unique._low );
        const ServiceStoreResult committed = ServiceCommitRunner::commitOnce( connection, transaction );
        if ( committed != ServiceStoreResult::Ok )
            SW_LOG_WARNING_ID( "ServiceAuditLog.RejectionNotRecorded", "Denied operator request '%#' on '%#' could not be audited (result %#)", rejected._action, rejected._subject,
                               static_cast<int32>( committed ) );
    }

    ServiceStoreResult ServiceAuditLog::listEntries( IServiceStoreConnection& connection, string_view subjectPrefix, string_view cursor, int32 maxCount,
                                                     vector<ServiceAuditEntry>& outListEntry, string& outNextCursor )
    {
        outNextCursor.clear();
        vector<ServiceRecord>    listRecord;
        const ServiceStoreResult result = ServiceRecordReader::readPage( connection, getTable(), subjectPrefix, cursor, maxCount, true, listRecord )._result;
        if ( result != ServiceStoreResult::Ok )
            return result;
        for ( const ServiceRecord& record : listRecord )
        {
            ServiceAuditEntry& entry = outListEntry.emplace_back();
            if ( getTable().decode( record, entry ) == false )
                return ServiceStoreResult::Invalid;
        }
        if ( listRecord.empty() == false && static_cast<int32>( listRecord.size() ) == maxCount )
            outNextCursor = listRecord.back()._key;
        return ServiceStoreResult::Ok;
    }
} // namespace sw

namespace sw
{
    void ServiceAuditedWrite::setOperatorAudit( const ServiceOperator& actor, string_view action, string_view subject, UnixMilliseconds nowMs, string_view memo )
    {
        _pGuard              = actor._pGuard;
        _auditEntry._actor   = ServiceAuditLog::makeOperatorActor( actor._actorID );
        _auditEntry._action  = string( action );
        _auditEntry._subject = string( subject );
        _auditEntry._memo    = string( memo );
        _auditEntry._timeMs  = nowMs.count();
    }

    ServiceAuditedWriteResult ServiceAuditedWrite::commit( IServiceStoreConnection& connection ) const
    {
        const bool bOneWrite = _change.getWrites().size() == 1 && _change.isWellFormed();
        if ( bOneWrite == false )
        {
            SW_LOG_ERROR_ID( "ServiceAuditLog.WriteNotStaged", "Operator write '%#' does not hold exactly one well-formed write - refused", _auditEntry._action );
            return ServiceAuditedWriteResult::Unavailable;
        }
        const ServiceWrite& change = _change.getWrites()[0];
        if ( _pGuard == nullptr )
        {
            SW_LOG_ERROR_ID( "ServiceAuditLog.WriteWithoutGuard", "Operator write to '%#/%#' has no guard - refused", change._table.c_str(), change._key.c_str() );
            ServiceAuditLog::commitRejection( connection, _auditEntry );
            return ServiceAuditedWriteResult::Forbidden;
        }
        return ServiceCommitRunner::run<ServiceAuditedWriteResult>( connection, "Service.AuditedWrite", [this, &change]( ServiceCommitAttempt& attempt ) -> optional<ServiceAuditedWriteResult>
        {
            const ServiceStoreResult allowed = _pGuard->validateAllowed( attempt );
            if ( allowed == ServiceStoreResult::NotFound )
            {
                ServiceAuditLog::commitRejection( attempt, _auditEntry );
                return ServiceAuditedWriteResult::Forbidden;
            }
            if ( allowed != ServiceStoreResult::Ok )
                return ServiceAuditedWriteResult::Unavailable;
            ServiceRecord            current;
            const ServiceStoreResult readResult = attempt.readRecord( change._table, change._key, current );
            if ( readResult == ServiceStoreResult::Unavailable || readResult == ServiceStoreResult::Invalid )
                return ServiceAuditedWriteResult::Unavailable;
            const bool bErase = change._kind == ServiceWrite::Kind::Erase;
            if ( bErase && readResult == ServiceStoreResult::NotFound )
                return ServiceAuditedWriteResult::NotFound;
            ServiceWrite guarded     = change;
            guarded._expectedVersion = current._version; // 읽은 판을 조건으로 — 그사이 바뀌었으면 다시 한다
            ServiceTransaction transaction;
            transaction.addWrite( guarded );
            ServiceAuditLog::stageEntry( transaction, _auditEntry, _auditUnique._high, _auditUnique._low );
            const ServiceStoreResult committed = attempt.commit( transaction );
            if ( committed == ServiceStoreResult::Conflict )
                return nullopt;
            return committed == ServiceStoreResult::Ok ? ServiceAuditedWriteResult::Ok : ServiceAuditedWriteResult::Unavailable;
        } );
    }
} // namespace sw
