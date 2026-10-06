#include "pch.h"

#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"

#include "Core/Common/HashUtil.h"
#include "Core/Log/LogContext.h"
#include "Core/String/StringUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct MemoryServiceStoreInternal
        {
            static bool isVersionMatched( uint64 expectedVersion, uint64 currentVersion )
            {
                return expectedVersion == ServiceRecord::kAnyVersion || expectedVersion == currentVersion;
            }

            static uint64 mixHash( uint64 hash, const void* pData, size_t size )
            {
                return StringUtil::computeHash64( static_cast<const utf8*>( pData ), size, false, hash );
            }

            /** @brief 내리는 중인 앞의 연결 — 모든 호출이 Unavailable 입니다. */
            class ClosedConnection final : public IServiceStoreConnection
            {
            public:
                ServiceStoreResult readRecord( const hashed_string& table, string_view key, ServiceRecord& outRecord ) override
                {
                    (void)table;
                    (void)key;
                    outRecord = ServiceRecord{};
                    return ServiceStoreResult::Unavailable;
                }

                ServiceStoreResult listRecords( const hashed_string& table, string_view keyPrefix, string_view cursorKey, int32 maxCount, bool bDescending,
                                                vector<ServiceRecord>& outListRecord ) override
                {
                    (void)table;
                    (void)keyPrefix;
                    (void)cursorKey;
                    (void)maxCount;
                    (void)bDescending;
                    (void)outListRecord;
                    return ServiceStoreResult::Unavailable;
                }

                ServiceStoreResult commit( const ServiceTransaction& transaction, ServiceCommitInfo* pOutInfo ) override
                {
                    (void)transaction;
                    (void)pOutInfo;
                    return ServiceStoreResult::Unavailable;
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    MemoryServiceDatabase::MemoryServiceDatabase()
        : _mutex{}
        , _mapTable{}
        , _commitVersion{ 0 }
        , _commitCount{ 0 }
        , _faultSkipCount{ 0 }
        , _armedFault{ ServiceStoreFault::None }
    {
    }

    ServiceStoreResult MemoryServiceDatabase::readRecord( const hashed_string& table, string_view key, ServiceRecord& outRecord )
    {
        outRecord._bytes.clear();
        outRecord._key.clear();
        outRecord._version = ServiceRecord::kAbsentVersion;
        if ( table.empty() || ServiceTransaction::isValidKey( key ) == false )
            return ServiceStoreResult::Invalid;
        std::scoped_lock<mutex> lock{ _mutex };
        if ( consumeFault( ServiceStoreFault::RejectRead ) )
            return ServiceStoreResult::Unavailable;
        const Entry* pEntry = findEntry( table, key );
        if ( pEntry == nullptr )
            return ServiceStoreResult::NotFound;
        outRecord._bytes   = pEntry->_bytes;
        outRecord._version = pEntry->_version;
        return ServiceStoreResult::Ok;
    }

    ServiceStoreResult MemoryServiceDatabase::listRecords( const hashed_string& table, string_view keyPrefix, string_view cursorKey, int32 maxCount, bool bDescending,
                                                           vector<ServiceRecord>& outListRecord )
    {
        if ( table.empty() || maxCount <= 0 )
            return ServiceStoreResult::Invalid;
        std::scoped_lock<mutex> lock{ _mutex };
        if ( consumeFault( ServiceStoreFault::RejectRead ) )
            return ServiceStoreResult::Unavailable;
        const auto tableIt = _mapTable.find( table );
        if ( tableIt == _mapTable.end() )
            return ServiceStoreResult::Ok;
        const Table& entries    = tableIt->second;
        int32        addedCount = 0;
        if ( bDescending == false )
        {
            auto it = cursorKey.empty() ? entries.lower_bound( keyPrefix ) : entries.upper_bound( cursorKey );
            for ( ; it != entries.end() && addedCount < maxCount; ++it )
            {
                if ( StringUtil::startsWith( it->first, keyPrefix ) == false )
                    break;
                ServiceRecord& record = outListRecord.emplace_back();
                record._key           = it->first;
                record._bytes         = it->second._bytes;
                record._version       = it->second._version;
                ++addedCount;
            }
            return ServiceStoreResult::Ok;
        }
        // 내림차순 — 커서 바로 앞(커서가 없으면 접두어 범위 끝)에서 거꾸로 걷는다. 키는 ASCII(키 규칙)라 접두어 + 0x7F 가 범위 끝이다.
        string upperKey{ keyPrefix };
        upperKey.push_back( static_cast<utf8>( 0x7F ) );
        auto it = cursorKey.empty() ? entries.upper_bound( string_view{ upperKey } ) : entries.lower_bound( cursorKey );
        while ( it != entries.begin() && addedCount < maxCount )
        {
            --it;
            if ( StringUtil::startsWith( it->first, keyPrefix ) == false )
                break; // 범위 끝 아래에서 접두어가 아니면 접두어보다 작다
            ServiceRecord& record = outListRecord.emplace_back();
            record._key           = it->first;
            record._bytes         = it->second._bytes;
            record._version       = it->second._version;
            ++addedCount;
        }
        return ServiceStoreResult::Ok;
    }

    ServiceStoreResult MemoryServiceDatabase::commit( const ServiceTransaction& transaction, ServiceCommitInfo* pOutInfo )
    {
        ServiceCommitInfo        info;
        std::scoped_lock<mutex>  lock{ _mutex };
        const ServiceStoreResult validateResult = validate( transaction, info );
        if ( validateResult != ServiceStoreResult::Ok )
        {
            if ( pOutInfo != nullptr )
                *pOutInfo = info;
            return validateResult;
        }
        if ( consumeFault( ServiceStoreFault::RejectCommit ) )
            return ServiceStoreResult::Unavailable;
        ++_commitVersion;
        ++_commitCount;
        for ( const ServiceWrite& write : transaction.getWrites() )
        {
            switch ( write._kind )
            {
                case ServiceWrite::Kind::Put:
                {
                    Entry& entry   = _mapTable[write._table][write._key];
                    entry._bytes   = write._bytes;
                    entry._version = _commitVersion;
                    break;
                }
                case ServiceWrite::Kind::Erase:
                {
                    const auto tableIt = _mapTable.find( write._table );
                    if ( tableIt != _mapTable.end() )
                        tableIt->second.erase( write._key );
                    break;
                }
                case ServiceWrite::Kind::Require:
                {
                    break;
                }
            }
        }
        info._commitVersion = _commitVersion;
        if ( pOutInfo != nullptr )
            *pOutInfo = info;
        if ( consumeFault( ServiceStoreFault::LoseCommitReply ) )
            return ServiceStoreResult::Unavailable;
        return ServiceStoreResult::Ok;
    }

    void MemoryServiceDatabase::armFault( ServiceStoreFault fault, int32 skipCount )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _armedFault     = fault;
        _faultSkipCount = std::max( skipCount, 0 );
    }

    bool MemoryServiceDatabase::isFaultArmed() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _armedFault != ServiceStoreFault::None;
    }

    uint64 MemoryServiceDatabase::getCommitCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _commitCount;
    }

    int32 MemoryServiceDatabase::countRecords( const hashed_string& table ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              tableIt = _mapTable.find( table );
        return tableIt == _mapTable.end() ? 0 : static_cast<int32>( tableIt->second.size() );
    }

    uint64 MemoryServiceDatabase::computeContentHash() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        vector<hashed_string>   listTable;
        listTable.reserve( _mapTable.size() );
        for ( const auto& [table, entries] : _mapTable )
        {
            if ( entries.empty() == false )
                listTable.push_back( table );
        }
        std::sort( listTable.begin(), listTable.end(), HashedStringLexicalLess{} );
        uint64 hash = HashUtil::kFnvOffset64;
        for ( const hashed_string& table : listTable )
        {
            hash = MemoryServiceStoreInternal::mixHash( hash, table.c_str(), table.size() );
            for ( const auto& [key, entry] : _mapTable.find( table )->second )
            {
                hash = MemoryServiceStoreInternal::mixHash( hash, key.data(), key.size() );
                hash = MemoryServiceStoreInternal::mixHash( hash, entry._bytes.data(), entry._bytes.size() );
            }
        }
        return hash;
    }

    bool MemoryServiceDatabase::consumeFault( ServiceStoreFault fault )
    {
        if ( _armedFault != fault )
            return false;
        if ( _faultSkipCount > 0 )
        {
            --_faultSkipCount;
            return false;
        }
        _armedFault = ServiceStoreFault::None;
        return true;
    }

    const MemoryServiceDatabase::Entry* MemoryServiceDatabase::findEntry( const hashed_string& table, string_view key ) const
    {
        const auto tableIt = _mapTable.find( table );
        if ( tableIt == _mapTable.end() )
            return nullptr;
        const auto entryIt = tableIt->second.find( key );
        return entryIt == tableIt->second.end() ? nullptr : &entryIt->second;
    }

    ServiceStoreResult MemoryServiceDatabase::validate( const ServiceTransaction& transaction, ServiceCommitInfo& outInfo ) const
    {
        if ( transaction.isWellFormed() == false )
            return ServiceStoreResult::Invalid;
        const vector<ServiceWrite>& listWrite = transaction.getWrites();
        for ( size_t writeIndex = 0; writeIndex < listWrite.size(); ++writeIndex )
        {
            const ServiceWrite& write          = listWrite[writeIndex];
            const Entry*        pEntry         = findEntry( write._table, write._key );
            const uint64        currentVersion = pEntry == nullptr ? ServiceRecord::kAbsentVersion : pEntry->_version;
            if ( MemoryServiceStoreInternal::isVersionMatched( write._expectedVersion, currentVersion ) == false )
            {
                outInfo._conflictIndex = static_cast<int32>( writeIndex );
                return ServiceStoreResult::Conflict;
            }
        }
        return ServiceStoreResult::Ok;
    }
} // namespace sw

