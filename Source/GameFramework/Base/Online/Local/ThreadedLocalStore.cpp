#include "pch.h"

#include "GameFramework/Base/Online/Local/ThreadedLocalStore.h"

namespace sw
{
    ThreadedLocalStore::ThreadedLocalStore( unique_ptr<ILocalSlotStorage> storage, const LocalSealContext& sealContext )
        : _listCompletion{}
        , _sealContext{ sealContext }
        , _storage{ std::move( storage ) }
        , _worker{}
        , _mutex{}
        , _requestIDAllocator{}
        , _pendingCount{ 0 }
    {
        _worker.start( "LocalStore", DedicatedWorker<LocalStoreRequest>::ProcessDelegate::create<&ThreadedLocalStore::processRequest>( this ) );
    }

    ThreadedLocalStore::~ThreadedLocalStore()
    {
        shutdown();
    }

    uint64 ThreadedLocalStore::submitRead( string_view slot )
    {
        LocalStoreRequest request;
        request._slot      = string( slot );
        request._operation = LocalStoreOperation::Read;
        return enqueueRequest( std::move( request ) );
    }

    uint64 ThreadedLocalStore::submitWrite( string_view slot, vector<uint8> bytes, const LocalStoreWriteOptions& options )
    {
        LocalStoreRequest request;
        request._bytes     = std::move( bytes );
        request._slot      = string( slot );
        request._options   = options;
        request._operation = LocalStoreOperation::Write;
        return enqueueRequest( std::move( request ) );
    }

    uint64 ThreadedLocalStore::submitErase( string_view slot )
    {
        LocalStoreRequest request;
        request._slot      = string( slot );
        request._operation = LocalStoreOperation::Erase;
        return enqueueRequest( std::move( request ) );
    }

    uint64 ThreadedLocalStore::submitList( string_view groupPrefix )
    {
        LocalStoreRequest request;
        request._slot      = string( groupPrefix );
        request._operation = LocalStoreOperation::List;
        return enqueueRequest( std::move( request ) );
    }

    int32 ThreadedLocalStore::pollCompletions( vector<LocalStoreCompletion>& outListCompletion )
    {
        vector<LocalStoreCompletion> listCompletion;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            listCompletion.swap( _listCompletion );
            _pendingCount -= static_cast<int32>( listCompletion.size() );
        }
        for ( LocalStoreCompletion& completion : listCompletion )
        {
            outListCompletion.push_back( std::move( completion ) );
        }
        return static_cast<int32>( listCompletion.size() );
    }

    int32 ThreadedLocalStore::getPendingCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _pendingCount;
    }

    void ThreadedLocalStore::shutdown()
    {
        _worker.stop();
    }

    uint64 ThreadedLocalStore::enqueueRequest( LocalStoreRequest request )
    {
        uint64 requestID = 0;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            requestID          = _requestIDAllocator.allocate();
            request._requestID = requestID;
            ++_pendingCount;
        }
        if ( _worker.submit( std::move( request ) ) == false )
        {
            // 종료한 뒤에 들어온 요청이다 — 실행하지 않고 실패로 끝낸다(submit 이 거절하면 request 는 옮겨지지 않았다).
            std::scoped_lock<mutex> lock{ _mutex };
            _listCompletion.push_back( LocalStoreRequestUtil::makeCompletion( request, LocalStoreResult::IOError ) );
        }
        return requestID;
    }

    void ThreadedLocalStore::processRequest( LocalStoreRequest& request, bool bStopping )
    {
        // 종료하는 중에도 쓰기 · 지우기는 끝까지 한다(종료 때 세이브를 잃지 않는다) — 읽기 · 나열은 받을 사람이 없다.
        const bool              bDropped   = bStopping && ( request._operation == LocalStoreOperation::Read || request._operation == LocalStoreOperation::List );
        LocalStoreCompletion    completion = bDropped ? LocalStoreRequestUtil::makeCompletion( request, LocalStoreResult::IOError )
                                                      : LocalStoreRequestUtil::execute( request, *_storage, _sealContext );
        std::scoped_lock<mutex> lock{ _mutex };
        _listCompletion.push_back( std::move( completion ) );
    }
} // namespace sw
