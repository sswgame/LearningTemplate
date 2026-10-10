#include "pch.h"

#include "GameFramework/Base/Online/Local/MemoryLocalStore.h"

#include "Core/File/FileUtil.h"

namespace sw
{
    MemoryLocalDatabase::MemoryLocalDatabase()
        : _mutex{}
        , _mapSlot{}
        , _bFailNextWrite{ SW_FALSE }
    {
    }

    LocalStoreResult MemoryLocalDatabase::readSlot( const string& slot, vector<uint8>& outEnvelopeBytes )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              slotIt = _mapSlot.find( slot );
        if ( slotIt == _mapSlot.end() )
            return LocalStoreResult::NotFound;
        outEnvelopeBytes = slotIt->second._envelope;
        return LocalStoreResult::Ok;
    }

    LocalStoreResult MemoryLocalDatabase::writeSlot( const string& slot, const vector<uint8>& envelopeBytes )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        if ( _bFailNextWrite == SW_TRUE )
        {
            _bFailNextWrite = SW_FALSE;
            return LocalStoreResult::IOError;
        }
        SlotEntry& entry   = _mapSlot[slot];
        entry._envelope    = envelopeBytes;
        entry._writtenAtMs = FileUtil::getCurrentFileWriteTime() / ( FileUtil::kFileTimeTicksPerSecond / 1000 );
        return LocalStoreResult::Ok;
    }

    LocalStoreResult MemoryLocalDatabase::eraseSlot( const string& slot )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _mapSlot.erase( slot ) > 0 ? LocalStoreResult::Ok : LocalStoreResult::NotFound;
    }

    LocalStoreResult MemoryLocalDatabase::listSlots( const string& groupPrefix, vector<LocalSlotInfo>& outListSlotInfo )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        for ( const auto& [slot, entry] : _mapSlot )
        {
            if ( slot.compare( 0, groupPrefix.size(), groupPrefix ) != 0 )
                continue;
            LocalSlotInfo& info = outListSlotInfo.emplace_back();
            info._slot          = slot;
            info._byteCount     = static_cast<int64>( entry._envelope.size() );
            info._writtenAtMs   = entry._writtenAtMs;
        }
        return LocalStoreResult::Ok;
    }

    void MemoryLocalDatabase::failNextWrite()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _bFailNextWrite = SW_TRUE;
    }

    bool MemoryLocalDatabase::modifyEnvelopeByte( const string& slot, size_t offset, uint8 xorMask )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              slotIt = _mapSlot.find( slot );
        if ( slotIt == _mapSlot.end() || offset >= slotIt->second._envelope.size() )
            return false;
        slotIt->second._envelope[offset] ^= xorMask;
        return true;
    }

    bool MemoryLocalDatabase::findEnvelope( const string& slot, vector<uint8>& outEnvelopeBytes ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              slotIt = _mapSlot.find( slot );
        if ( slotIt == _mapSlot.end() )
            return false;
        outEnvelopeBytes = slotIt->second._envelope;
        return true;
    }
} // namespace sw

namespace sw
{
    MemoryLocalStore::MemoryLocalStore( MemoryLocalDatabase* pDatabase, const LocalSealContext& sealContext )
        : _listCompletion{}
        , _sealContext{ sealContext }
        , _pDatabase{ pDatabase }
        , _nextRequestID{ 1 }
        , _bShutdown{ SW_FALSE }
    {
    }

    uint64 MemoryLocalStore::submitRead( string_view slot )
    {
        LocalStoreRequest request;
        request._slot      = string( slot );
        request._operation = LocalStoreOperation::Read;
        return executeRequest( request );
    }

    uint64 MemoryLocalStore::submitWrite( string_view slot, vector<uint8> bytes, const LocalStoreWriteOptions& options )
    {
        LocalStoreRequest request;
        request._bytes     = std::move( bytes );
        request._slot      = string( slot );
        request._options   = options;
        request._operation = LocalStoreOperation::Write;
        return executeRequest( request );
    }

    uint64 MemoryLocalStore::submitErase( string_view slot )
    {
        LocalStoreRequest request;
        request._slot      = string( slot );
        request._operation = LocalStoreOperation::Erase;
        return executeRequest( request );
    }

    uint64 MemoryLocalStore::submitList( string_view groupPrefix )
    {
        LocalStoreRequest request;
        request._slot      = string( groupPrefix );
        request._operation = LocalStoreOperation::List;
        return executeRequest( request );
    }

    int32 MemoryLocalStore::pollCompletions( vector<LocalStoreCompletion>& outListCompletion )
    {
        const int32 completionCount = static_cast<int32>( _listCompletion.size() );
        for ( LocalStoreCompletion& completion : _listCompletion )
        {
            outListCompletion.push_back( std::move( completion ) );
        }
        _listCompletion.clear();
        return completionCount;
    }

    void MemoryLocalStore::shutdown() { _bShutdown = SW_TRUE; }

    uint64 MemoryLocalStore::executeRequest( LocalStoreRequest& request )
    {
        request._requestID = _nextRequestID++;
        if ( _bShutdown == SW_TRUE )
            _listCompletion.push_back( LocalStoreRequestUtil::makeCompletion( request, LocalStoreResult::IOError ) );
        else
            _listCompletion.push_back( LocalStoreRequestUtil::execute( request, *_pDatabase, _sealContext ) );
        return request._requestID;
    }
} // namespace sw