namespace sw
{
    MemoryServiceStore::MemoryServiceStore( MemoryServiceDatabase* pDatabase )
        : _mutex{}
        , _listCompleted{}
        , _pDatabase{ pDatabase }
        , _bShutdown{ SW_FALSE }
    {
    }

    MemoryServiceStore::~MemoryServiceStore()
    {
        // 거두지 않은 일은 complete 없이 버린다 — 서비스가 먼저 내려가 complete 가 가리킬 곳이 없다(SQL 구현도 같다).
        std::scoped_lock<mutex> lock{ _mutex };
        _listCompleted.clear();
    }

    void MemoryServiceStore::submit( unique_ptr<IServiceStoreWork> work )
    {
        bool bShutdown = false;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            bShutdown = _bShutdown == SW_TRUE;
        }
        work->bindLogContext( LogContext::getCurrent() );
        {
            ScopedLogContext scope( work->getLogContext() );
            if ( bShutdown )
            {
                MemoryServiceStoreInternal::ClosedConnection closed;
                work->run( closed );
            }
            else
            {
                work->run( *_pDatabase );
            }
        }
        std::scoped_lock<mutex> lock{ _mutex };
        _listCompleted.push_back( std::move( work ) );
    }

    int32 MemoryServiceStore::pollCompletions()
    {
        // complete 안에서 새 일을 맡길 수 있다 — 꺼낸 목록을 따로 돌고, 새로 쌓인 것은 다음 poll 이 거둔다(SQL 구현과 같은 순서 약속).
        vector<unique_ptr<IServiceStoreWork>> listReady;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            listReady.swap( _listCompleted );
        }
        for ( unique_ptr<IServiceStoreWork>& work : listReady )
        {
            ScopedLogContext scope( work->getLogContext() ); // 문맥 없는 스레드에서 거둬도 맡긴 요청의 꼬리표로
            work->complete();
        }
        return static_cast<int32>( listReady.size() );
    }

    int32 MemoryServiceStore::getPendingCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return static_cast<int32>( _listCompleted.size() );
    }

    void MemoryServiceStore::shutdown()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _bShutdown = SW_TRUE;
    }
} // namespace sw
