#include "pch.h"

#include "GameFramework/Runtime/Foundation/Save/Local/MemoryLocalStore.h"

namespace sw
{
    MemoryLocalStore::MemoryLocalStore( MemoryLocalStoreBackend* pDatabase, const LocalSealContext& sealContext )
        : _completionBuffer{}
        , _sealContext{ sealContext }
        , _pDatabase{ pDatabase }
        , _requestIDAllocator{}
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
        const int32 completionCount = static_cast<int32>( _completionBuffer.getCount() );
        _completionBuffer.moveAllTo( outListCompletion );
        return completionCount;
    }

    void MemoryLocalStore::shutdown()
    {
        _bShutdown = SW_TRUE;
    }

    uint64 MemoryLocalStore::executeRequest( LocalStoreRequest& request )
    {
        request._requestID = _requestIDAllocator.allocate();
        if ( _bShutdown == SW_TRUE )
            _completionBuffer.push( LocalStoreRequest::makeCompletion( request, LocalStoreResult::IOError ) );
        else
            _completionBuffer.push( LocalStoreRequest::execute( request, *_pDatabase, _sealContext ) );
        return request._requestID;
    }
} // namespace sw
