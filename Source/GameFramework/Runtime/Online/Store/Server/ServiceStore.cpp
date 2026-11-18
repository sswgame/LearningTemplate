#include "pch.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"

#include "Core/Hash/HashUtil.h"

#include "GameFramework/Runtime/Online/Service/OnlineLimit.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceReadSetConnection.h"

namespace sw
{
    void IServiceStoreJob::execute( IServiceStoreConnection& connection, ServiceStoreCommitStats& inoutStats )
    {
        ServiceReadSetConnection readSet( connection, &inoutStats );
        run( readSet );
    }

    ServiceTransaction::ServiceTransaction()
        : _listWrite{}
    {
    }

    void ServiceTransaction::put( const hashed_string& table, string_view key, vector<uint8> bytes, uint64 expectedVersion )
    {
        ServiceWrite& write    = _listWrite.emplace_back();
        write._bytes           = std::move( bytes );
        write._key             = string{ key };
        write._table           = table;
        write._expectedVersion = expectedVersion;
        write._kind            = ServiceWrite::Kind::Put;
    }

    void ServiceTransaction::erase( const hashed_string& table, string_view key, uint64 expectedVersion )
    {
        ServiceWrite& write    = _listWrite.emplace_back();
        write._key             = string{ key };
        write._table           = table;
        write._expectedVersion = expectedVersion;
        write._kind            = ServiceWrite::Kind::Erase;
    }

    void ServiceTransaction::requireVersion( const hashed_string& table, string_view key, uint64 expectedVersion )
    {
        ServiceWrite& write    = _listWrite.emplace_back();
        write._key             = string{ key };
        write._table           = table;
        write._expectedVersion = expectedVersion;
        write._kind            = ServiceWrite::Kind::Require;
    }

    void ServiceTransaction::requireRange( const hashed_string& table, string_view keyPrefix, string_view cursorKey, int32 maxCount, bool bDescending, uint64 fingerprint )
    {
        ServiceWrite& write       = _listWrite.emplace_back();
        write._key                = string{ keyPrefix };
        write._table              = table;
        write._range._cursorKey   = string{ cursorKey };
        write._range._fingerprint = fingerprint;
        write._range._maxCount    = maxCount;
        write._range._bDescending = bDescending ? SW_TRUE : SW_FALSE;
        write._kind               = ServiceWrite::Kind::RequireRange;
    }

    bool ServiceTransaction::isWellFormed() const
    {
        int32 conditionCount = 0;
        for ( const ServiceWrite& write : _listWrite )
        {
            conditionCount += write._kind == ServiceWrite::Kind::Require || write._kind == ServiceWrite::Kind::RequireRange ? 1 : 0;
        }
        const int32 writeCount = static_cast<int32>( _listWrite.size() ) - conditionCount;
        if ( _listWrite.empty() || writeCount > kMaxWriteCount || conditionCount > kMaxConditionCount )
            return false;
        for ( size_t writeIndex = 0; writeIndex < _listWrite.size(); ++writeIndex )
        {
            const ServiceWrite& write = _listWrite[writeIndex];
            if ( write._kind == ServiceWrite::Kind::RequireRange )
            {
                // 목록 읽기의 접두어 · 커서는 키 규칙을 지나지 않는다(빈 접두어는 표 전체) — 길이만 본다. 저장소에는 매개변수로 간다
                const size_t maxSize = static_cast<size_t>( OnlineLimit::kMaxKeySize );
                if ( write._table.empty() || write._key.size() > maxSize || write._range._cursorKey.size() > maxSize || write._range._maxCount <= 0 )
                    return false;
                continue; // 같은 키 검사에 들지 않는다 — 접두어는 레코드 키가 아니다
            }
            if ( write._table.empty() || isValidKey( write._key ) == false )
                return false;
            if ( write._bytes.size() > static_cast<size_t>( IServiceStoreConnection::kMaxRecordSize ) )
                return false;
            for ( size_t otherIndex = 0; otherIndex < writeIndex; ++otherIndex )
            {
                const ServiceWrite& other    = _listWrite[otherIndex];
                const bool          bSameKey = other._kind != ServiceWrite::Kind::RequireRange && other._table == write._table && other._key == write._key;
                if ( bSameKey )
                    return false;
            }
        }
        return true;
    }

    uint64 ServiceTransaction::computeRangeFingerprint( const vector<ServiceRecord>& listRecord, size_t first )
    {
        const uint64 count = first < listRecord.size() ? static_cast<uint64>( listRecord.size() - first ) : 0;
        uint64       hash  = HashUtil::appendBytes64( HashUtil::kFNVOffset64, &count, sizeof( count ) );
        for ( size_t index = first; index < listRecord.size(); ++index )
        {
            const ServiceRecord& record = listRecord[index];
            const uint64         size   = static_cast<uint64>( record._key.size() ); // 길이를 넣어 키 경계가 섞이지 않게 한다
            hash                        = HashUtil::appendBytes64( hash, &size, sizeof( size ) );
            hash                        = HashUtil::appendBytes64( hash, record._key.data(), record._key.size() );
            hash                        = HashUtil::appendBytes64( hash, &record._version, sizeof( record._version ) );
        }
        return hash;
    }

    bool ServiceTransaction::isValidKey( string_view key )
    {
        if ( key.empty() || key.size() > static_cast<size_t>( OnlineLimit::kMaxKeySize ) )
            return false;
        for ( const utf8 ch : key )
        {
            const bool bLower                    = 'a' <= ch && ch <= 'z';
            const bool bDigit                    = '0' <= ch && ch <= '9';
            const bool bMark                     = ch == '_' || ch == '.' || ch == '/' || ch == '-';
            const bool bInvalidStoreKeyCharacter = bLower == false && bDigit == false && bMark == false;
            if ( bInvalidStoreKeyCharacter )
                return false;
        }
        return true;
    }
} // namespace sw
