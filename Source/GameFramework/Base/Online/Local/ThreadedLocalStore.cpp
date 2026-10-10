#include "pch.h"

#include "GameFramework/Base/Online/Local/ThreadedLocalStore.h"

namespace sw
{
    ThreadedLocalStore::ThreadedLocalStore( unique_ptr<ILocalSlotStorage> storage, const LocalSealContext& sealContext )
        : _listQueuedRequest{}
        , _listCompletion{}
        , _sealContext{ sealContext }
        , _storage{ std::move( storage ) }
        , _worker{}
        , _mutex{}
        , _requestReady{}
        , _nextRequestId{ 1 }
        , _pendingCount{ 0 }
        , _bStopping{ SW_FALSE }
    {
        _worker = std::thread( &ThreadedLocalStore::runWorker, this );
    }

    ThreadedLocalStore::~ThreadedLocalStore() { shutdown(); }

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
        {
            std::scoped_lock<mutex> lock{ _mutex };
            if ( _bStopping == SW_TRUE && _worker.joinable() == false )
                return;
            _bStopping = SW_TRUE;
        }
        _requestReady.notify_all();
        if ( _worker.joinable() )
            _worker.join();
    }

    uint64 ThreadedLocalStore::enqueueRequest( LocalStoreRequest request )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        request._requestId = _nextRequestId++;
        ++_pendingCount;
        const uint64 requestId = request._requestId;
        if ( _bStopping == SW_TRUE )
        {
            _listCompletion.push_back( LocalStoreRequestUtil::makeCompletion( request, LocalStoreResult::IOError ) );
            return requestId;
        }
        _listQueuedRequest.push_back( std::move( request ) );
        _requestReady.notify_one();
        return requestId;
    }

    void ThreadedLocalStore::runWorker()
    {
        for ( ;; )
        {
            LocalStoreRequest request;
            bool              bStopping = false;
            {
                std::unique_lock<mutex> lock{ _mutex };
                while ( _listQueuedRequest.empty() && _bStopping == SW_FALSE )
                {
                    _requestReady.wait( lock );
                }
                if ( _listQueuedRequest.empty() )
                    return; // 내리는 중이고 남은 일이 없다
                request = std::move( _listQueuedRequest.front() );
                _listQueuedRequest.pop_front();
                bStopping = _bStopping == SW_TRUE;
            }
            // 내리는 중에도 쓰기 · 지우기는 끝까지 한다(종료 때 세이브를 잃지 않는다) — 읽기 · 나열은 받을 사람이 없다.
            const bool              bDropped   = bStopping && ( request._operation == LocalStoreOperation::Read || request._operation == LocalStoreOperation::List );
            LocalStoreCompletion    completion = bDropped ? LocalStoreRequestUtil::makeCompletion( request, LocalStoreResult::IOError )
                                                          : LocalStoreRequestUtil::execute( request, *_storage, _sealContext );
            std::scoped_lock<mutex> lock{ _mutex };
            _listCompletion.push_back( std::move( completion ) );
        }
    }
} // namespace sw
