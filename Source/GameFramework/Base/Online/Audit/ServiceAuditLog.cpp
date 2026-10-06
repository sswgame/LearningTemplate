#include "pch.h"

#include "GameFramework/Base/Online/Audit/ServiceAuditLog.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

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
        inoutTransaction.put( getTable(), makeKey( entry, uniqueHigh, uniqueLow ), ServiceAuditLogInternal::encode( entry ), ServiceRecord::kAbsentVersion );
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
